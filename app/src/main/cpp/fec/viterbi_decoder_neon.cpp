#include "viterbi_decoder_neon.h"

#include "convolutional_codec.h"

namespace satcomfec {

bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_neon(soft_in, tables)) {
        return false;
    }
    return viterbi_decode_from_metrics(tables, hard_out);
}

}  // namespace satcomfec
