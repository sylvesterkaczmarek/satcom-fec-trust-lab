#ifndef SATCOMFEC_VITERBI_DECODER_STREAMING_VECTOR_H
#define SATCOMFEC_VITERBI_DECODER_STREAMING_VECTOR_H

#include <cstdint>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

bool viterbi_decode_streaming_vector(const SoftBitBuffer& soft_in,
                                     std::vector<uint8_t>& hard_out);

}  // namespace satcomfec

#endif  // SATCOMFEC_VITERBI_DECODER_STREAMING_VECTOR_H
