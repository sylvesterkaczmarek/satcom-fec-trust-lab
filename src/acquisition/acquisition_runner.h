#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_RUNNER_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_RUNNER_H

#include "acquisition/acquisition_neon.h"

namespace satcomfec::acquisition {

const char* acquisition_implementation_label(AcquisitionImplementation implementation);

AcquisitionResult run_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    AcquisitionImplementation implementation);

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_RUNNER_H
