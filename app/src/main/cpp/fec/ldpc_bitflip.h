#ifndef SATCOMFEC_LDPC_BITFLIP_H
#define SATCOMFEC_LDPC_BITFLIP_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

bool ldpc_decode_reference(const SoftBitBuffer& soft_in,
                           std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_LDPC_BITFLIP_H
