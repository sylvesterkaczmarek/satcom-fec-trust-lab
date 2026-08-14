#include "acquisition/acquisition_sme2.h"

#include "acquisition/acquisition_sme2_kernel.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <string>

#if defined(SATCOMFEC_ACQUISITION_SME2_KERNEL_AVAILABLE)
#define SATCOMFEC_ACQUISITION_HAS_SME2 1
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/auxv.h>
#if defined(__has_include)
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
#endif
#endif
#else
#define SATCOMFEC_ACQUISITION_HAS_SME2 0
#endif

namespace satcomfec::acquisition {
namespace {

constexpr const char* kSme2Mechanism = "za-vgx4-fmla-fmls";

static_assert(
    sizeof(ComplexF) == 2 * sizeof(float),
    "SME2 interleaved loads require two contiguous float components per sample");

bool finite_iq(const ComplexF& sample) {
    return std::isfinite(sample.real()) && std::isfinite(sample.imag());
}

bool checked_product(std::size_t left, std::size_t right, std::size_t& product) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    product = left * right;
    return true;
}

bool timing_grid_is_contiguous(
    const std::vector<std::size_t>& timing_offsets,
    std::size_t& first_timing) {
    first_timing = timing_offsets.front();
    for (std::size_t index = 1; index < timing_offsets.size(); ++index) {
        if (first_timing > std::numeric_limits<std::size_t>::max() - index ||
            timing_offsets[index] != first_timing + index) {
            return false;
        }
    }
    return true;
}

bool validate_sme2_plan(const AcquisitionPlan& plan, std::string& error_message) {
    if (!std::isfinite(plan.sample_rate_hz) || plan.sample_rate_hz <= 0.0 ||
        plan.preamble_length == 0 || plan.timing_offsets.empty() ||
        plan.frequency_hypotheses.empty()) {
        error_message = "acquisition plan is incomplete";
        return false;
    }

    std::size_t contiguous_timing_start = 0;
    const bool timing_offsets_contiguous = timing_grid_is_contiguous(
        plan.timing_offsets, contiguous_timing_start);
    if (plan.timing_offsets_contiguous != timing_offsets_contiguous ||
        (timing_offsets_contiguous &&
         plan.contiguous_timing_start != contiguous_timing_start)) {
        error_message = "acquisition plan has inconsistent timing-layout metadata";
        return false;
    }

    std::size_t weight_count = 0;
    if (!checked_product(
            plan.frequency_hypotheses.size(), plan.preamble_length, weight_count) ||
        plan.matched_filter_weights_real_f32.size() != weight_count ||
        plan.matched_filter_weights_imag_f32.size() != weight_count) {
        error_message = "acquisition plan has an invalid SME2 weight table";
        return false;
    }
    return true;
}

bool validate_plan_and_input(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    std::string& error_message) {
    if (!validate_sme2_plan(plan, error_message)) {
        return false;
    }
    if (received_iq.size() < plan.preamble_length) {
        error_message = "received IQ is shorter than the preamble";
        return false;
    }
    for (const ComplexF& sample : received_iq) {
        if (!finite_iq(sample)) {
            error_message = "received IQ contains a non-finite sample";
            return false;
        }
    }
    for (std::size_t timing_offset : plan.timing_offsets) {
        if (timing_offset > received_iq.size() - plan.preamble_length) {
            error_message = "a timing hypothesis extends beyond the received IQ window";
            return false;
        }
    }
    return true;
}

#if SATCOMFEC_ACQUISITION_HAS_SME2

void consider_candidate(
    const AcquisitionCandidate& candidate,
    AcquisitionCandidate& best,
    AcquisitionCandidate& second_best) {
    if (!best.valid || candidate.score > best.score) {
        second_best = best;
        best = candidate;
        return;
    }
    if (!second_best.valid || candidate.score > second_best.score) {
        second_best = candidate;
    }
}

AcquisitionResult result_from_workspace(
    const AcquisitionPlan& plan,
    const Sme2AcquisitionWorkspace& workspace) {
    AcquisitionResult result;
    result.implementation = "sme2";

    for (std::size_t frequency_index = 0;
         frequency_index < workspace.frequency_count;
         ++frequency_index) {
        for (std::size_t timing_index = 0;
             timing_index < workspace.timing_count;
             ++timing_index) {
            const std::size_t candidate_index =
                frequency_index * workspace.timing_count + timing_index;
            AcquisitionCandidate candidate;
            candidate.hypothesis.timing_offset = plan.timing_offsets[timing_index];
            candidate.hypothesis.frequency_offset_hz =
                plan.frequency_hypotheses[frequency_index].frequency_offset_hz;
            candidate.correlation = {
                workspace.correlation_real_by_frequency_and_timing[candidate_index],
                workspace.correlation_imag_by_frequency_and_timing[candidate_index],
            };
            candidate.score = std::norm(candidate.correlation);
            candidate.valid = true;
            consider_candidate(candidate, result.best, result.second_best);
            ++result.evaluated_candidate_count;
        }
    }

    result.ok = result.best.valid;
    if (!result.ok) {
        result.error_message = "no acquisition candidate was evaluated";
        return result;
    }
    if (result.second_best.valid) {
        const double denominator = std::max(
            result.second_best.score, std::numeric_limits<double>::min());
        result.peak_ratio = result.best.score / denominator;
        result.normalized_peak_separation = result.best.score > 0.0
                                                ? (result.best.score - result.second_best.score) /
                                                      result.best.score
                                                : 0.0;
    } else {
        result.peak_ratio = std::numeric_limits<double>::infinity();
        result.normalized_peak_separation = 1.0;
    }
    return result;
}

bool runtime_has_sme2() {
#if defined(__APPLE__)
    const auto feature_enabled = [](const char* name) {
        int supported = 0;
        std::size_t supported_size = sizeof(supported);
        return sysctlbyname(
                   name,
                   &supported,
                   &supported_size,
                   nullptr,
                   0) == 0 &&
               supported != 0;
    };
    return feature_enabled("hw.optional.arm.FEAT_SME") &&
           feature_enabled("hw.optional.arm.FEAT_SME2");
#elif defined(__linux__) && defined(HWCAP2_SME2)
    // Check the kernel-provided capability mask before touching streaming state.
    const unsigned long capabilities = getauxval(AT_HWCAP2);
#if defined(HWCAP2_SME)
    return (capabilities & HWCAP2_SME) != 0 &&
           (capabilities & HWCAP2_SME2) != 0;
#else
    return (capabilities & HWCAP2_SME2) != 0;
#endif
#else
    return false;
#endif
}

#endif

}  // namespace

bool prepare_sme2_acquisition_workspace(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace,
    std::string& error_message) {
    error_message.clear();
    if (!validate_plan_and_input(received_iq, plan, error_message)) {
        return false;
    }

    std::size_t packed_sample_count = 0;
    std::size_t candidate_count = 0;
    if (!checked_product(
            plan.preamble_length, plan.timing_offsets.size(), packed_sample_count) ||
        !checked_product(
            plan.frequency_hypotheses.size(),
            plan.timing_offsets.size(),
            candidate_count)) {
        error_message = "SME2 acquisition workspace size overflows size_t";
        return false;
    }

    workspace.timing_count = plan.timing_offsets.size();
    workspace.frequency_count = plan.frequency_hypotheses.size();
    workspace.preamble_length = plan.preamble_length;
    workspace.packed_input_required = !plan.timing_offsets_contiguous;
    workspace.contiguous_timing_start = plan.contiguous_timing_start;
    if (workspace.packed_input_required) {
        workspace.received_by_sample_and_timing.resize(packed_sample_count);
    } else {
        std::vector<ComplexF>().swap(workspace.received_by_sample_and_timing);
    }
    workspace.correlation_real_by_frequency_and_timing.resize(candidate_count);
    workspace.correlation_imag_by_frequency_and_timing.resize(candidate_count);

    prepare_sme2_acquisition_capture_steady_state(received_iq, plan, workspace);
    return true;
}

void prepare_sme2_acquisition_capture_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace) {
    if (!workspace.packed_input_required) {
        return;
    }

    // Sample-major packing makes each timing batch contiguous for scalable loads.
    for (std::size_t sample_index = 0;
         sample_index < plan.preamble_length;
         ++sample_index) {
        const std::size_t packed_base = sample_index * workspace.timing_count;
        for (std::size_t timing_index = 0;
             timing_index < workspace.timing_count;
             ++timing_index) {
            const ComplexF& sample = received_iq[
                plan.timing_offsets[timing_index] + sample_index];
            workspace.received_by_sample_and_timing[packed_base + timing_index] =
                sample;
        }
    }
}

AcquisitionResult run_sme2_acquisition_prepared(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace) {
    AcquisitionResult result;
#if SATCOMFEC_ACQUISITION_HAS_SME2
    if (!acquisition_sme2_runtime_supported()) {
        result.implementation = "unavailable";
        result.error_message =
            "SME2 acquisition kernel is compiled but SME2 execution is unavailable on this host";
        return result;
    }

    std::string plan_error;
    if (!validate_plan_and_input(received_iq, plan, plan_error)) {
        result.implementation = "sme2";
        result.error_message = plan_error;
        return result;
    }

    std::size_t packed_sample_count = 0;
    std::size_t candidate_count = 0;
    if (!checked_product(
            workspace.preamble_length, workspace.timing_count, packed_sample_count) ||
        !checked_product(
            workspace.frequency_count, workspace.timing_count, candidate_count) ||
        workspace.timing_count != plan.timing_offsets.size() ||
        workspace.frequency_count != plan.frequency_hypotheses.size() ||
        workspace.preamble_length != plan.preamble_length ||
        workspace.received_by_sample_and_timing.size() !=
            (workspace.packed_input_required ? packed_sample_count : 0) ||
        workspace.correlation_real_by_frequency_and_timing.size() != candidate_count ||
        workspace.correlation_imag_by_frequency_and_timing.size() != candidate_count) {
        result.implementation = "sme2";
        result.error_message = "SME2 acquisition workspace dimensions are inconsistent";
        return result;
    }

    if (workspace.packed_input_required == plan.timing_offsets_contiguous ||
        (!workspace.packed_input_required &&
         workspace.contiguous_timing_start != plan.contiguous_timing_start)) {
        result.implementation = "sme2";
        result.error_message = "SME2 acquisition workspace layout is inconsistent";
        return result;
    }

    return run_sme2_acquisition_steady_state(received_iq, plan, workspace);
#else
    static_cast<void>(received_iq);
    static_cast<void>(plan);
    static_cast<void>(workspace);
    result.implementation = "unavailable";
    result.error_message = "SME2 acquisition kernel is not compiled for this target";
    return result;
#endif
}

AcquisitionResult run_sme2_acquisition_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace) {
#if SATCOMFEC_ACQUISITION_HAS_SME2
    const ComplexF* received = workspace.packed_input_required
                                   ? workspace.received_by_sample_and_timing.data()
                                   : received_iq.data() +
                                         workspace.contiguous_timing_start;
    const std::size_t sample_stride = workspace.packed_input_required
                                          ? workspace.timing_count
                                          : 1;
    detail::correlate_sme2_kernel(
        reinterpret_cast<const float*>(received),
        sample_stride,
        plan.matched_filter_weights_real_f32.data(),
        plan.matched_filter_weights_imag_f32.data(),
        workspace.timing_count,
        workspace.frequency_count,
        workspace.preamble_length,
        workspace.correlation_real_by_frequency_and_timing.data(),
        workspace.correlation_imag_by_frequency_and_timing.data());
    return result_from_workspace(plan, workspace);
#else
    AcquisitionResult result;
    static_cast<void>(received_iq);
    static_cast<void>(plan);
    static_cast<void>(workspace);
    result.implementation = "unavailable";
    result.error_message = "SME2 acquisition kernel is not compiled for this target";
    return result;
#endif
}

AcquisitionResult run_sme2_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;
    if (!acquisition_sme2_kernel_compiled()) {
        result.implementation = "unavailable";
        result.error_message = "SME2 acquisition kernel is not compiled for this target";
        return result;
    }
    if (!acquisition_sme2_runtime_supported()) {
        result.implementation = "unavailable";
        result.error_message =
            "SME2 acquisition kernel is compiled but SME2 execution is unavailable on this host";
        return result;
    }

    Sme2AcquisitionWorkspace workspace;
    std::string error_message;
    if (!prepare_sme2_acquisition_workspace(
            received_iq, plan, workspace, error_message)) {
        result.implementation = "sme2";
        result.error_message = error_message;
        return result;
    }
    return run_sme2_acquisition_prepared(received_iq, plan, workspace);
}

bool acquisition_sme2_kernel_compiled() {
    return SATCOMFEC_ACQUISITION_HAS_SME2 != 0;
}

bool acquisition_sme2_runtime_supported() {
#if SATCOMFEC_ACQUISITION_HAS_SME2
    return runtime_has_sme2();
#else
    return false;
#endif
}

std::size_t acquisition_sme2_streaming_lanes_f32() {
#if SATCOMFEC_ACQUISITION_HAS_SME2
    return acquisition_sme2_runtime_supported()
               ? detail::sme2_streaming_lanes_f32()
               : 0;
#else
    return 0;
#endif
}

const char* acquisition_sme2_mechanism() {
    return acquisition_sme2_kernel_compiled() ? kSme2Mechanism : "unavailable";
}

}  // namespace satcomfec::acquisition
