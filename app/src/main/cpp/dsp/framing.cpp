#include "framing.h"

#include "util/logging.h"

namespace satcomfec {

void find_frames(const SoftBitBuffer& soft_bits,
                 std::vector<FrameDescriptor>& frames) {
    frames.clear();

    FrameDescriptor frame;
    frame.start_index = 0;
    frame.length = soft_bits.size();

    frames.push_back(frame);
    log_info("find_frames stub: one frame over full buffer");
}

}  // namespace satcomfec
