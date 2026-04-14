#ifndef SATCOMFEC_LDPC_DECODER_NEON_H
#define SATCOMFEC_LDPC_DECODER_NEON_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/*
 * Simplified implementation.
 * This path currently uses the shared bit-flip reference decoder with no
 * NEON-specific intrinsics.
 */
bool ldpc_decode_neon(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_LDPC_DECODER_NEON_H
