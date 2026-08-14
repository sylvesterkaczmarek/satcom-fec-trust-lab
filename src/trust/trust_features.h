#ifndef SATCOMFEC_TRUST_FEATURES_H
#define SATCOMFEC_TRUST_FEATURES_H

#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

struct DemodStats;
struct FrameDescriptor;
struct ReplayAcquisitionDiagnostics;

struct TrustFeatures {
    float mean_abs_soft_decision = 0.0f;
    float normalized_mean_abs_soft_decision = 0.0f;
    float weak_soft_decision_fraction = 0.0f;
    float normalized_frame_sync_score = 0.0f;
    float normalized_frame_sync_margin = 0.0f;
    float normalized_acquisition_peak = 0.0f;
    float acquisition_peak_separation = 0.0f;
    float timing_ambiguity = 1.0f;
    float residual_acquisition_uncertainty = 1.0f;
    float acquisition_accepted = 0.0f;
    float clipped_symbol_fraction = 0.0f;
    float crc_evaluated = 0.0f;
    float crc_pass = 0.0f;
};

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     const FrameDescriptor& frame,
                                     int max_sync_score,
                                     const DemodStats& demod_stats,
                                     const ReplayAcquisitionDiagnostics& acquisition,
                                     bool crc_evaluated,
                                     bool crc_ok);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_FEATURES_H
