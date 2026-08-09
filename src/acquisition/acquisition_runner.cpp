#include "acquisition/acquisition_runner.h"

namespace satcomfec::acquisition {

const char* acquisition_implementation_label(AcquisitionImplementation implementation) {
    switch (implementation) {
        case AcquisitionImplementation::kReference:
            return "reference";
        case AcquisitionImplementation::kNeon:
            return "neon";
    }
    return "unknown";
}

AcquisitionResult run_acquisition(
    const std::vector<ComplexF>& received_iq,
    const AcquisitionPlan& plan,
    AcquisitionImplementation implementation) {
    switch (implementation) {
        case AcquisitionImplementation::kReference:
            return run_reference_acquisition(received_iq, plan);
        case AcquisitionImplementation::kNeon:
            return run_neon_acquisition(received_iq, plan);
    }

    AcquisitionResult result;
    result.implementation = "unavailable";
    result.error_message = "unknown acquisition implementation";
    return result;
}

}  // namespace satcomfec::acquisition
