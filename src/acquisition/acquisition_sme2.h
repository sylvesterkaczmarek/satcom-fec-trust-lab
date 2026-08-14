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
    std::size_t contiguous_timing_start = 0;
    bool packed_input_required = false;
    std::vector<ComplexF> received_by_sample_and_timing;
    std::vector<float> correlation_real_by_frequency_and_timing;
    std::vector<float> correlation_imag_by_frequency_and_timing;
};

bool prepare_sme2_acquisition_workspace(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace,
    std::string& error_message);

// Requires a previously validated input/plan pair and a correctly sized
// workspace. Packs only non-contiguous timing grids; contiguous grids are
// consumed directly from the supplied interleaved IQ capture.
void prepare_sme2_acquisition_capture_steady_state(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace);

// Validates runtime support and dimensions, but does not repack the capture.
// For a non-contiguous timing grid, workspace must have been prepared from
// this exact received_iq value.
AcquisitionResult run_sme2_acquisition_prepared(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    Sme2AcquisitionWorkspace& workspace);

// Requires successful workspace preparation and a prior runtime-support check.
AcquisitionResult run_sme2_acquisition_steady_state(
    const std::vector<ComplexF>& received_iq,
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
