#include "acquisition/acquisition_neon.h"

#include <algorithm>
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

bool finite_iq(const ComplexF& sample) {
    return std::isfinite(sample.real()) && std::isfinite(sample.imag());
}

float horizontal_sum_f32(float32x4_t value) {
    const float32x2_t pair = vadd_f32(vget_low_f32(value), vget_high_f32(value));
    return vget_lane_f32(vpadd_f32(pair, pair), 0);
}

std::complex<double> correlate_neon(
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

}  // namespace
#endif

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

    for (const PreparedFrequencyHypothesis& frequency : plan.frequency_hypotheses) {
        for (std::size_t timing_offset : plan.timing_offsets) {
            AcquisitionCandidate candidate;
            candidate.hypothesis.timing_offset = timing_offset;
            candidate.hypothesis.frequency_offset_hz = frequency.frequency_offset_hz;
            candidate.correlation = correlate_neon(
                received_iq.data() + timing_offset,
                frequency.matched_filter_weights_f32.data(),
                plan.preamble_length);
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
#else
    static_cast<void>(received_iq);
    static_cast<void>(plan);
    result.implementation = "unavailable";
    result.error_message = "NEON acquisition kernel is not compiled for this target";
#endif

    return result;
}

bool acquisition_neon_kernel_compiled() {
    return SATCOMFEC_ACQUISITION_HAS_NEON != 0;
}

}  // namespace satcomfec::acquisition
