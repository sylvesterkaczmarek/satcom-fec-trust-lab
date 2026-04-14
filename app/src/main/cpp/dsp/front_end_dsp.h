#ifndef SATCOMFEC_FRONT_END_DSP_H
#define SATCOMFEC_FRONT_END_DSP_H

#include <complex>
#include <vector>

namespace satcomfec {

using ComplexF = std::complex<float>;

struct FrontEndConfig {
    float sample_rate_hz = 48000.0f;
    float center_freq_hz = 0.0f;
    bool remove_dc = true;
    bool normalize_rms = true;
};

struct FrontEndStats {
    size_t sample_count = 0;
    float dc_i = 0.0f;
    float dc_q = 0.0f;
    float rms_before_normalization = 0.0f;
    float rms_after_normalization = 0.0f;
};

bool run_front_end(const std::vector<ComplexF>& iq_in,
                   std::vector<ComplexF>& iq_out,
                   const FrontEndConfig& cfg,
                   FrontEndStats* stats = nullptr);

}  // namespace satcomfec

#endif  // SATCOMFEC_FRONT_END_DSP_H
