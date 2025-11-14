#ifndef SATCOMFEC_SOFT_DEMOD_H
#define SATCOMFEC_SOFT_DEMOD_H

#include <vector>

#include "front_end_dsp.h"
#include "framing.h"

namespace satcomfec {

/**
 * Stub soft demodulation.
 *
 * In a real implementation this would take complex samples and produce
 * soft bits (LLRs). For now we just quantise the real part.
 */
bool soft_demodulate_bpsk(const std::vector<ComplexF>& iq,
                          SoftBitBuffer& soft_bits);

}  // namespace satcomfec

#endif  // SATCOMFEC_SOFT_DEMOD_H
