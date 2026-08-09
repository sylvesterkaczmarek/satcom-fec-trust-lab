#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_H

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace satcomfec::acquisition {

using CorrelationScore = double;

struct AcquisitionHypothesis {
    std::size_t timing_offset = 0;
    double frequency_offset_hz = 0.0;
};

struct AcquisitionCandidate {
    AcquisitionHypothesis hypothesis;
    std::complex<double> correlation{0.0, 0.0};
    CorrelationScore score = 0.0;
    bool valid = false;
};

struct AcquisitionConfig {
    double sample_rate_hz = 0.0;
    std::vector<std::size_t> timing_offsets;
    std::vector<double> frequency_offsets_hz;
};

struct AcquisitionResult {
    bool ok = false;
    std::size_t evaluated_candidate_count = 0;
    AcquisitionCandidate best;
    AcquisitionCandidate second_best;
    double peak_ratio = 0.0;
    double normalized_peak_separation = 0.0;
    std::string implementation = "reference";
    std::string error_message;
};

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_H
