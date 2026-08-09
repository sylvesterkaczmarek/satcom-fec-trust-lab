#include "acquisition/acquisition_reference.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <set>
#include <utility>

namespace satcomfec::acquisition {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

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

bool prepare_reference_acquisition(
    const AcquisitionConfig& config,
    const std::vector<ComplexF>& preamble,
    AcquisitionPlan& plan,
    std::string& error_message) {
    plan = {};
    error_message.clear();

    if (!std::isfinite(config.sample_rate_hz) || config.sample_rate_hz <= 0.0) {
        error_message = "sample_rate_hz must be finite and greater than zero";
        return false;
    }
    if (preamble.empty()) {
        error_message = "preamble must contain at least one complex sample";
        return false;
    }
    if (config.timing_offsets.empty()) {
        error_message = "at least one timing hypothesis is required";
        return false;
    }
    if (config.frequency_offsets_hz.empty()) {
        error_message = "at least one frequency-offset hypothesis is required";
        return false;
    }

    double preamble_energy = 0.0;
    for (const ComplexF& sample : preamble) {
        if (!finite_iq(sample)) {
            error_message = "preamble contains a non-finite sample";
            return false;
        }
        preamble_energy += std::norm(std::complex<double>(sample.real(), sample.imag()));
    }
    if (preamble_energy <= std::numeric_limits<double>::min()) {
        error_message = "preamble energy must be greater than zero";
        return false;
    }

    std::set<std::size_t> unique_timings;
    for (std::size_t timing_offset : config.timing_offsets) {
        if (!unique_timings.insert(timing_offset).second) {
            error_message = "timing hypotheses must not contain duplicates";
            return false;
        }
    }

    std::set<double> unique_frequencies;
    for (double frequency_offset_hz : config.frequency_offsets_hz) {
        if (!std::isfinite(frequency_offset_hz)) {
            error_message = "frequency-offset hypotheses must be finite";
            return false;
        }
        if (!unique_frequencies.insert(frequency_offset_hz).second) {
            error_message = "frequency-offset hypotheses must not contain duplicates";
            return false;
        }
    }

    plan.sample_rate_hz = config.sample_rate_hz;
    plan.preamble_length = preamble.size();
    plan.timing_offsets = config.timing_offsets;
    plan.frequency_hypotheses.reserve(config.frequency_offsets_hz.size());

    for (double frequency_offset_hz : config.frequency_offsets_hz) {
        PreparedFrequencyHypothesis prepared;
        prepared.frequency_offset_hz = frequency_offset_hz;
        prepared.matched_filter_weights.reserve(preamble.size());

        const double phase_step = -2.0 * kPi * frequency_offset_hz / config.sample_rate_hz;
        for (std::size_t sample_index = 0; sample_index < preamble.size(); ++sample_index) {
            const std::complex<double> preamble_sample(
                preamble[sample_index].real(), preamble[sample_index].imag());
            const std::complex<double> cfo_correction = std::polar(
                1.0, phase_step * static_cast<double>(sample_index));
            prepared.matched_filter_weights.push_back(
                std::conj(preamble_sample) * cfo_correction);
        }
        plan.frequency_hypotheses.push_back(std::move(prepared));
    }

    return true;
}

AcquisitionResult run_reference_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan) {
    AcquisitionResult result;

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

}  // namespace satcomfec::acquisition
