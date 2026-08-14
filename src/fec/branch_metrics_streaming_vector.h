#ifndef SATCOMFEC_BRANCH_METRICS_STREAMING_VECTOR_H
#define SATCOMFEC_BRANCH_METRICS_STREAMING_VECTOR_H

#include "convolutional_codec.h"

namespace satcomfec {

bool prepare_branch_metrics_streaming_vector(const SoftBitBuffer& soft_in,
                                             BranchMetricTables& tables);
bool branch_metrics_streaming_vector_kernel_compiled();
const char* branch_metrics_streaming_vector_selected_implementation();

}  // namespace satcomfec

#endif  // SATCOMFEC_BRANCH_METRICS_STREAMING_VECTOR_H
