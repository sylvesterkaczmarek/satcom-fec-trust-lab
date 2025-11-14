#ifndef SATCOMFEC_LDPC_DECODER_SME2_H
#define SATCOMFEC_LDPC_DECODER_SME2_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/**
 * Stub LDPC decoder using SME2 optimised kernels (later).
 *
 * For now it just thresholds soft bits into hard bits.
 */
bool ldpc_decode_sme2(const SoftBitBuffer& soft_in,
                      std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_LDPC_DECODER_SME2_H
