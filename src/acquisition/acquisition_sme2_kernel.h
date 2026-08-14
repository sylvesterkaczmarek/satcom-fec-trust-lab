#ifndef SATCOMFEC_ACQUISITION_ACQUISITION_SME2_KERNEL_H
#define SATCOMFEC_ACQUISITION_ACQUISITION_SME2_KERNEL_H

#include <cstddef>

namespace satcomfec::acquisition::detail {

void correlate_sme2_kernel(
    const float* received_interleaved,
    std::size_t received_sample_stride,
    const float* weight_real,
    const float* weight_imag,
    std::size_t timing_count,
    std::size_t frequency_count,
    std::size_t preamble_length,
    float* correlation_real,
    float* correlation_imag);

std::size_t sme2_streaming_lanes_f32();

}  // namespace satcomfec::acquisition::detail

#endif  // SATCOMFEC_ACQUISITION_ACQUISITION_SME2_KERNEL_H
