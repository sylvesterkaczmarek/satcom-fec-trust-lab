#ifndef SATCOMFEC_REPLAY_ACQUISITION_H
#define SATCOMFEC_REPLAY_ACQUISITION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "acquisition/acquisition.h"
#include "dsp/front_end_dsp.h"

namespace satcomfec {

struct ReplayAcquisitionGroundTruth {
    bool available = false;
    std::string scenario;
    bool signal_present = false;
    bool has_timing_offset = false;
    std::size_t timing_offset = 0;
    bool has_cfo_hz = false;
    double cfo_hz = 0.0;
};

struct ReplayAcquisitionConfig {
    double sample_rate_hz = 48000.0;
    acquisition::AcquisitionImplementation implementation =
        acquisition::AcquisitionImplementation::kReference;
    std::vector<double> cfo_hypotheses_hz {
        -500.0, -250.0, 0.0, 250.0, 500.0,
    };
    double minimum_normalized_peak = 0.10;
    double minimum_peak_separation = 0.05;
};

struct ReplayAcquisitionDiagnostics {
    std::string requested_implementation = "reference";
    std::string selected_implementation = "unavailable";
    bool search_completed = false;
    bool accepted = false;
    std::size_t sample_count = 0;
    std::size_t preamble_length = 0;
    std::size_t timing_hypothesis_count = 0;
    std::size_t cfo_hypothesis_count = 0;
    std::size_t evaluated_candidate_count = 0;
    bool detected_candidate_valid = false;
    std::size_t detected_timing_offset = 0;
    double detected_cfo_hz = 0.0;
    double correlation_score = 0.0;
    double second_best_score = 0.0;
    double peak_ratio = 0.0;
    double normalized_peak = 0.0;
    double second_best_normalized_peak = 0.0;
    double normalized_peak_separation = 0.0;
    double confidence = 0.0;
    double minimum_normalized_peak = 0.0;
    double minimum_peak_separation = 0.0;
    ReplayAcquisitionGroundTruth ground_truth;
    bool has_timing_error = false;
    std::int64_t timing_error_samples = 0;
    bool has_cfo_hypothesis_error = false;
    double cfo_hypothesis_error_hz = 0.0;
};

bool acquire_and_align_replay_frame(
    const std::vector<ComplexF>& normalized_iq,
    const std::vector<ComplexF>& preamble,
    std::size_t frame_sample_count,
    const ReplayAcquisitionConfig& config,
    const ReplayAcquisitionGroundTruth& ground_truth,
    std::vector<ComplexF>& aligned_frame_iq,
    ReplayAcquisitionDiagnostics& diagnostics,
    std::string& error_message);

}  // namespace satcomfec

#endif  // SATCOMFEC_REPLAY_ACQUISITION_H
