#ifndef SATCOMFEC_VITERBI_DECODER_NEON_H
#define SATCOMFEC_VITERBI_DECODER_NEON_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/**
 * Stub Viterbi decoder for NEON baseline.
 */
bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_NEON_H
