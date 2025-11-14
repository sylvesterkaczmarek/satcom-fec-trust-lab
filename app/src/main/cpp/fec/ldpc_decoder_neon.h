#ifndef SATCOMFEC_LDPC_DECODER_NEON_H
#define SATCOMFEC_LDPC_DECODER_NEON_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/**
 * Stub LDPC decoder using a NEON baseline (later).
 *
 * For now this mirrors the SME2 version for A/B plumbing.
 */
bool ldpc_decode_neon(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_LDPC_DECODER_NEON_H
