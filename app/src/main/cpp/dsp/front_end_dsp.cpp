#include "front_end_dsp.h"

#include "util/logging.h"

namespace satcomfec {

bool run_front_end(const std::vector<ComplexF>& iq_in,
                   std::vector<ComplexF>& iq_out,
                   const FrontEndConfig&) {
    iq_out = iq_in;
    log_info("run_front_end stub: passthrough");
    return true;
}

}  // namespace satcomfec
