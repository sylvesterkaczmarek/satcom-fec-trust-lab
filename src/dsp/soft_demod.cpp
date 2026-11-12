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
    if (cfg.full_scale_symbol_mean &&
        (!std::isfinite(*cfg.full_scale_symbol_mean) || *cfg.full_scale_symbol_mean <= 0.0f)) {
        log_error("soft_demodulate_bpsk: fixed quantiser full scale must be finite and positive");
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
        double scaled = mean * scale;
        if (cfg.full_scale_symbol_mean) {
            const float full_scale = *cfg.full_scale_symbol_mean;
            if (std::abs(mean) > full_scale) {
                ++clipped_symbol_count;
            }
            scaled = 127.0 * static_cast<double>(mean) / full_scale;
        }
        const int8_t soft = static_cast<int8_t>(std::lround(std::clamp(scaled, -127.0, 127.0)));
        soft_bits.push_back(soft);
    }

    if (stats != nullptr) {
        stats->symbol_count = symbol_count;
        stats->samples_per_symbol = cfg.samples_per_symbol;
        if (cfg.full_scale_symbol_mean) {
            stats->clipped_symbol_count = clipped_symbol_count;
        }
        stats->max_abs_symbol_mean = max_abs_symbol_mean;
    }

    log_info("soft_demodulate_bpsk: integrate-and-dump complete");
    return true;
}

}  // namespace satcomfec
