#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H

#include "acquisition/acquisition_plan.h"

#include <vector>

namespace satcomfec::acquisition {

AcquisitionResult run_reference_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan);

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_REFERENCE_H
