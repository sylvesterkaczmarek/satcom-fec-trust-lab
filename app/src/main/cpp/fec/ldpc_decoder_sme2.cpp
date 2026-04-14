#include "ldpc_decoder_sme2.h"

#include "ldpc_bitflip.h"

namespace satcomfec {

bool ldpc_decode_sme2(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out) {
    return ldpc_decode_reference(soft_in, hard_out);
}

}  // namespace satcomfec
