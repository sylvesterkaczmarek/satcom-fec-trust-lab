#include "viterbi_decoder_neon.h"

#include "../util/logging.h"
#include "convolutional_codec.h"

#if defined(SATCOMFEC_FEC_NEON_COMPILED) && \
    (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define SATCOMFEC_FEC_HAS_NEON 1
#else
#define SATCOMFEC_FEC_HAS_NEON 0
#endif

namespace satcomfec {
namespace {

#if SATCOMFEC_FEC_HAS_NEON
void resize_metric_tables(std::size_t symbol_count, BranchMetricTables& tables) {
    tables.symbol_count = symbol_count;
    for (auto& metric_vector : tables.metric_by_symbol_type) {
        metric_vector.assign(symbol_count, 0);
    }
}
#endif

}  // namespace

const ImplementationInfo& viterbi_neon_implementation_info() {
    static const ImplementationInfo compiled_info {
        "viterbi-neon",
        ImplementationClass::kPartial,
        "Partial NEON implementation: Arm NEON prepares branch metrics; "
        "add-compare-select and traceback use the shared scalar reference core.",
    };
    static const ImplementationInfo fallback_info {
        "viterbi-neon",
        ImplementationClass::kFallback,
        "NEON branch-metric preparation was not compiled for this target; "
        "branch metrics, add-compare-select, and traceback execute as scalar code.",
    };
    return branch_metrics_neon_kernel_compiled() ? compiled_info : fallback_info;
}

bool prepare_branch_metrics_neon(const SoftBitBuffer& soft_in,
                                 BranchMetricTables& tables) {
#if SATCOMFEC_FEC_HAS_NEON
    if (soft_in.empty() || (soft_in.size() % 2) != 0) {
        log_error(
            "prepare_branch_metrics_neon: expected an even number of soft "
            "decisions");
        return false;
    }

    const std::size_t symbol_count = soft_in.size() / 2;
    resize_metric_tables(symbol_count, tables);

    std::size_t symbol_index = 0;
    for (; symbol_index + 16 <= symbol_count; symbol_index += 16) {
        const auto* interleaved_ptr =
            reinterpret_cast<const int8_t*>(soft_in.data() + 2 * symbol_index);
        const int8x16x2_t pair = vld2q_s8(interleaved_ptr);

        const int16x8_t soft0_lo = vmovl_s8(vget_low_s8(pair.val[0]));
        const int16x8_t soft0_hi = vmovl_s8(vget_high_s8(pair.val[0]));
        const int16x8_t soft1_lo = vmovl_s8(vget_low_s8(pair.val[1]));
        const int16x8_t soft1_hi = vmovl_s8(vget_high_s8(pair.val[1]));

        const int16x8_t sum_lo = vaddq_s16(soft0_lo, soft1_lo);
        const int16x8_t sum_hi = vaddq_s16(soft0_hi, soft1_hi);
        const int16x8_t diff_lo = vsubq_s16(soft0_lo, soft1_lo);
        const int16x8_t diff_hi = vsubq_s16(soft0_hi, soft1_hi);
        const int16x8_t inverse_diff_lo = vsubq_s16(soft1_lo, soft0_lo);
        const int16x8_t inverse_diff_hi = vsubq_s16(soft1_hi, soft0_hi);

        vst1q_s16(
            tables.metric_by_symbol_type[0].data() + symbol_index,
            vnegq_s16(sum_lo));
        vst1q_s16(
            tables.metric_by_symbol_type[0].data() + symbol_index + 8,
            vnegq_s16(sum_hi));
        vst1q_s16(
            tables.metric_by_symbol_type[1].data() + symbol_index,
            inverse_diff_lo);
        vst1q_s16(
            tables.metric_by_symbol_type[1].data() + symbol_index + 8,
            inverse_diff_hi);
        vst1q_s16(
            tables.metric_by_symbol_type[2].data() + symbol_index,
            diff_lo);
        vst1q_s16(
            tables.metric_by_symbol_type[2].data() + symbol_index + 8,
            diff_hi);
        vst1q_s16(
            tables.metric_by_symbol_type[3].data() + symbol_index,
            sum_lo);
        vst1q_s16(
            tables.metric_by_symbol_type[3].data() + symbol_index + 8,
            sum_hi);
    }

    for (; symbol_index < symbol_count; ++symbol_index) {
        const int soft0 = static_cast<int>(soft_in[2 * symbol_index]);
        const int soft1 = static_cast<int>(soft_in[2 * symbol_index + 1]);
        tables.metric_by_symbol_type[0][symbol_index] =
            static_cast<int16_t>(-(soft0 + soft1));
        tables.metric_by_symbol_type[1][symbol_index] =
            static_cast<int16_t>(soft1 - soft0);
        tables.metric_by_symbol_type[2][symbol_index] =
            static_cast<int16_t>(soft0 - soft1);
        tables.metric_by_symbol_type[3][symbol_index] =
            static_cast<int16_t>(soft0 + soft1);
    }
    return true;
#else
    return prepare_branch_metrics_reference(soft_in, tables);
#endif
}

bool branch_metrics_neon_kernel_compiled() {
    return SATCOMFEC_FEC_HAS_NEON != 0;
}

const char* branch_metrics_neon_selected_implementation() {
    return branch_metrics_neon_kernel_compiled() ? "neon" : "fallback";
}

bool viterbi_decode_neon(const SoftBitBuffer& soft_in,
                         std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_neon(soft_in, tables)) {
        return false;
    }
    return viterbi_decode_from_metrics(tables, hard_out);
}

}  // namespace satcomfec
