#include "trust_score.h"

#include <algorithm>
#include <cmath>

#include "../util/logging.h"

namespace satcomfec {

TrustScoreBreakdown compute_trust_score_breakdown(const TrustFeatures& features) {
    TrustScoreBreakdown breakdown;
    float total_weight = 0.0f;
    // Exclude missing measurements from both sides of the weighted mean.
    auto component = [&](bool measured, float quality, float weight) {
        if (!measured) {
            return 0.0f;
        }
        quality = std::clamp(quality, 0.0f, 1.0f);
        breakdown.score += weight * quality;
        total_weight += weight;
        return quality;
    };
    breakdown.soft_decision_strength = component(
        features.soft_decisions_measured, features.normalized_mean_abs_soft_decision, 0.15f);
    breakdown.soft_decision_consistency = component(
        features.soft_decisions_measured, 1.0f - features.weak_soft_decision_fraction, 0.15f);
    breakdown.acquisition_strength = component(
        features.acquisition_measured, features.normalized_acquisition_peak, 0.15f);
    breakdown.acquisition_separation = component(
        features.acquisition_separation_measured, features.acquisition_peak_separation, 0.15f);
    breakdown.acquisition_certainty = component(
        features.acquisition_separation_measured, 1.0f - features.residual_acquisition_uncertainty, 0.10f);
    breakdown.frame_sync_quality = component(
        features.frame_sync_measured, features.normalized_frame_sync_score, 0.05f);
    breakdown.frame_sync_margin_quality = component(
        features.frame_sync_margin_measured, features.normalized_frame_sync_margin, 0.05f);
    breakdown.demod_quality = component(
        features.clipping_measured, 1.0f - 4.0f * features.clipped_symbol_fraction, 0.05f);
    breakdown.crc_quality = component(
        features.crc_evaluated >= 0.5f, features.crc_pass, 0.15f);
    if (total_weight > 0.0f) {
        breakdown.score /= total_weight;
    }
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
        features.soft_decisions_measured && features.weak_soft_decision_fraction > 0.08f;
    assessment.ambiguous_acquisition =
        features.acquisition_separation_measured &&
        (features.timing_ambiguity > 0.70f || features.residual_acquisition_uncertainty > 0.65f);
    assessment.acquisition_rejected = features.acquisition_accepted < 0.5f;
    assessment.ambiguous_frame_sync =
        features.frame_sync_margin_measured && features.normalized_frame_sync_margin < 0.20f;
    assessment.demod_clipping = features.clipping_measured && features.clipped_symbol_fraction > 0.05f;
    assessment.crc_not_evaluated = features.crc_evaluated < 0.5f;
    assessment.crc_failed =
        features.crc_evaluated >= 0.5f && features.crc_pass < 0.5f;

    if (assessment.acquisition_rejected || assessment.crc_failed || assessment.crc_not_evaluated ||
        breakdown.score < 0.45f) {
        assessment.band = "low-confidence";
    } else if (!features.acquisition_separation_measured || assessment.weak_soft_decisions ||
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
