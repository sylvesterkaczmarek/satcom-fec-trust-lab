#include "viterbi_decoder_sme2.h"

#include "convolutional_codec.h"

namespace satcomfec {

bool viterbi_decode_sme2(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_reference(soft_in, tables)) {
        return false;
    }
    return viterbi_decode_from_metrics(tables, hard_out);
}

}  // namespace satcomfec
