#ifndef SATCOMFEC_TRUST_SCORE_H
#define SATCOMFEC_TRUST_SCORE_H

#include "trust_features.h"

namespace satcomfec {

/**
 * Convert trust features into a scalar score in [0, 1].
 *
 * Stub implementation: clamps a simple heuristic.
 */
float compute_trust_score(const TrustFeatures& features);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_SCORE_H
