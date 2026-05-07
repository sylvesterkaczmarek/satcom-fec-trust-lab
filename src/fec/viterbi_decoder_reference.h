#ifndef SATCOMFEC_VITERBI_DECODER_REFERENCE_H
#define SATCOMFEC_VITERBI_DECODER_REFERENCE_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

bool viterbi_decode_reference_path(const SoftBitBuffer& soft_in,
                                   std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_REFERENCE_H
