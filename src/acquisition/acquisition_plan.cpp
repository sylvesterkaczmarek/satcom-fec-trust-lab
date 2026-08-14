#include "acquisition/acquisition_plan.h"

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

bool timing_offsets_are_contiguous(
    const std::vector<std::size_t>& timing_offsets) {
    const std::size_t first = timing_offsets.front();
    for (std::size_t index = 1; index < timing_offsets.size(); ++index) {
        if (first > std::numeric_limits<std::size_t>::max() - index ||
            timing_offsets[index] != first + index) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool prepare_acquisition_plan(
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

    if (config.frequency_offsets_hz.size() >
        std::numeric_limits<std::size_t>::max() / preamble.size()) {
        error_message = "acquisition weight table size overflows size_t";
        return false;
    }
    const std::size_t flattened_weight_count =
        config.frequency_offsets_hz.size() * preamble.size();

    plan.sample_rate_hz = config.sample_rate_hz;
    plan.preamble_length = preamble.size();
    plan.timing_offsets = config.timing_offsets;
    plan.contiguous_timing_start = config.timing_offsets.front();
    plan.timing_offsets_contiguous =
        timing_offsets_are_contiguous(config.timing_offsets);
    plan.frequency_hypotheses.reserve(config.frequency_offsets_hz.size());
    plan.matched_filter_weights_real_f32.reserve(flattened_weight_count);
    plan.matched_filter_weights_imag_f32.reserve(flattened_weight_count);

    for (double frequency_offset_hz : config.frequency_offsets_hz) {
        PreparedFrequencyHypothesis prepared;
        prepared.frequency_offset_hz = frequency_offset_hz;
        prepared.matched_filter_weights.reserve(preamble.size());
        prepared.matched_filter_weights_f32.reserve(preamble.size());

        const double phase_step = -2.0 * kPi * frequency_offset_hz / config.sample_rate_hz;
        for (std::size_t sample_index = 0; sample_index < preamble.size(); ++sample_index) {
            const std::complex<double> preamble_sample(
                preamble[sample_index].real(), preamble[sample_index].imag());
            const std::complex<double> cfo_correction = std::polar(
                1.0, phase_step * static_cast<double>(sample_index));
            const std::complex<double> weight =
                std::conj(preamble_sample) * cfo_correction;
            prepared.matched_filter_weights.push_back(weight);
            prepared.matched_filter_weights_f32.emplace_back(
                static_cast<float>(weight.real()), static_cast<float>(weight.imag()));
            plan.matched_filter_weights_real_f32.push_back(
                static_cast<float>(weight.real()));
            plan.matched_filter_weights_imag_f32.push_back(
                static_cast<float>(weight.imag()));
        }
        plan.frequency_hypotheses.push_back(std::move(prepared));
    }

    return true;
}

}  // namespace satcomfec::acquisition
