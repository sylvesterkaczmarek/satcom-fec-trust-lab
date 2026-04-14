#ifndef SATCOMFEC_FRAMING_H
#define SATCOMFEC_FRAMING_H

#include <cstdint>
#include <vector>

namespace satcomfec {

// Each entry is a signed 8-bit soft decision value after demodulation.
using SoftBit = int8_t;
using SoftBitBuffer = std::vector<SoftBit>;

struct FrameDescriptor {
    size_t sync_start_index = 0;
    size_t start_index = 0;
    size_t length = 0;
    int correlation_score = 0;
    bool has_second_best_correlation = false;
    size_t second_best_sync_start_index = 0;
    int second_best_correlation_score = 0;
};

struct FramingConfig {
    std::vector<uint8_t> sync_word;
    size_t coded_bits_per_frame = 0;
    int min_correlation_score = 0;
};

bool find_frames(const SoftBitBuffer& soft_bits,
                 std::vector<FrameDescriptor>& frames,
                 const FramingConfig& cfg);

}  // namespace satcomfec

#endif  // SATCOMFEC_FRAMING_H
