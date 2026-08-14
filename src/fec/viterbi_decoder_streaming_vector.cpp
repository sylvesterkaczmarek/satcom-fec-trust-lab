#include "viterbi_decoder_streaming_vector.h"

#include "branch_metrics_streaming_vector.h"
#include "convolutional_codec.h"

namespace satcomfec {

const ImplementationInfo& viterbi_streaming_vector_implementation_info() {
    static const ImplementationInfo compiled_info {
        "viterbi-streaming-vector",
        ImplementationClass::kPartial,
        "Historical streaming-vector experiment: locally streaming SVE-style "
        "operations prepare branch metrics; ZA, Viterbi trellis recurrence, "
        "and traceback are not accelerated.",
    };
    static const ImplementationInfo fallback_info {
        "viterbi-streaming-vector",
        ImplementationClass::kFallback,
        "The historical streaming-vector branch-metric kernel was not compiled "
        "for this target; branch metrics, Viterbi trellis recurrence, and "
        "traceback execute as scalar code.",
    };
    return branch_metrics_streaming_vector_kernel_compiled()
               ? compiled_info
               : fallback_info;
}

bool viterbi_decode_streaming_vector(const SoftBitBuffer& soft_in,
                                     std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_streaming_vector(soft_in, tables)) {
        return false;
    }
    return viterbi_decode_from_metrics(tables, hard_out);
}

}  // namespace satcomfec
