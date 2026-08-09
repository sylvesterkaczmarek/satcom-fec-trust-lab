#include "acquisition/acquisition_reference.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace satcomfec::acquisition {
namespace {

bool finite_iq(const ComplexF& sample) {
    return std::isfinite(sample.real()) && std::isfinite(sample.imag());
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

AcquisitionResult run_reference_acquisition_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;
    result.implementation = "reference";

    for (const PreparedFrequencyHypothesis& frequency : plan.frequency_hypotheses) {
        for (std::size_t timing_offset : plan.timing_offsets) {
            std::complex<double> correlation(0.0, 0.0);
            for (std::size_t sample_index = 0; sample_index < plan.preamble_length;
                 ++sample_index) {
                const ComplexF& received = received_iq[timing_offset + sample_index];
                correlation += std::complex<double>(received.real(), received.imag()) *
                               frequency.matched_filter_weights[sample_index];
            }

            AcquisitionCandidate candidate;
            candidate.hypothesis.timing_offset = timing_offset;
            candidate.hypothesis.frequency_offset_hz = frequency.frequency_offset_hz;
            candidate.correlation = correlation;
            candidate.score = std::norm(correlation);
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

AcquisitionResult run_reference_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;
    result.implementation = "reference";

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
        if (frequency.matched_filter_weights.size() != plan.preamble_length) {
            result.error_message = "acquisition plan has an invalid matched-filter table";
            return result;
        }
    }
    for (std::size_t timing_offset : plan.timing_offsets) {
        if (timing_offset > received_iq.size() - plan.preamble_length) {
            result.error_message = "a timing hypothesis extends beyond the received IQ window";
            return result;
        }
    }

    return run_reference_acquisition_steady_state(received_iq, plan);
}

}  // namespace satcomfec::acquisition
