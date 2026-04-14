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

bool run_front_end(const std::vector<ComplexF>& iq_in,
                   std::vector<ComplexF>& iq_out,
                   const FrontEndConfig& cfg);

}  // namespace satcomfec

#endif  // SATCOMFEC_FRONT_END_DSP_H
