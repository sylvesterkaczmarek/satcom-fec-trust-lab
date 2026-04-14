#include "soft_demod.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "util/logging.h"

namespace satcomfec {

bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits,
                          const DemodConfig& cfg,
                          DemodStats* stats) {
    soft_bits.clear();
    if (stats != nullptr) {
        *stats = DemodStats {};
    }
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
    std::vector<float> symbol_means(symbol_count, 0.0f);
    float max_abs_symbol_mean = 0.0f;

    for (size_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
        float accumulator = 0.0f;
        for (size_t k = 0; k < cfg.samples_per_symbol; ++k) {
            accumulator += iq[symbol_index * cfg.samples_per_symbol + k].real();
        }

        const float mean = accumulator / static_cast<float>(cfg.samples_per_symbol);
        symbol_means[symbol_index] = mean;
        max_abs_symbol_mean = std::max(max_abs_symbol_mean, std::abs(mean));
    }

    size_t clipped_symbol_count = 0;
    const float scale = (max_abs_symbol_mean > 1.0e-6f)
                            ? (127.0f / max_abs_symbol_mean)
                            : 0.0f;
    for (float mean : symbol_means) {
        const float scaled = mean * scale;
        const float clipped = std::max(std::min(scaled, 127.0f), -127.0f);
        if (clipped != scaled) {
            ++clipped_symbol_count;
        }
        const int8_t soft = static_cast<int8_t>(std::lround(clipped));
        soft_bits.push_back(soft);
    }

    if (stats != nullptr) {
        stats->symbol_count = symbol_count;
        stats->samples_per_symbol = cfg.samples_per_symbol;
        stats->clipped_symbol_count = clipped_symbol_count;
        stats->max_abs_symbol_mean = max_abs_symbol_mean;
    }

    log_info("soft_demodulate_bpsk: integrate-and-dump complete");
    return true;
}

}  // namespace satcomfec
