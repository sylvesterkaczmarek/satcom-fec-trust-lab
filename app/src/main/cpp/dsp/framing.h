#ifndef SATCOMFEC_FRAMING_H
#define SATCOMFEC_FRAMING_H

#include <cstdint>
#include <vector>

namespace satcomfec {

/**
 * Represents a frame of soft bits after demodulation.
 * Each entry is a signed 8 bit LLR style value.
 */
using SoftBit = int8_t;
using SoftBitBuffer = std::vector<SoftBit>;

/**
 * Very simple frame descriptor, just start index and length
 * into a soft bit stream.
 */
struct FrameDescriptor {
    size_t start_index = 0;
    size_t length = 0;
    int correlation_score = 0;
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
