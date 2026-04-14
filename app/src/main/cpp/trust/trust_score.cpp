#include "trust_score.h"

#include <algorithm>
#include <cmath>

#include "../util/logging.h"

namespace satcomfec {

float compute_trust_score(const TrustFeatures& features) {
    const float llr_quality = std::min(features.mean_abs_llr / 96.0f, 1.0f);
    const float sync_quality = std::max(0.0f, std::min(features.sync_score, 1.0f));
    const float crc_quality = std::max(0.0f, std::min(features.crc_pass, 1.0f));

    float score = 0.45f * llr_quality + 0.25f * sync_quality + 0.30f * crc_quality;
    if (crc_quality < 0.5f) {
        score = std::min(score, 0.45f);
    }
    score = std::max(0.0f, std::min(score, 1.0f));

    log_info("compute_trust_score: score computed");
    return score;
}

}  // namespace satcomfec
