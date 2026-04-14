#include "trust_score.h"

#include <algorithm>
#include <cmath>

#include "../util/logging.h"

namespace satcomfec {

TrustScoreBreakdown compute_trust_score_breakdown(const TrustFeatures& features) {
    TrustScoreBreakdown breakdown;
    breakdown.llr_strength =
        std::max(0.0f, std::min(features.normalized_mean_abs_llr, 1.0f));
    breakdown.llr_consistency =
        std::max(0.0f, std::min(1.0f - features.weak_llr_fraction, 1.0f));
    breakdown.sync_quality =
        std::max(0.0f, std::min(features.normalized_sync_score, 1.0f));
    breakdown.sync_margin_quality =
        std::max(0.0f, std::min(features.normalized_sync_margin, 1.0f));
    breakdown.demod_quality =
        std::max(0.0f, std::min(1.0f - (4.0f * features.clipped_symbol_fraction), 1.0f));
    breakdown.crc_quality = std::max(0.0f, std::min(features.crc_pass, 1.0f));

    breakdown.score = 0.20f * breakdown.llr_strength +
                      0.20f * breakdown.llr_consistency +
                      0.15f * breakdown.sync_quality +
                      0.15f * breakdown.sync_margin_quality +
                      0.05f * breakdown.demod_quality +
                      0.25f * breakdown.crc_quality;
    if (breakdown.crc_quality < 0.5f) {
        breakdown.capped_by_crc_failure = true;
        breakdown.score = std::min(breakdown.score, 0.35f);
    }
    breakdown.score = std::max(0.0f, std::min(breakdown.score, 1.0f));
    return breakdown;
}

TrustAssessment assess_trust(const TrustFeatures& features,
                             const TrustScoreBreakdown& breakdown) {
    TrustAssessment assessment;
    assessment.weak_soft_bits = features.weak_llr_fraction > 0.08f;
    assessment.ambiguous_sync = features.normalized_sync_margin < 0.20f;
    assessment.demod_clipping = features.clipped_symbol_fraction > 0.05f;
    assessment.crc_failed = features.crc_pass < 0.5f;

    if (assessment.crc_failed || breakdown.score < 0.45f) {
        assessment.band = "low-confidence";
    } else if (assessment.weak_soft_bits || assessment.ambiguous_sync ||
               assessment.demod_clipping || breakdown.score < 0.95f) {
        assessment.band = "guarded";
    } else {
        assessment.band = "high-confidence";
    }

    return assessment;
}

float compute_trust_score(const TrustFeatures& features) {
    const TrustScoreBreakdown breakdown =
        compute_trust_score_breakdown(features);
    log_info("compute_trust_score: score computed");
    return breakdown.score;
}

}  // namespace satcomfec
