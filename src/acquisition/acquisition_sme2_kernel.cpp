#include "acquisition/acquisition_sme2_kernel.h"

#include <cstdint>

#if defined(SATCOMFEC_ACQUISITION_SME2_KERNEL_COMPILED) && \
    defined(__ARM_FEATURE_SME2)
#include <arm_sme.h>
#include <arm_sve.h>

namespace satcomfec::acquisition::detail {

__arm_locally_streaming std::size_t sme2_streaming_lanes_f32() {
    return svcntw();
}

__arm_locally_streaming __arm_new("za")
void correlate_sme2_kernel(
    const float* received_interleaved,
    std::size_t received_sample_stride,
    const float* weight_real,
    const float* weight_imag,
    std::size_t timing_count,
    std::size_t frequency_count,
    std::size_t preamble_length,
    float* correlation_real,
    float* correlation_imag) {
    const std::size_t lanes = svcntw();
    const std::size_t batch_width = 4 * lanes;
    const svfloat32_t zero = svdup_f32(0.0F);
    const svfloat32x4_t zeros = svcreate4_f32(zero, zero, zero, zero);

    for (std::size_t frequency_index = 0;
         frequency_index < frequency_count;
         ++frequency_index) {
        const std::size_t weight_base = frequency_index * preamble_length;
        const std::size_t output_base = frequency_index * timing_count;

        for (std::size_t timing_base = 0;
             timing_base < timing_count;
             timing_base += batch_width) {
            const std::size_t lane_base_0 = timing_base;
            const std::size_t lane_base_1 = timing_base + lanes;
            const std::size_t lane_base_2 = timing_base + 2 * lanes;
            const std::size_t lane_base_3 = timing_base + 3 * lanes;
            const svbool_t pg0 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_0),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg1 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_1),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg2 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_2),
                static_cast<std::uint64_t>(timing_count));
            const svbool_t pg3 = svwhilelt_b32(
                static_cast<std::uint64_t>(lane_base_3),
                static_cast<std::uint64_t>(timing_count));
            const std::size_t safe_base_1 = lane_base_1 < timing_count
                                                ? lane_base_1
                                                : timing_count;
            const std::size_t safe_base_2 = lane_base_2 < timing_count
                                                ? lane_base_2
                                                : timing_count;
            const std::size_t safe_base_3 = lane_base_3 < timing_count
                                                ? lane_base_3
                                                : timing_count;

            // Two ZA vector groups retain 4 * SVL real and imaginary correlations.
            svwrite_za32_f32_vg1x4(0, zeros);
            svwrite_za32_f32_vg1x4(1, zeros);

            for (std::size_t sample_index = 0;
                 sample_index < preamble_length;
                 ++sample_index) {
                const std::size_t sample_base =
                    sample_index * received_sample_stride;
                const svfloat32x2_t received_group_0 = svld2_f32(
                    pg0,
                    received_interleaved + 2 * (sample_base + lane_base_0));
                const svfloat32x2_t received_group_1 = svld2_f32(
                    pg1,
                    received_interleaved + 2 * (sample_base + safe_base_1));
                const svfloat32x2_t received_group_2 = svld2_f32(
                    pg2,
                    received_interleaved + 2 * (sample_base + safe_base_2));
                const svfloat32x2_t received_group_3 = svld2_f32(
                    pg3,
                    received_interleaved + 2 * (sample_base + safe_base_3));
                const svfloat32x4_t received_real_group = svcreate4_f32(
                    svget2_f32(received_group_0, 0),
                    svget2_f32(received_group_1, 0),
                    svget2_f32(received_group_2, 0),
                    svget2_f32(received_group_3, 0));
                const svfloat32x4_t received_imag_group = svcreate4_f32(
                    svget2_f32(received_group_0, 1),
                    svget2_f32(received_group_1, 1),
                    svget2_f32(received_group_2, 1),
                    svget2_f32(received_group_3, 1));
                const svfloat32_t weight_real_vector = svdup_f32(
                    weight_real[weight_base + sample_index]);
                const svfloat32_t weight_imag_vector = svdup_f32(
                    weight_imag[weight_base + sample_index]);

                svmla_single_za32_f32_vg1x4(
                    0, received_real_group, weight_real_vector);
                svmls_single_za32_f32_vg1x4(
                    0, received_imag_group, weight_imag_vector);
                svmla_single_za32_f32_vg1x4(
                    1, received_real_group, weight_imag_vector);
                svmla_single_za32_f32_vg1x4(
                    1, received_imag_group, weight_real_vector);
            }

            const svfloat32x4_t real_group = svread_za32_f32_vg1x4(0);
            const svfloat32x4_t imag_group = svread_za32_f32_vg1x4(1);
            svst1_f32(
                pg0,
                correlation_real + output_base + lane_base_0,
                svget4_f32(real_group, 0));
            svst1_f32(
                pg1,
                correlation_real + output_base + safe_base_1,
                svget4_f32(real_group, 1));
            svst1_f32(
                pg2,
                correlation_real + output_base + safe_base_2,
                svget4_f32(real_group, 2));
            svst1_f32(
                pg3,
                correlation_real + output_base + safe_base_3,
                svget4_f32(real_group, 3));
            svst1_f32(
                pg0,
                correlation_imag + output_base + lane_base_0,
                svget4_f32(imag_group, 0));
            svst1_f32(
                pg1,
                correlation_imag + output_base + safe_base_1,
                svget4_f32(imag_group, 1));
            svst1_f32(
                pg2,
                correlation_imag + output_base + safe_base_2,
                svget4_f32(imag_group, 2));
            svst1_f32(
                pg3,
                correlation_imag + output_base + safe_base_3,
                svget4_f32(imag_group, 3));
        }
    }
}

}  // namespace satcomfec::acquisition::detail

#endif
