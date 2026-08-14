#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_PLAN_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_PLAN_H

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
    std::vector<ComplexF> matched_filter_weights_f32;
};

struct AcquisitionPlan {
    double sample_rate_hz = 0.0;
    std::size_t preamble_length = 0;
    std::vector<std::size_t> timing_offsets;
    bool timing_offsets_contiguous = false;
    std::size_t contiguous_timing_start = 0;
    std::vector<PreparedFrequencyHypothesis> frequency_hypotheses;
    std::vector<float> matched_filter_weights_real_f32;
    std::vector<float> matched_filter_weights_imag_f32;
};

bool prepare_acquisition_plan(
    const AcquisitionConfig& config,
    const std::vector<ComplexF>& preamble,
    AcquisitionPlan& plan,
    std::string& error_message);

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_PLAN_H
