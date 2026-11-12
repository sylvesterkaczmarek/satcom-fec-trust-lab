#ifndef SATCOMFEC_SOFT_DEMOD_H
#define SATCOMFEC_SOFT_DEMOD_H

#include <optional>
#include <vector>

#include "front_end_dsp.h"
#include "framing.h"

namespace satcomfec {

struct DemodConfig {
    size_t samples_per_symbol = 8;
    // Fixed full-scale magnitude of the integrated BPSK symbol, when known.
    // The default adapts the quantiser to each frame.
    std::optional<float> full_scale_symbol_mean = std::nullopt;
};

struct DemodStats {
    size_t symbol_count = 0;
    size_t samples_per_symbol = 0;
    std::optional<size_t> clipped_symbol_count;
    float max_abs_symbol_mean = 0.0f;
};

bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits,
                          const DemodConfig& cfg = {},
                          DemodStats* stats = nullptr);

}  // namespace satcomfec

#endif  // SATCOMFEC_SOFT_DEMOD_H
