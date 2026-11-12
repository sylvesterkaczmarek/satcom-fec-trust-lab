#ifndef SATCOMFEC_ACQUISITION_PEAKS_H
#define SATCOMFEC_ACQUISITION_PEAKS_H

#include "acquisition/acquisition.h"

#include <algorithm>
#include <limits>

namespace satcomfec::acquisition {

inline void consider_candidate(
    const AcquisitionCandidate& candidate,
    AcquisitionCandidate& best,
    AcquisitionCandidate& second_best) {
    if (!best.valid || candidate.score > best.score) {
        second_best = best;
        best = candidate;
    } else if (!second_best.valid || candidate.score > second_best.score) {
        second_best = candidate;
    }
}

// Called after evaluating every timing for one frequency, only for replay.
inline void retain_frequency_peaks(AcquisitionResult& result) {
    result.peaks_by_frequency.push_back({result.best, result.second_best});
    result.best = {};
    result.second_best = {};
}

inline void finish_candidate_ranking(AcquisitionResult& result) {
    // Each row's two highest scores are sufficient to recover the global two.
    // Preserve grid traversal order for ties.
    for (const FrequencyPeaks& row : result.peaks_by_frequency) {
        consider_candidate(row.best, result.best, result.second_best);
        if (row.second_best.valid) {
            consider_candidate(row.second_best, result.best, result.second_best);
        }
    }
    result.ok = result.best.valid;
    if (!result.ok) {
        result.error_message = "no acquisition candidate was evaluated";
        return;
    }
    if (result.second_best.valid) {
        const double denominator = std::max(
            result.second_best.score, std::numeric_limits<double>::min());
        result.peak_ratio = result.best.score / denominator;
        result.normalized_peak_separation = result.best.score > 0.0
            ? (result.best.score - result.second_best.score) / result.best.score
            : 0.0;
    } else {
        result.peak_ratio = std::numeric_limits<double>::infinity();
        result.normalized_peak_separation = 1.0;
    }
}

}  // namespace satcomfec::acquisition

#endif  // SATCOMFEC_ACQUISITION_PEAKS_H
