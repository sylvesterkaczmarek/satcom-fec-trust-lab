#ifndef SATCOMFEC_VITERBI_DECODER_SME2_H
#define SATCOMFEC_VITERBI_DECODER_SME2_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/*
 * Simplified implementation.
 * The SME2 entrypoint exists so the replay and benchmark harnesses can compare
 * like-for-like decoder plumbing, but it currently uses the shared scalar
 * reference implementation rather than an SME2-specific kernel.
 */
bool viterbi_decode_sme2(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_SME2_H
