#include "soft_demod.h"

#include <algorithm>
#include <cmath>

#include "util/logging.h"

namespace satcomfec {

bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits) {
    soft_bits.clear();
    soft_bits.reserve(iq.size());

    for (const auto& sample : iq) {
        float val = sample.real();
        float clipped = std::max(std::min(val, 1.0f), -1.0f);
        int8_t soft = static_cast<int8_t>(clipped * 127.0f);
        soft_bits.push_back(soft);
    }

    log_info("soft_demodulate_bpsk stub: sign based mapping");
    return true;
}

}  // namespace satcomfec
