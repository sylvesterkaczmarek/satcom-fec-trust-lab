#ifndef SATCOMFEC_VITERBI_DECODER_SME2_H
#define SATCOMFEC_VITERBI_DECODER_SME2_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/**
 * Stub Viterbi decoder for SME2 path.
 *
 * For now it just thresholds soft bits.
 */
bool viterbi_decode_sme2(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_SME2_H
