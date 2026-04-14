#ifndef SATCOMFEC_TRUST_SCORE_H
#define SATCOMFEC_TRUST_SCORE_H

#include "trust_features.h"

namespace satcomfec {

float compute_trust_score(const TrustFeatures& features);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_SCORE_H
