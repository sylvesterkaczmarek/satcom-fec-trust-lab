#ifndef SATCOMFEC_VITERBI_DECODER_NEON_H
#define SATCOMFEC_VITERBI_DECODER_NEON_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/*
 * Real implementation on Arm NEON targets.
 * The NEON-specific work is branch-metric preparation; the decode core and
 * traceback stay shared so both paths operate on the same state machine.
 */
bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_NEON_H
