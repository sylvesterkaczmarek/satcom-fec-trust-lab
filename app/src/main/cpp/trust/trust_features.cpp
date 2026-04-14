#include "trust_features.h"

#include <algorithm>
#include <cstdlib>
#include <cstddef>

#include "../dsp/soft_demod.h"
#include "../util/logging.h"

namespace satcomfec {

namespace {

constexpr float kTrustLlrNormalization = 96.0f;
constexpr int kWeakLlrThreshold = 48;

}  // namespace

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     const FrameDescriptor& frame,
                                     int max_sync_score,
                                     const DemodStats& demod_stats,
                                     bool crc_ok) {
    TrustFeatures f;

    if (!soft_bits.empty()) {
        long sum = 0;
        size_t weak_count = 0;
        for (auto s : soft_bits) {
            const int abs_llr = std::abs(static_cast<int>(s));
            sum += abs_llr;
            if (abs_llr < kWeakLlrThreshold) {
                ++weak_count;
            }
        }
        f.mean_abs_llr = static_cast<float>(sum) /
                         static_cast<float>(soft_bits.size());
        f.weak_llr_fraction = static_cast<float>(weak_count) /
                              static_cast<float>(soft_bits.size());
    } else {
        f.mean_abs_llr = 0.0f;
        f.weak_llr_fraction = 1.0f;
    }
    f.normalized_mean_abs_llr =
        std::min(f.mean_abs_llr / kTrustLlrNormalization, 1.0f);

    if (max_sync_score > 0) {
        f.normalized_sync_score = static_cast<float>(frame.correlation_score) /
                                  static_cast<float>(max_sync_score);
        if (frame.has_second_best_correlation) {
            const int sync_margin =
                frame.correlation_score - frame.second_best_correlation_score;
            f.normalized_sync_margin =
                std::max(0.0f, std::min(static_cast<float>(sync_margin) /
                                            static_cast<float>(max_sync_score),
                                        1.0f));
        } else {
            f.normalized_sync_margin = 1.0f;
        }
    } else {
        f.normalized_sync_score = 0.0f;
        f.normalized_sync_margin = 0.0f;
    }

    if (demod_stats.symbol_count > 0) {
        f.clipped_symbol_fraction =
            static_cast<float>(demod_stats.clipped_symbol_count) /
            static_cast<float>(demod_stats.symbol_count);
    } else {
        f.clipped_symbol_fraction = 0.0f;
    }

    f.crc_pass = crc_ok ? 1.0f : 0.0f;

    log_info("compute_trust_features: features extracted");
    return f;
}

}  // namespace satcomfec
