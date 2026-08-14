#include "viterbi_decoder_sme2.h"

#include "branch_metrics_sme2.h"
#include "convolutional_codec.h"

namespace satcomfec {

const ImplementationInfo& viterbi_sme2_implementation_info() {
    static const ImplementationInfo compiled_info {
        "viterbi-sme2",
        ImplementationClass::kPartial,
        "Partial SME2 implementation: SME/SME2 streaming mode prepares branch "
        "metrics; Viterbi trellis recurrence and traceback remain scalar.",
    };
    static const ImplementationInfo fallback_info {
        "viterbi-sme2",
        ImplementationClass::kFallback,
        "SME2 branch-metric preparation was not compiled for this target; "
        "branch metrics, Viterbi trellis recurrence, and traceback execute as "
        "scalar code.",
    };
    return branch_metrics_sme2_kernel_compiled() ? compiled_info : fallback_info;
}

bool viterbi_decode_sme2(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_sme2(soft_in, tables)) {
        return false;
    }
    return viterbi_decode_from_metrics(tables, hard_out);
}

}  // namespace satcomfec
