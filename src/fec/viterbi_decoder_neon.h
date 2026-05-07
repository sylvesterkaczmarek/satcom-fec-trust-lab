#ifndef SATCOMFEC_VITERBI_DECODER_NEON_H
#define SATCOMFEC_VITERBI_DECODER_NEON_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/*
 * Partial NEON implementation.
 * Branch-metric preparation uses NEON intrinsics when __ARM_NEON is available;
 * add-compare-select and traceback stay in the shared scalar reference core.
 */
bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_NEON_H
