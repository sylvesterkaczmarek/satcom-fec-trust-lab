#include "trust_score.h"

#include <algorithm>
#include <cmath>

#include "../util/logging.h"

namespace satcomfec {

TrustScoreBreakdown compute_trust_score_breakdown(const TrustFeatures& features) {
    TrustScoreBreakdown breakdown;
    breakdown.soft_decision_strength = std::max(
        0.0f, std::min(features.normalized_mean_abs_soft_decision, 1.0f));
    breakdown.soft_decision_consistency = std::max(
        0.0f, std::min(1.0f - features.weak_soft_decision_fraction, 1.0f));
    breakdown.acquisition_strength = std::max(
        0.0f, std::min(features.normalized_acquisition_peak, 1.0f));
    breakdown.acquisition_separation = std::max(
        0.0f, std::min(features.acquisition_peak_separation, 1.0f));
    breakdown.acquisition_certainty = std::max(
        0.0f, std::min(
            1.0f - features.residual_acquisition_uncertainty, 1.0f));
    breakdown.frame_sync_quality = std::max(
        0.0f, std::min(features.normalized_frame_sync_score, 1.0f));
    breakdown.frame_sync_margin_quality = std::max(
        0.0f, std::min(features.normalized_frame_sync_margin, 1.0f));
    breakdown.demod_quality =
        std::max(0.0f, std::min(1.0f - (4.0f * features.clipped_symbol_fraction), 1.0f));
    breakdown.crc_quality = std::max(0.0f, std::min(features.crc_pass, 1.0f));

    breakdown.score = 0.15f * breakdown.soft_decision_strength +
                      0.15f * breakdown.soft_decision_consistency +
                      0.15f * breakdown.acquisition_strength +
                      0.15f * breakdown.acquisition_separation +
                      0.10f * breakdown.acquisition_certainty +
                      0.05f * breakdown.frame_sync_quality +
                      0.05f * breakdown.frame_sync_margin_quality +
                      0.05f * breakdown.demod_quality +
                      0.15f * breakdown.crc_quality;
    if (features.acquisition_accepted < 0.5f) {
        breakdown.capped_by_acquisition_rejection = true;
        breakdown.score = std::min(breakdown.score, 0.15f);
    }
    if (features.crc_evaluated >= 0.5f && breakdown.crc_quality < 0.5f) {
        breakdown.capped_by_crc_failure = true;
        breakdown.score = std::min(breakdown.score, 0.35f);
    }
    breakdown.score = std::max(0.0f, std::min(breakdown.score, 1.0f));
    return breakdown;
}

TrustAssessment assess_trust(const TrustFeatures& features,
                             const TrustScoreBreakdown& breakdown) {
    TrustAssessment assessment;
    assessment.weak_soft_decisions =
        features.weak_soft_decision_fraction > 0.08f;
    assessment.ambiguous_acquisition =
        features.timing_ambiguity > 0.70f ||
        features.residual_acquisition_uncertainty > 0.65f;
    assessment.acquisition_rejected = features.acquisition_accepted < 0.5f;
    assessment.ambiguous_frame_sync =
        features.normalized_frame_sync_margin < 0.20f;
    assessment.demod_clipping = features.clipped_symbol_fraction > 0.05f;
    assessment.crc_not_evaluated = features.crc_evaluated < 0.5f;
    assessment.crc_failed =
        features.crc_evaluated >= 0.5f && features.crc_pass < 0.5f;

    if (assessment.acquisition_rejected || assessment.crc_failed ||
        breakdown.score < 0.45f) {
        assessment.band = "low-confidence";
    } else if (assessment.weak_soft_decisions ||
               assessment.ambiguous_acquisition ||
               assessment.ambiguous_frame_sync ||
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
