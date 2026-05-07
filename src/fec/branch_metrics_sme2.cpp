#include "branch_metrics_sme2.h"

#include <algorithm>

#include "../util/logging.h"

#if defined(__ARM_FEATURE_SME2)
#include <arm_sve.h>
#if defined(__has_include)
#if __has_include(<arm_sme.h>)
#include <arm_sme.h>
#endif
#else
#include <arm_sme.h>
#endif
#endif

namespace satcomfec {

#if defined(__ARM_FEATURE_SME2)
namespace {

void resize_metric_tables_sme2(size_t symbol_count, BranchMetricTables& tables) {
    tables.symbol_count = symbol_count;
    for (auto& metric_vector : tables.metric_by_symbol_type) {
        metric_vector.assign(symbol_count, 0);
    }
}

__arm_locally_streaming bool prepare_branch_metrics_sme2_kernel(
    const SoftBitBuffer& soft_in,
    BranchMetricTables& tables) {
    if (soft_in.empty() || (soft_in.size() % 2) != 0) {
        log_error("prepare_branch_metrics_sme2: expected an even number of soft bits");
        return false;
    }

    const size_t symbol_count = soft_in.size() / 2;
    resize_metric_tables_sme2(symbol_count, tables);

    size_t symbol_index = 0;
    while (symbol_index < symbol_count) {
        const size_t chunk =
            std::min(static_cast<size_t>(svcnth()), symbol_count - symbol_index);
        const svbool_t pg8 = svwhilelt_b8(static_cast<uint64_t>(0),
                                          static_cast<uint64_t>(chunk));
        const svbool_t pg16 = svwhilelt_b16(static_cast<uint64_t>(0),
                                            static_cast<uint64_t>(chunk));
        const auto* input =
            reinterpret_cast<const int8_t*>(soft_in.data() + (2 * symbol_index));

        const svint8x2_t pair = svld2_s8(pg8, input);
        const svint16_t llr0 = svunpklo_s16(svget2_s8(pair, 0));
        const svint16_t llr1 = svunpklo_s16(svget2_s8(pair, 1));

        const svint16_t sum = svadd_s16_x(pg16, llr0, llr1);
        const svint16_t neg_sum = svsub_s16_x(pg16, svdup_s16(0), sum);
        const svint16_t llr1_minus_llr0 = svsub_s16_x(pg16, llr1, llr0);
        const svint16_t llr0_minus_llr1 = svsub_s16_x(pg16, llr0, llr1);

        svst1_s16(pg16,
                  tables.metric_by_symbol_type[0].data() + symbol_index,
                  neg_sum);
        svst1_s16(pg16,
                  tables.metric_by_symbol_type[1].data() + symbol_index,
                  llr1_minus_llr0);
        svst1_s16(pg16,
                  tables.metric_by_symbol_type[2].data() + symbol_index,
                  llr0_minus_llr1);
        svst1_s16(pg16,
                  tables.metric_by_symbol_type[3].data() + symbol_index,
                  sum);

        symbol_index += chunk;
    }

    return true;
}

}  // namespace
#endif

bool prepare_branch_metrics_sme2(const SoftBitBuffer& soft_in,
                                 BranchMetricTables& tables) {
#if defined(__ARM_FEATURE_SME2)
    return prepare_branch_metrics_sme2_kernel(soft_in, tables);
#else
    return prepare_branch_metrics_reference(soft_in, tables);
#endif
}

bool branch_metrics_sme2_kernel_compiled() {
#if defined(__ARM_FEATURE_SME2)
    return true;
#else
    return false;
#endif
}

const char* branch_metrics_sme2_selected_implementation() {
#if defined(__ARM_FEATURE_SME2)
    return "sme2";
#else
    return "fallback";
#endif
}

}  // namespace satcomfec
