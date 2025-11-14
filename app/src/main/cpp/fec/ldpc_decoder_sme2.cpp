#include "ldpc_decoder_sme2.h"

#include "../util/logging.h"

namespace satcomfec {

bool ldpc_decode_sme2(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out) {
    hard_out.clear();
    hard_out.reserve(soft_in.size());

    for (auto s : soft_in) {
        hard_out.push_back(s >= 0 ? 1U : 0U);
    }

    log_info("ldpc_decode_sme2 stub: simple threshold decode");
    return true;
}

}  // namespace satcomfec
