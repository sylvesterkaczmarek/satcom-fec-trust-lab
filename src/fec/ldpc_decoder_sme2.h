#ifndef SATCOMFEC_LDPC_DECODER_SME2_H
#define SATCOMFEC_LDPC_DECODER_SME2_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/*
 * Simplified implementation.
 * This path currently uses the shared bit-flip reference decoder with no
 * SME2-specific kernel.
 */
bool ldpc_decode_sme2(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_LDPC_DECODER_SME2_H
