#ifndef SATCOMFEC_TRUST_FEATURES_H
#define SATCOMFEC_TRUST_FEATURES_H

#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

struct DemodStats;
struct FrameDescriptor;

struct TrustFeatures {
    float mean_abs_llr = 0.0f;
    float normalized_mean_abs_llr = 0.0f;
    float weak_llr_fraction = 0.0f;
    float normalized_sync_score = 0.0f;
    float normalized_sync_margin = 0.0f;
    float clipped_symbol_fraction = 0.0f;
    float crc_pass = 0.0f;
};

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     const FrameDescriptor& frame,
                                     int max_sync_score,
                                     const DemodStats& demod_stats,
                                     bool crc_ok);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_FEATURES_H
