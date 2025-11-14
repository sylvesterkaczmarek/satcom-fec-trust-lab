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
};

/**
 * Stub framing function.
 *
 * For now, this treats the entire soft bit buffer as one frame.
 */
void find_frames(const SoftBitBuffer& soft_bits,
                 std::vector<FrameDescriptor>& frames);

}  // namespace satcomfec

#endif  // SATCOMFEC_FRAMING_H
