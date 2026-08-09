#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_NEON_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_NEON_H

#include "acquisition/acquisition_reference.h"

namespace satcomfec::acquisition {

AcquisitionResult run_neon_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan);

bool acquisition_neon_kernel_compiled();

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_NEON_H
