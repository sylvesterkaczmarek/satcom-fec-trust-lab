#ifndef SATCOMFEC_TRUST_FEATURES_H
#define SATCOMFEC_TRUST_FEATURES_H

#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

struct TrustFeatures {
    float mean_abs_llr = 0.0f;
    float sync_score = 0.0f;
    float crc_pass = 0.0f;
};

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     int sync_score,
                                     int max_sync_score,
                                     bool crc_ok);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_FEATURES_H
