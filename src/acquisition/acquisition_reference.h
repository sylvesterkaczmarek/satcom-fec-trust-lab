#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H

#include "acquisition/acquisition.h"
#include "util/iq_reader.h"

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace satcomfec::acquisition {

struct PreparedFrequencyHypothesis {
    double frequency_offset_hz = 0.0;
    std::vector<std::complex<double>> matched_filter_weights;
};

struct AcquisitionPlan {
    double sample_rate_hz = 0.0;
    std::size_t preamble_length = 0;
    std::vector<std::size_t> timing_offsets;
    std::vector<PreparedFrequencyHypothesis> frequency_hypotheses;
};

bool prepare_reference_acquisition(
    const AcquisitionConfig& config,
    const std::vector<ComplexF>& preamble,
    AcquisitionPlan& plan,
    std::string& error_message);

AcquisitionResult run_reference_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan);

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H
