#include "trust_features.h"

#include <algorithm>
#include <cstdlib>
#include <cstddef>

#include "../demo/replay_acquisition.h"
#include "../dsp/soft_demod.h"
#include "../util/logging.h"

namespace satcomfec {

namespace {

constexpr float kTrustSoftDecisionNormalization = 96.0f;
constexpr int kWeakSoftDecisionThreshold = 48;

}  // namespace

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     const FrameDescriptor& frame,
                                     int max_sync_score,
                                     const DemodStats& demod_stats,
                                     const ReplayAcquisitionDiagnostics& acquisition,
                                     bool crc_evaluated,
                                     bool crc_ok) {
    TrustFeatures f;

    if (!soft_bits.empty()) {
        long sum = 0;
        size_t weak_count = 0;
        for (auto s : soft_bits) {
            const int absolute_soft_decision = std::abs(static_cast<int>(s));
            sum += absolute_soft_decision;
            if (absolute_soft_decision < kWeakSoftDecisionThreshold) {
                ++weak_count;
            }
        }
        f.mean_abs_soft_decision = static_cast<float>(sum) /
                                   static_cast<float>(soft_bits.size());
        f.weak_soft_decision_fraction = static_cast<float>(weak_count) /
                                        static_cast<float>(soft_bits.size());
    } else {
        f.mean_abs_soft_decision = 0.0f;
        f.weak_soft_decision_fraction = 1.0f;
    }
    f.normalized_mean_abs_soft_decision = std::min(
        f.mean_abs_soft_decision / kTrustSoftDecisionNormalization, 1.0f);

    if (max_sync_score > 0 && frame.length > 0) {
        f.normalized_frame_sync_score =
            static_cast<float>(frame.correlation_score) /
            static_cast<float>(max_sync_score);
        if (frame.has_second_best_correlation) {
            const int sync_margin =
                frame.correlation_score - frame.second_best_correlation_score;
            f.normalized_frame_sync_margin =
                std::max(0.0f, std::min(static_cast<float>(sync_margin) /
                                            static_cast<float>(max_sync_score),
                                        1.0f));
        } else {
            f.normalized_frame_sync_margin = 1.0f;
        }
    } else {
        f.normalized_frame_sync_score = 0.0f;
        f.normalized_frame_sync_margin = 0.0f;
    }

    if (acquisition.search_completed) {
        f.normalized_acquisition_peak = static_cast<float>(
            std::max(0.0, std::min(acquisition.normalized_peak, 1.0)));
        f.acquisition_peak_separation = static_cast<float>(
            std::max(0.0, std::min(
                acquisition.normalized_peak_separation, 1.0)));
        f.timing_ambiguity = 1.0f - f.acquisition_peak_separation;
        f.residual_acquisition_uncertainty = 1.0f - static_cast<float>(
            std::max(0.0, std::min(acquisition.confidence, 1.0)));
    }
    f.acquisition_accepted = acquisition.accepted ? 1.0f : 0.0f;

    if (demod_stats.symbol_count > 0) {
        f.clipped_symbol_fraction =
            static_cast<float>(demod_stats.clipped_symbol_count) /
            static_cast<float>(demod_stats.symbol_count);
    } else {
        f.clipped_symbol_fraction = 0.0f;
    }

    f.crc_evaluated = crc_evaluated ? 1.0f : 0.0f;
    f.crc_pass = (crc_evaluated && crc_ok) ? 1.0f : 0.0f;

    log_info("compute_trust_features: features extracted");
    return f;
}

}  // namespace satcomfec
