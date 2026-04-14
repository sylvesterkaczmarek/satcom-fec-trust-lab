#ifndef SATCOMFEC_TRUST_SCORE_H
#define SATCOMFEC_TRUST_SCORE_H

#include "trust_features.h"

namespace satcomfec {

struct TrustScoreBreakdown {
    float llr_strength = 0.0f;
    float llr_consistency = 0.0f;
    float sync_quality = 0.0f;
    float sync_margin_quality = 0.0f;
    float demod_quality = 0.0f;
    float crc_quality = 0.0f;
    bool capped_by_crc_failure = false;
    float score = 0.0f;
};

struct TrustAssessment {
    const char* band = "low-confidence";
    bool weak_soft_bits = false;
    bool ambiguous_sync = false;
    bool demod_clipping = false;
    bool crc_failed = false;
};

TrustScoreBreakdown compute_trust_score_breakdown(const TrustFeatures& features);
TrustAssessment assess_trust(const TrustFeatures& features,
                             const TrustScoreBreakdown& breakdown);
float compute_trust_score(const TrustFeatures& features);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_SCORE_H
