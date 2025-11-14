#include "viterbi_decoder_neon.h"

#include "../util/logging.h"

namespace satcomfec {

bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out) {
    hard_out.clear();
    hard_out.reserve(soft_in.size());

    for (auto s : soft_in) {
        hard_out.push_back(s >= 0 ? 1U : 0U);
    }

    log_info("viterbi_decode_neon stub: simple threshold decode");
    return true;
}

}  // namespace satcomfec
