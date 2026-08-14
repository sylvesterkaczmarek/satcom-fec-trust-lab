#include "acquisition/acquisition_neon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>

#if defined(SATCOMFEC_ACQUISITION_NEON_COMPILED) && \
    (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define SATCOMFEC_ACQUISITION_HAS_NEON 1
#else
#define SATCOMFEC_ACQUISITION_HAS_NEON 0
#endif

namespace satcomfec::acquisition {

#if SATCOMFEC_ACQUISITION_HAS_NEON
namespace {

static_assert(
    sizeof(ComplexF) == 2 * sizeof(float),
    "NEON interleaved loads require two contiguous float components per sample");

constexpr std::size_t kNeonLanes = 4;
constexpr std::size_t kMaximumTimingGroups = 8;

bool finite_iq(const ComplexF& sample) {
    return std::isfinite(sample.real()) && std::isfinite(sample.imag());
}

float horizontal_sum_f32(float32x4_t value) {
    const float32x2_t pair = vadd_f32(vget_low_f32(value), vget_high_f32(value));
    return vget_lane_f32(vpadd_f32(pair, pair), 0);
}

std::complex<double> correlate_preamble_samples_neon(
    const ComplexF* received,
    const ComplexF* weights,
    std::size_t sample_count) {
    float32x4_t real_accumulator = vdupq_n_f32(0.0F);
    float32x4_t imag_accumulator = vdupq_n_f32(0.0F);

    std::size_t sample_index = 0;
    for (; sample_index + 4 <= sample_count; sample_index += 4) {
        const float32x4x2_t received_vector =
            vld2q_f32(reinterpret_cast<const float*>(received + sample_index));
        const float32x4x2_t weight_vector =
            vld2q_f32(reinterpret_cast<const float*>(weights + sample_index));

        float32x4_t real_product =
            vmulq_f32(received_vector.val[0], weight_vector.val[0]);
        real_product =
            vmlsq_f32(real_product, received_vector.val[1], weight_vector.val[1]);

        float32x4_t imag_product =
            vmulq_f32(received_vector.val[0], weight_vector.val[1]);
        imag_product =
            vmlaq_f32(imag_product, received_vector.val[1], weight_vector.val[0]);

        real_accumulator = vaddq_f32(real_accumulator, real_product);
        imag_accumulator = vaddq_f32(imag_accumulator, imag_product);
    }

    float real = horizontal_sum_f32(real_accumulator);
    float imag = horizontal_sum_f32(imag_accumulator);
    for (; sample_index < sample_count; ++sample_index) {
        const float received_real = received[sample_index].real();
        const float received_imag = received[sample_index].imag();
        const float weight_real = weights[sample_index].real();
        const float weight_imag = weights[sample_index].imag();
        real += received_real * weight_real - received_imag * weight_imag;
        imag += received_real * weight_imag + received_imag * weight_real;
    }

    return {static_cast<double>(real), static_cast<double>(imag)};
}

template <std::size_t GroupCount>
void correlate_consecutive_timings_neon(
    const ComplexF* received_at_first_timing,
    const ComplexF* weights,
    std::size_t sample_count,
    std::array<float, GroupCount * kNeonLanes>& correlation_real,
    std::array<float, GroupCount * kNeonLanes>& correlation_imag) {
    static_assert(GroupCount >= 1 && GroupCount <= kMaximumTimingGroups);
    std::array<float32x4_t, GroupCount> real_accumulators;
    std::array<float32x4_t, GroupCount> imag_accumulators;
    for (std::size_t group = 0; group < GroupCount; ++group) {
        real_accumulators[group] = vdupq_n_f32(0.0F);
        imag_accumulators[group] = vdupq_n_f32(0.0F);
    }

    // Each lane is one timing hypothesis. A scalar CFO/preamble weight is
    // reused across the timing tile before advancing to the next sample.
    for (std::size_t sample_index = 0;
         sample_index < sample_count;
         ++sample_index) {
        const float weight_real = weights[sample_index].real();
        const float weight_imag = weights[sample_index].imag();
        for (std::size_t group = 0; group < GroupCount; ++group) {
            const ComplexF* group_samples =
                received_at_first_timing + sample_index + group * kNeonLanes;
            const float32x4x2_t received =
                vld2q_f32(reinterpret_cast<const float*>(group_samples));
            real_accumulators[group] = vmlaq_n_f32(
                real_accumulators[group], received.val[0], weight_real);
            real_accumulators[group] = vmlsq_n_f32(
                real_accumulators[group], received.val[1], weight_imag);
            imag_accumulators[group] = vmlaq_n_f32(
                imag_accumulators[group], received.val[0], weight_imag);
            imag_accumulators[group] = vmlaq_n_f32(
                imag_accumulators[group], received.val[1], weight_real);
        }
    }

    for (std::size_t group = 0; group < GroupCount; ++group) {
        vst1q_f32(
            correlation_real.data() + group * kNeonLanes,
            real_accumulators[group]);
        vst1q_f32(
            correlation_imag.data() + group * kNeonLanes,
            imag_accumulators[group]);
    }
}

bool consecutive_timings(
    const std::vector<std::size_t>& timing_offsets,
    std::size_t first_index,
    std::size_t count) {
    if (count == 0 || first_index > timing_offsets.size() ||
        count > timing_offsets.size() - first_index) {
        return false;
    }
    const std::size_t first_timing = timing_offsets[first_index];
    for (std::size_t lane = 1; lane < count; ++lane) {
        if (first_timing > std::numeric_limits<std::size_t>::max() - lane ||
            timing_offsets[first_index + lane] != first_timing + lane) {
            return false;
        }
    }
    return true;
}

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

template <std::size_t GroupCount>
void evaluate_consecutive_timing_batch(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    const PreparedFrequencyHypothesis& frequency,
    std::size_t timing_index,
    AcquisitionResult& result);

void evaluate_contiguous_timing_grid(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    const PreparedFrequencyHypothesis& frequency,
    AcquisitionResult& result) {
    std::size_t timing_index = 0;
    const std::size_t timing_count = plan.timing_offsets.size();
    while (timing_count - timing_index >= kMaximumTimingGroups * kNeonLanes) {
        evaluate_consecutive_timing_batch<kMaximumTimingGroups>(
            received_iq, plan, frequency, timing_index, result);
        timing_index += kMaximumTimingGroups * kNeonLanes;
    }
    if (timing_count - timing_index >= 4 * kNeonLanes) {
        evaluate_consecutive_timing_batch<4>(
            received_iq, plan, frequency, timing_index, result);
        timing_index += 4 * kNeonLanes;
    }
    if (timing_count - timing_index >= 2 * kNeonLanes) {
        evaluate_consecutive_timing_batch<2>(
            received_iq, plan, frequency, timing_index, result);
        timing_index += 2 * kNeonLanes;
    }
    if (timing_count - timing_index >= kNeonLanes) {
        evaluate_consecutive_timing_batch<1>(
            received_iq, plan, frequency, timing_index, result);
        timing_index += kNeonLanes;
    }
    for (; timing_index < timing_count; ++timing_index) {
        const std::size_t timing_offset = plan.timing_offsets[timing_index];
        AcquisitionCandidate candidate;
        candidate.hypothesis.timing_offset = timing_offset;
        candidate.hypothesis.frequency_offset_hz = frequency.frequency_offset_hz;
        candidate.correlation = correlate_preamble_samples_neon(
            received_iq.data() + timing_offset,
            frequency.matched_filter_weights_f32.data(),
            plan.preamble_length);
        candidate.score = std::norm(candidate.correlation);
        candidate.valid = true;
        consider_candidate(candidate, result.best, result.second_best);
        ++result.evaluated_candidate_count;
    }
}

template <std::size_t GroupCount>
void evaluate_consecutive_timing_batch(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    const PreparedFrequencyHypothesis& frequency,
    std::size_t timing_index,
    AcquisitionResult& result) {
    constexpr std::size_t kTimingCount = GroupCount * kNeonLanes;
    std::array<float, kTimingCount> correlation_real{};
    std::array<float, kTimingCount> correlation_imag{};
    correlate_consecutive_timings_neon<GroupCount>(
        received_iq.data() + plan.timing_offsets[timing_index],
        frequency.matched_filter_weights_f32.data(),
        plan.preamble_length,
        correlation_real,
        correlation_imag);

    for (std::size_t lane = 0; lane < kTimingCount; ++lane) {
        AcquisitionCandidate candidate;
        candidate.hypothesis.timing_offset =
            plan.timing_offsets[timing_index + lane];
        candidate.hypothesis.frequency_offset_hz = frequency.frequency_offset_hz;
        candidate.correlation = {
            static_cast<double>(correlation_real[lane]),
            static_cast<double>(correlation_imag[lane]),
        };
        candidate.score = std::norm(candidate.correlation);
        candidate.valid = true;
        consider_candidate(candidate, result.best, result.second_best);
        ++result.evaluated_candidate_count;
    }
}

}  // namespace
#endif

AcquisitionResult run_neon_acquisition_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;

#if SATCOMFEC_ACQUISITION_HAS_NEON
    result.implementation = "neon";

    for (const PreparedFrequencyHypothesis& frequency : plan.frequency_hypotheses) {
        if (plan.timing_offsets_contiguous) {
            evaluate_contiguous_timing_grid(
                received_iq, plan, frequency, result);
            continue;
        }
        std::size_t timing_index = 0;
        while (timing_index < plan.timing_offsets.size()) {
            if (consecutive_timings(
                    plan.timing_offsets,
                    timing_index,
                    kMaximumTimingGroups * kNeonLanes)) {
                evaluate_consecutive_timing_batch<kMaximumTimingGroups>(
                    received_iq, plan, frequency, timing_index, result);
                timing_index += kMaximumTimingGroups * kNeonLanes;
                continue;
            }
            if (consecutive_timings(
                    plan.timing_offsets, timing_index, 4 * kNeonLanes)) {
                evaluate_consecutive_timing_batch<4>(
                    received_iq, plan, frequency, timing_index, result);
                timing_index += 4 * kNeonLanes;
                continue;
            }
            if (consecutive_timings(
                    plan.timing_offsets, timing_index, 2 * kNeonLanes)) {
                evaluate_consecutive_timing_batch<2>(
                    received_iq, plan, frequency, timing_index, result);
                timing_index += 2 * kNeonLanes;
                continue;
            }
            if (consecutive_timings(
                    plan.timing_offsets, timing_index, kNeonLanes)) {
                evaluate_consecutive_timing_batch<1>(
                    received_iq, plan, frequency, timing_index, result);
                timing_index += kNeonLanes;
                continue;
            }

            const std::size_t timing_offset = plan.timing_offsets[timing_index];
            AcquisitionCandidate candidate;
            candidate.hypothesis.timing_offset = timing_offset;
            candidate.hypothesis.frequency_offset_hz = frequency.frequency_offset_hz;
            candidate.correlation = correlate_preamble_samples_neon(
                received_iq.data() + timing_offset,
                frequency.matched_filter_weights_f32.data(),
                plan.preamble_length);
            candidate.score = std::norm(candidate.correlation);
            candidate.valid = true;
            consider_candidate(candidate, result.best, result.second_best);
            ++result.evaluated_candidate_count;
            ++timing_index;
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
#else
    static_cast<void>(received_iq);
    static_cast<void>(plan);
    result.implementation = "unavailable";
    result.error_message = "NEON acquisition kernel is not compiled for this target";
#endif

    return result;
}

AcquisitionResult run_neon_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;

#if SATCOMFEC_ACQUISITION_HAS_NEON
    result.implementation = "neon";

    if (!std::isfinite(plan.sample_rate_hz) || plan.sample_rate_hz <= 0.0 ||
        plan.preamble_length == 0 || plan.timing_offsets.empty() ||
        plan.frequency_hypotheses.empty()) {
        result.error_message = "acquisition plan is incomplete";
        return result;
    }
    if (received_iq.size() < plan.preamble_length) {
        result.error_message = "received IQ is shorter than the preamble";
        return result;
    }
    for (const ComplexF& sample : received_iq) {
        if (!finite_iq(sample)) {
            result.error_message = "received IQ contains a non-finite sample";
            return result;
        }
    }
    for (const PreparedFrequencyHypothesis& frequency : plan.frequency_hypotheses) {
        if (frequency.matched_filter_weights_f32.size() != plan.preamble_length) {
            result.error_message = "acquisition plan has an invalid NEON weight table";
            return result;
        }
    }
    for (std::size_t timing_offset : plan.timing_offsets) {
        if (timing_offset > received_iq.size() - plan.preamble_length) {
            result.error_message = "a timing hypothesis extends beyond the received IQ window";
            return result;
        }
    }
    const bool timing_offsets_contiguous = consecutive_timings(
        plan.timing_offsets, 0, plan.timing_offsets.size());
    if (plan.timing_offsets_contiguous != timing_offsets_contiguous ||
        (timing_offsets_contiguous &&
         plan.contiguous_timing_start != plan.timing_offsets.front())) {
        result.error_message = "acquisition plan has inconsistent timing-layout metadata";
        return result;
    }

    return run_neon_acquisition_steady_state(received_iq, plan);
#else
    static_cast<void>(received_iq);
    static_cast<void>(plan);
    result.implementation = "unavailable";
    result.error_message = "NEON acquisition kernel is not compiled for this target";
    return result;
#endif
}

bool acquisition_neon_kernel_compiled() {
    return SATCOMFEC_ACQUISITION_HAS_NEON != 0;
}

const char* acquisition_neon_mechanism() {
    return acquisition_neon_kernel_compiled()
               ? "neon-eight-vector-timing-tile"
               : "unavailable";
}

}  // namespace satcomfec::acquisition
