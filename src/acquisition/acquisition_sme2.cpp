#include "acquisition/acquisition_sme2.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>

#if defined(SATCOMFEC_ACQUISITION_SME2_COMPILED) && defined(__ARM_FEATURE_SME2)
#include <arm_sme.h>
#include <arm_sve.h>
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

bool validate_sme2_plan(const AcquisitionPlan& plan, std::string& error_message) {
    if (!std::isfinite(plan.sample_rate_hz) || plan.sample_rate_hz <= 0.0 ||
        plan.preamble_length == 0 || plan.timing_offsets.empty() ||
        plan.frequency_hypotheses.empty()) {
        error_message = "acquisition plan is incomplete";
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
    if (!__arm_has_sme()) {
        return false;
    }
#if defined(__APPLE__)
    int supported = 0;
    std::size_t supported_size = sizeof(supported);
    return sysctlbyname(
               "hw.optional.arm.FEAT_SME2",
               &supported,
               &supported_size,
               nullptr,
               0) == 0 &&
           supported != 0;
#elif defined(__linux__) && defined(HWCAP2_SME2)
    return (getauxval(AT_HWCAP2) & HWCAP2_SME2) != 0;
#else
    return false;
#endif
}

__arm_locally_streaming std::size_t streaming_lanes_f32() {
    return svcntw();
}

__arm_locally_streaming __arm_new("za")
void correlate_sme2_kernel(
    const float* received_real,
    const float* received_imag,
    const float* weight_real,
    const float* weight_imag,
    std::size_t timing_count,
    std::size_t frequency_count,
    std::size_t preamble_length,
    float* correlation_real,
    float* correlation_imag) {
    const std::size_t lanes = svcntw();
    const std::size_t batch_width = 4 * lanes;
    const svfloat32_t zero = svdup_f32(0.0F);
    const svfloat32x4_t zeros = svcreate4_f32(zero, zero, zero, zero);

    for (std::size_t frequency_index = 0;
         frequency_index < frequency_count;
         ++frequency_index) {
        const std::size_t weight_base = frequency_index * preamble_length;
        const std::size_t output_base = frequency_index * timing_count;

        for (std::size_t timing_base = 0;
             timing_base < timing_count;
             timing_base += batch_width) {
            const std::size_t lane_base_0 = timing_base;
            const std::size_t lane_base_1 = timing_base + lanes;
            const std::size_t lane_base_2 = timing_base + 2 * lanes;
            const std::size_t lane_base_3 = timing_base + 3 * lanes;
            const svbool_t pg0 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_0),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg1 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_1),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg2 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_2),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg3 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_3),
                static_cast<std::uint64_t>(timing_count));
            const std::size_t safe_base_1 = std::min(lane_base_1, timing_count);
            const std::size_t safe_base_2 = std::min(lane_base_2, timing_count);
            const std::size_t safe_base_3 = std::min(lane_base_3, timing_count);

            // Two ZA vector groups retain 4 * SVL real and imaginary correlations.
            svwrite_za32_f32_vg1x4(0, zeros);
            svwrite_za32_f32_vg1x4(1, zeros);

            for (std::size_t sample_index = 0;
                 sample_index < preamble_length;
                 ++sample_index) {
                const std::size_t sample_base = sample_index * timing_count;
                const svfloat32x4_t received_real_group = svcreate4_f32(
                    svld1_f32(pg0, received_real + sample_base + lane_base_0),
                    svld1_f32(pg1, received_real + sample_base + safe_base_1),
                    svld1_f32(pg2, received_real + sample_base + safe_base_2),
                    svld1_f32(pg3, received_real + sample_base + safe_base_3));
                const svfloat32x4_t received_imag_group = svcreate4_f32(
                    svld1_f32(pg0, received_imag + sample_base + lane_base_0),
                    svld1_f32(pg1, received_imag + sample_base + safe_base_1),
                    svld1_f32(pg2, received_imag + sample_base + safe_base_2),
                    svld1_f32(pg3, received_imag + sample_base + safe_base_3));
                const svfloat32_t weight_real_vector = svdup_f32(
                    weight_real[weight_base + sample_index]);
                const svfloat32_t weight_imag_vector = svdup_f32(
                    weight_imag[weight_base + sample_index]);

                svmla_single_za32_f32_vg1x4(
                    0, received_real_group, weight_real_vector);
                svmls_single_za32_f32_vg1x4(
                    0, received_imag_group, weight_imag_vector);
                svmla_single_za32_f32_vg1x4(
                    1, received_real_group, weight_imag_vector);
                svmla_single_za32_f32_vg1x4(
                    1, received_imag_group, weight_real_vector);
            }

            const svfloat32x4_t real_group = svread_za32_f32_vg1x4(0);
            const svfloat32x4_t imag_group = svread_za32_f32_vg1x4(1);
            svst1_f32(
                pg0,
                correlation_real + output_base + lane_base_0,
                svget4_f32(real_group, 0));
            svst1_f32(
                pg1,
                correlation_real + output_base + safe_base_1,
                svget4_f32(real_group, 1));
            svst1_f32(
                pg2,
                correlation_real + output_base + safe_base_2,
                svget4_f32(real_group, 2));
            svst1_f32(
                pg3,
                correlation_real + output_base + safe_base_3,
                svget4_f32(real_group, 3));
            svst1_f32(
                pg0,
                correlation_imag + output_base + lane_base_0,
                svget4_f32(imag_group, 0));
            svst1_f32(
                pg1,
                correlation_imag + output_base + safe_base_1,
                svget4_f32(imag_group, 1));
            svst1_f32(
                pg2,
                correlation_imag + output_base + safe_base_2,
                svget4_f32(imag_group, 2));
            svst1_f32(
                pg3,
                correlation_imag + output_base + safe_base_3,
                svget4_f32(imag_group, 3));
        }
    }
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
    workspace.received_real_by_sample_and_timing.resize(packed_sample_count);
    workspace.received_imag_by_sample_and_timing.resize(packed_sample_count);
    workspace.correlation_real_by_frequency_and_timing.resize(candidate_count);
    workspace.correlation_imag_by_frequency_and_timing.resize(candidate_count);

    pack_sme2_acquisition_capture_steady_state(received_iq, plan, workspace);
    return true;
}

void pack_sme2_acquisition_capture_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace) {

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
            workspace.received_real_by_sample_and_timing[packed_base + timing_index] =
                sample.real();
            workspace.received_imag_by_sample_and_timing[packed_base + timing_index] =
                sample.imag();
        }
    }
}

AcquisitionResult run_sme2_acquisition_prepared(
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
    if (!validate_sme2_plan(plan, plan_error)) {
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
        workspace.received_real_by_sample_and_timing.size() != packed_sample_count ||
        workspace.received_imag_by_sample_and_timing.size() != packed_sample_count ||
        workspace.correlation_real_by_frequency_and_timing.size() != candidate_count ||
        workspace.correlation_imag_by_frequency_and_timing.size() != candidate_count) {
        result.implementation = "sme2";
        result.error_message = "SME2 acquisition workspace dimensions are inconsistent";
        return result;
    }

    return run_sme2_acquisition_steady_state(plan, workspace);
#else
    static_cast<void>(plan);
    static_cast<void>(workspace);
    result.implementation = "unavailable";
    result.error_message = "SME2 acquisition kernel is not compiled for this target";
    return result;
#endif
}

AcquisitionResult run_sme2_acquisition_steady_state(
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace) {
#if SATCOMFEC_ACQUISITION_HAS_SME2
    correlate_sme2_kernel(
        workspace.received_real_by_sample_and_timing.data(),
        workspace.received_imag_by_sample_and_timing.data(),
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
    return run_sme2_acquisition_prepared(plan, workspace);
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
    return acquisition_sme2_runtime_supported() ? streaming_lanes_f32() : 0;
#else
    return 0;
#endif
}

const char* acquisition_sme2_mechanism() {
    return acquisition_sme2_kernel_compiled() ? kSme2Mechanism : "unavailable";
}

}  // namespace satcomfec::acquisition
