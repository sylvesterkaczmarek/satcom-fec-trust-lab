#include "viterbi_decoder_reference.h"

#include "convolutional_codec.h"

namespace satcomfec {

bool viterbi_decode_reference_path(const SoftBitBuffer& soft_in,
                                   std::vector<uint8_t>& hard_out) {
    return viterbi_decode_reference(soft_in, hard_out);
}

}  // namespace satcomfec
