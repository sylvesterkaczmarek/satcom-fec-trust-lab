#include "front_end_dsp.h"

#include <cmath>

#include "util/logging.h"

namespace satcomfec {

bool run_front_end(const std::vector<ComplexF>& iq_in,
                   std::vector<ComplexF>& iq_out,
                   const FrontEndConfig& cfg,
                   FrontEndStats* stats) {
    iq_out.clear();
    if (stats != nullptr) {
        *stats = FrontEndStats {};
    }
    if (iq_in.empty()) {
        log_error("run_front_end: empty IQ buffer");
        return false;
    }

    ComplexF mean(0.0f, 0.0f);
    if (cfg.remove_dc) {
        for (const auto& sample : iq_in) {
            mean += sample;
        }
        mean /= static_cast<float>(iq_in.size());
    }

    iq_out.reserve(iq_in.size());
    float energy_sum = 0.0f;
    for (const auto& sample : iq_in) {
        ComplexF corrected = cfg.remove_dc ? (sample - mean) : sample;
        energy_sum += std::norm(corrected);
        iq_out.push_back(corrected);
    }

    const float rms_before_normalization =
        std::sqrt(energy_sum / static_cast<float>(iq_out.size()));
    float rms_after_normalization = rms_before_normalization;

    if (cfg.normalize_rms) {
        if (rms_before_normalization > 1.0e-6f) {
            const float scale = 1.0f / rms_before_normalization;
            for (auto& sample : iq_out) {
                sample *= scale;
            }
            rms_after_normalization = 1.0f;
        }
    }

    if (stats != nullptr) {
        stats->sample_count = iq_in.size();
        stats->dc_i = mean.real();
        stats->dc_q = mean.imag();
        stats->rms_before_normalization = rms_before_normalization;
        stats->rms_after_normalization = rms_after_normalization;
    }

    log_info("run_front_end: dc removal and RMS normalization complete");
    return true;
}

}  // namespace satcomfec
