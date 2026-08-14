#ifndef SATCOMFEC_TRUST_SCORE_H
#define SATCOMFEC_TRUST_SCORE_H

#include "trust_features.h"

namespace satcomfec {

struct TrustScoreBreakdown {
    float soft_decision_strength = 0.0f;
    float soft_decision_consistency = 0.0f;
    float acquisition_strength = 0.0f;
    float acquisition_separation = 0.0f;
    float acquisition_certainty = 0.0f;
    float frame_sync_quality = 0.0f;
    float frame_sync_margin_quality = 0.0f;
    float demod_quality = 0.0f;
    float crc_quality = 0.0f;
    bool capped_by_acquisition_rejection = false;
    bool capped_by_crc_failure = false;
    float score = 0.0f;
};

struct TrustAssessment {
    const char* band = "low-confidence";
    bool weak_soft_decisions = false;
    bool ambiguous_acquisition = false;
    bool acquisition_rejected = false;
    bool ambiguous_frame_sync = false;
    bool demod_clipping = false;
    bool crc_not_evaluated = false;
    bool crc_failed = false;
};

TrustScoreBreakdown compute_trust_score_breakdown(const TrustFeatures& features);
TrustAssessment assess_trust(const TrustFeatures& features,
                             const TrustScoreBreakdown& breakdown);
float compute_trust_score(const TrustFeatures& features);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_SCORE_H
