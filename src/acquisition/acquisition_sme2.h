#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_SME2_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_SME2_H

#include "acquisition/acquisition_reference.h"

#include <cstddef>
#include <string>
#include <vector>

namespace satcomfec::acquisition {

struct Sme2AcquisitionWorkspace {
    std::size_t timing_count = 0;
    std::size_t frequency_count = 0;
    std::size_t preamble_length = 0;
    std::vector<float> received_real_by_sample_and_timing;
    std::vector<float> received_imag_by_sample_and_timing;
    std::vector<float> correlation_real_by_frequency_and_timing;
    std::vector<float> correlation_imag_by_frequency_and_timing;
};

bool prepare_sme2_acquisition_workspace(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace,
    std::string& error_message);

AcquisitionResult run_sme2_acquisition_prepared(
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace);

// Requires successful workspace preparation and one checked SME2 execution.
AcquisitionResult run_sme2_acquisition_steady_state(
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace);

AcquisitionResult run_sme2_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan);

bool acquisition_sme2_kernel_compiled();
bool acquisition_sme2_runtime_supported();
std::size_t acquisition_sme2_streaming_lanes_f32();
const char* acquisition_sme2_mechanism();

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_SME2_H
