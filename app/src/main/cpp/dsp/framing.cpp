#include "framing.h"

#include <limits>

#include "util/logging.h"

namespace satcomfec {

namespace {

int correlate_sync(const SoftBitBuffer& soft_bits,
                   size_t start_index,
                   const std::vector<uint8_t>& sync_word) {
    int score = 0;
    for (size_t i = 0; i < sync_word.size(); ++i) {
        const bool observed_bit = soft_bits[start_index + i] >= 0;
        score += (observed_bit == (sync_word[i] != 0U)) ? 1 : -1;
    }
    return score;
}

}  // namespace

bool find_frames(const SoftBitBuffer& soft_bits,
                 std::vector<FrameDescriptor>& frames,
                 const FramingConfig& cfg) {
    frames.clear();
    if (cfg.sync_word.empty() || cfg.coded_bits_per_frame == 0) {
        log_error("find_frames: framing config is incomplete");
        return false;
    }
    if (soft_bits.size() < cfg.sync_word.size() + cfg.coded_bits_per_frame) {
        log_error("find_frames: soft bit buffer shorter than one frame");
        return false;
    }

    int best_score = std::numeric_limits<int>::min();
    size_t best_offset = 0;
    const size_t max_offset =
        soft_bits.size() - cfg.sync_word.size() - cfg.coded_bits_per_frame;

    for (size_t offset = 0; offset <= max_offset; ++offset) {
        const int score = correlate_sync(soft_bits, offset, cfg.sync_word);
        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }

    if (best_score < cfg.min_correlation_score) {
        log_error("find_frames: no frame met the sync threshold");
        return false;
    }

    FrameDescriptor frame;
    frame.start_index = best_offset + cfg.sync_word.size();
    frame.length = cfg.coded_bits_per_frame;
    frame.correlation_score = best_score;
    frames.push_back(frame);
    log_info("find_frames: sync word located");
    return true;
}

}  // namespace satcomfec
