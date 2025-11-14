#include "trust_score.h"

#include <algorithm>
#include <cmath>

#include "../util/logging.h"

namespace satcomfec {

float compute_trust_score(const TrustFeatures& features) {
    // Very naive heuristic:
    // - large |mean_llr| is "good"
    // - higher FER is "bad"
    float llr_quality = std::min(std::fabs(features.mean_llr) / 64.0f, 1.0f);
    float fer_penalty = std::min(features.fer_window, 1.0f);

    float score = llr_quality * (1.0f - fer_penalty);
    score = std::max(0.0f, std::min(score, 1.0f));

    log_info("compute_trust_score stub");
    return score;
}

}  // namespace satcomfec
