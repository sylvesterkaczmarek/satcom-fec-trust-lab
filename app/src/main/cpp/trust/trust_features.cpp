#include "trust_features.h"

#include <cstddef>

#include "../util/logging.h"

namespace satcomfec {

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     float fer_window) {
    TrustFeatures f;

    if (!soft_bits.empty()) {
        long sum = 0;
        for (auto s : soft_bits) {
            sum += static_cast<int>(s);
        }
        f.mean_llr = static_cast<float>(sum) /
                     static_cast<float>(soft_bits.size());
    } else {
        f.mean_llr = 0.0f;
    }

    f.fer_window = fer_window;

    log_info("compute_trust_features stub");
    return f;
}

}  // namespace satcomfec
