#ifndef SATCOMFEC_TRUST_FEATURES_H
#define SATCOMFEC_TRUST_FEATURES_H

#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

/**
 * Minimal trust feature vector for now.
 * Values are simple floats, to be extended later.
 */
struct TrustFeatures {
    float mean_llr = 0.0f;
    float fer_window = 0.0f;
};

/**
 * Compute simple trust features over a decoded window.
 *
 * This stub only looks at soft bits and a supplied frame error rate.
 */
TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     float fer_window);

}  // namespace satcomfec

#endif  // SATCOMFEC_TRUST_FEATURES_H
