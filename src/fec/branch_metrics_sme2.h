#ifndef SATCOMFEC_BRANCH_METRICS_SME2_H
#define SATCOMFEC_BRANCH_METRICS_SME2_H

#include "convolutional_codec.h"

namespace satcomfec {

bool prepare_branch_metrics_sme2(const SoftBitBuffer& soft_in,
                                 BranchMetricTables& tables);
bool branch_metrics_sme2_kernel_compiled();
const char* branch_metrics_sme2_selected_implementation();

}  // namespace satcomfec

#endif  // SATCOMFEC_BRANCH_METRICS_SME2_H
