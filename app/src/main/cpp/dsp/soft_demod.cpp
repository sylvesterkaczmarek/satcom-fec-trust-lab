#include "soft_demod.h"

#include <algorithm>
#include <cmath>

#include "util/logging.h"

namespace satcomfec {

bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits,
                          const DemodConfig& cfg) {
    soft_bits.clear();
    if (cfg.samples_per_symbol == 0) {
        log_error("soft_demodulate_bpsk: samples_per_symbol must be > 0");
        return false;
    }
    if (iq.size() < cfg.samples_per_symbol) {
        log_error("soft_demodulate_bpsk: IQ buffer shorter than one symbol");
        return false;
    }

    const size_t symbol_count = iq.size() / cfg.samples_per_symbol;
    soft_bits.reserve(symbol_count);

    for (size_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
        float accumulator = 0.0f;
        for (size_t k = 0; k < cfg.samples_per_symbol; ++k) {
            accumulator += iq[symbol_index * cfg.samples_per_symbol + k].real();
        }

        const float mean = accumulator / static_cast<float>(cfg.samples_per_symbol);
        const float clipped = std::max(std::min(mean, 1.0f), -1.0f);
        const int8_t soft = static_cast<int8_t>(clipped * 127.0f);
        soft_bits.push_back(soft);
    }

    log_info("soft_demodulate_bpsk: integrate-and-dump complete");
    return true;
}

}  // namespace satcomfec
