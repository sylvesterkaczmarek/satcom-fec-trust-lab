#include "trust_features.h"

#include <cstdlib>
#include <cstddef>

#include "../util/logging.h"

namespace satcomfec {

TrustFeatures compute_trust_features(const SoftBitBuffer& soft_bits,
                                     int sync_score,
                                     int max_sync_score,
                                     bool crc_ok) {
    TrustFeatures f;

    if (!soft_bits.empty()) {
        long sum = 0;
        for (auto s : soft_bits) {
            sum += std::abs(static_cast<int>(s));
        }
        f.mean_abs_llr = static_cast<float>(sum) /
                         static_cast<float>(soft_bits.size());
    } else {
        f.mean_abs_llr = 0.0f;
    }

    if (max_sync_score > 0) {
        f.sync_score = static_cast<float>(sync_score) /
                       static_cast<float>(max_sync_score);
    } else {
        f.sync_score = 0.0f;
    }

    f.crc_pass = crc_ok ? 1.0f : 0.0f;

    log_info("compute_trust_features: features extracted");
    return f;
}

}  // namespace satcomfec
