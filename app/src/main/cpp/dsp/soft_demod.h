#ifndef SATCOMFEC_SOFT_DEMOD_H
#define SATCOMFEC_SOFT_DEMOD_H

#include <vector>

#include "front_end_dsp.h"
#include "framing.h"

namespace satcomfec {

struct DemodConfig {
    size_t samples_per_symbol = 8;
};

bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits,
                          const DemodConfig& cfg = {});

}  // namespace satcomfec

#endif  // SATCOMFEC_SOFT_DEMOD_H
