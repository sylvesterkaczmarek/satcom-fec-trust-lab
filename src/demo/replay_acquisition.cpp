#include "demo/replay_acquisition.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>

#include "acquisition/acquisition_plan.h"
#include "acquisition/acquisition_runner.h"

namespace satcomfec {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double clamp_unit(double value) {
    return std::max(0.0, std::min(value, 1.0));
}

double signal_energy(const std::vector<ComplexF>& samples) {
    double energy = 0.0;
    for (const ComplexF& sample : samples) {
        energy += std::norm(std::complex<double>(sample.real(), sample.imag()));
    }
    return energy;
}

double normalized_candidate_score(
    const std::vector<ComplexF>& received_iq,
    const std::vector<ComplexF>& preamble,
    const acquisition::AcquisitionCandidate& candidate,
    double preamble_energy) {
    if (!candidate.valid || candidate.hypothesis.timing_offset >
                                received_iq.size() - preamble.size()) {
        return 0.0;
    }

    double window_energy = 0.0;
    const std::size_t start = candidate.hypothesis.timing_offset;
    for (std::size_t index = 0; index < preamble.size(); ++index) {
        const ComplexF& sample = received_iq[start + index];
        window_energy += std::norm(
            std::complex<double>(sample.real(), sample.imag()));
    }

    const double denominator = preamble_energy * window_energy;
    if (denominator <= std::numeric_limits<double>::min()) {
        return 0.0;
    }
    return clamp_unit(candidate.score / denominator);
}

void attach_ground_truth(
    const ReplayAcquisitionGroundTruth& ground_truth,
    ReplayAcquisitionDiagnostics& diagnostics) {
    diagnostics.ground_truth = ground_truth;
    if (!diagnostics.detected_candidate_valid || !ground_truth.available ||
        !ground_truth.signal_present) {
        return;
    }
    if (ground_truth.has_timing_offset) {
        diagnostics.has_timing_error = true;
        diagnostics.timing_error_samples =
            static_cast<std::int64_t>(diagnostics.detected_timing_offset) -
            static_cast<std::int64_t>(ground_truth.timing_offset);
    }
    if (ground_truth.has_cfo_hz) {
        diagnostics.has_cfo_hypothesis_error = true;
        diagnostics.cfo_hypothesis_error_hz =
            diagnostics.detected_cfo_hz - ground_truth.cfo_hz;
    }
}

}  // namespace

bool acquire_and_align_replay_frame(
    const std::vector<ComplexF>& normalized_iq,
    const std::vector<ComplexF>& preamble,
    std::size_t frame_sample_count,
    const ReplayAcquisitionConfig& config,
    const ReplayAcquisitionGroundTruth& ground_truth,
    std::vector<ComplexF>& aligned_frame_iq,
    ReplayAcquisitionDiagnostics& diagnostics,
    std::string& error_message) {
    aligned_frame_iq.clear();
    diagnostics = {};
    error_message.clear();
    diagnostics.requested_implementation =
        acquisition::acquisition_implementation_label(config.implementation);
    diagnostics.sample_count = normalized_iq.size();
    diagnostics.preamble_length = preamble.size();
    diagnostics.cfo_hypothesis_count = config.cfo_hypotheses_hz.size();
    diagnostics.minimum_normalized_peak = config.minimum_normalized_peak;
    diagnostics.minimum_peak_separation = config.minimum_peak_separation;
    diagnostics.ground_truth = ground_truth;

    if (!std::isfinite(config.sample_rate_hz) || config.sample_rate_hz <= 0.0) {
        error_message = "Acquisition sample rate must be finite and positive";
        return false;
    }
    if (preamble.empty()) {
        error_message = "Acquisition preamble is empty";
        return false;
    }
    if (frame_sample_count == 0) {
        error_message = "Aligned replay frame length is zero";
        return false;
    }
    if (config.cfo_hypotheses_hz.empty()) {
        error_message = "Acquisition CFO hypothesis grid is empty";
        return false;
    }
    if (config.minimum_normalized_peak < 0.0 ||
        config.minimum_normalized_peak > 1.0 ||
        config.minimum_peak_separation < 0.0 ||
        config.minimum_peak_separation > 1.0) {
        error_message = "Acquisition thresholds must be in [0, 1]";
        return false;
    }
    if (normalized_iq.size() < preamble.size() + frame_sample_count) {
        error_message = "IQ window is too short for the preamble and replay frame";
        return false;
    }

    const std::size_t maximum_timing_offset =
        normalized_iq.size() - preamble.size() - frame_sample_count;
    std::vector<std::size_t> timing_offsets(maximum_timing_offset + 1);
    std::iota(timing_offsets.begin(), timing_offsets.end(), std::size_t{0});
    diagnostics.timing_hypothesis_count = timing_offsets.size();

    acquisition::AcquisitionPlan plan;
    std::string plan_error;
    if (!acquisition::prepare_acquisition_plan(
            acquisition::AcquisitionConfig {
                config.sample_rate_hz,
                std::move(timing_offsets),
                config.cfo_hypotheses_hz,
            },
            preamble,
            plan,
            plan_error)) {
        error_message = "Acquisition plan failed: " + plan_error;
        return false;
    }

    const acquisition::AcquisitionResult acquisition_result =
        acquisition::run_acquisition(
            normalized_iq, plan, config.implementation);
    diagnostics.selected_implementation = acquisition_result.implementation;
    diagnostics.evaluated_candidate_count =
        acquisition_result.evaluated_candidate_count;
    diagnostics.detected_candidate_valid = acquisition_result.best.valid;
    if (acquisition_result.best.valid) {
        diagnostics.detected_timing_offset =
            acquisition_result.best.hypothesis.timing_offset;
        diagnostics.detected_cfo_hz =
            acquisition_result.best.hypothesis.frequency_offset_hz;
        diagnostics.correlation_score = acquisition_result.best.score;
    }
    if (acquisition_result.second_best.valid) {
        diagnostics.second_best_score = acquisition_result.second_best.score;
    }
    diagnostics.peak_ratio = acquisition_result.peak_ratio;
    diagnostics.normalized_peak_separation = clamp_unit(
        acquisition_result.normalized_peak_separation);

    if (!acquisition_result.ok) {
        attach_ground_truth(ground_truth, diagnostics);
        error_message = "Acquisition search failed: " +
                        acquisition_result.error_message;
        return false;
    }

    diagnostics.search_completed = true;
    const double preamble_energy = signal_energy(preamble);
    diagnostics.normalized_peak = normalized_candidate_score(
        normalized_iq, preamble, acquisition_result.best, preamble_energy);
    diagnostics.second_best_normalized_peak = normalized_candidate_score(
        normalized_iq, preamble, acquisition_result.second_best, preamble_energy);
    diagnostics.confidence = clamp_unit(
        std::sqrt(diagnostics.normalized_peak) *
        diagnostics.normalized_peak_separation);
    diagnostics.accepted =
        diagnostics.normalized_peak >= config.minimum_normalized_peak &&
        diagnostics.normalized_peak_separation >=
            config.minimum_peak_separation;
    attach_ground_truth(ground_truth, diagnostics);

    if (!diagnostics.accepted) {
        error_message =
            "Acquisition rejected: peak strength or separation below threshold";
        return false;
    }

    const std::size_t frame_start =
        diagnostics.detected_timing_offset + preamble.size();
    if (frame_start > normalized_iq.size() - frame_sample_count) {
        error_message = "Acquisition candidate does not leave a complete replay frame";
        diagnostics.accepted = false;
        return false;
    }

    const double carrier_phase = std::arg(acquisition_result.best.correlation);
    aligned_frame_iq.reserve(frame_sample_count);
    for (std::size_t frame_index = 0; frame_index < frame_sample_count;
         ++frame_index) {
        const double sample_index =
            static_cast<double>(preamble.size() + frame_index);
        const double phase = carrier_phase +
            2.0 * kPi * diagnostics.detected_cfo_hz * sample_index /
                config.sample_rate_hz;
        const std::complex<double> correction = std::polar(1.0, -phase);
        const ComplexF& sample = normalized_iq[frame_start + frame_index];
        const std::complex<double> corrected =
            std::complex<double>(sample.real(), sample.imag()) * correction;
        aligned_frame_iq.emplace_back(
            static_cast<float>(corrected.real()),
            static_cast<float>(corrected.imag()));
    }

    return true;
}

}  // namespace satcomfec
