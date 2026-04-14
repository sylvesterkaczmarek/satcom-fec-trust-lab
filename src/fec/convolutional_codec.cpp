#include "convolutional_codec.h"

#include <algorithm>
#include <limits>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include "../util/logging.h"

namespace satcomfec {

namespace {

uint8_t parity3(uint8_t value) {
    value ^= value >> 1U;
    value ^= value >> 2U;
    return value & 0x1U;
}

std::pair<uint8_t, uint8_t> encode_symbol(uint8_t state, uint8_t input_bit) {
    const uint8_t shift_reg =
        static_cast<uint8_t>(((state << 1U) | (input_bit & 0x1U)) & 0x7U);
    return {
        parity3(static_cast<uint8_t>(shift_reg & kConvGenerator0)),
        parity3(static_cast<uint8_t>(shift_reg & kConvGenerator1)),
    };
}

void resize_metric_tables(size_t symbol_count, BranchMetricTables& tables) {
    tables.symbol_count = symbol_count;
    for (auto& metric_vector : tables.metric_by_symbol_type) {
        metric_vector.assign(symbol_count, 0);
    }
}

}  // namespace

const ImplementationInfo& viterbi_neon_implementation_info() {
    static const ImplementationInfo info {
        "viterbi-neon",
        ImplementationClass::kReal,
        "Real implementation on Arm NEON targets: NEON precomputes branch metrics; "
        "add-compare-select and traceback remain shared scalar reference code.",
    };
    return info;
}

const ImplementationInfo& viterbi_sme2_implementation_info() {
    static const ImplementationInfo info {
        "viterbi-sme2",
        ImplementationClass::kSimplified,
        "Simplified implementation: shared scalar reference Viterbi decode with "
        "no SME2-specific kernel checked in.",
    };
    return info;
}

const char* implementation_class_label(ImplementationClass value) {
    switch (value) {
        case ImplementationClass::kReal:
            return "real";
        case ImplementationClass::kSimplified:
            return "simplified";
        case ImplementationClass::kPlaceholder:
            return "placeholder";
    }
    return "placeholder";
}

std::vector<uint8_t> convolutional_encode(const std::vector<uint8_t>& input_bits) {
    std::vector<uint8_t> encoded_bits;
    encoded_bits.reserve(2 * (input_bits.size() + kConvConstraintLength - 1));

    uint8_t state = 0;
    for (uint8_t bit : input_bits) {
        const auto encoded = encode_symbol(state, bit);
        encoded_bits.push_back(encoded.first);
        encoded_bits.push_back(encoded.second);
        state = static_cast<uint8_t>(((state << 1U) | (bit & 0x1U)) & 0x3U);
    }

    for (size_t i = 0; i < kConvConstraintLength - 1; ++i) {
        const auto encoded = encode_symbol(state, 0U);
        encoded_bits.push_back(encoded.first);
        encoded_bits.push_back(encoded.second);
        state = static_cast<uint8_t>((state << 1U) & 0x3U);
    }

    return encoded_bits;
}

bool prepare_branch_metrics_reference(const SoftBitBuffer& soft_in,
                                      BranchMetricTables& tables) {
    if (soft_in.empty() || (soft_in.size() % 2) != 0) {
        log_error("prepare_branch_metrics_reference: expected an even number of soft bits");
        return false;
    }

    const size_t symbol_count = soft_in.size() / 2;
    resize_metric_tables(symbol_count, tables);

    for (size_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
        const int llr0 = static_cast<int>(soft_in[2 * symbol_index]);
        const int llr1 = static_cast<int>(soft_in[2 * symbol_index + 1]);
        tables.metric_by_symbol_type[0][symbol_index] =
            static_cast<int16_t>(-(llr0 + llr1));
        tables.metric_by_symbol_type[1][symbol_index] =
            static_cast<int16_t>(llr1 - llr0);
        tables.metric_by_symbol_type[2][symbol_index] =
            static_cast<int16_t>(llr0 - llr1);
        tables.metric_by_symbol_type[3][symbol_index] =
            static_cast<int16_t>(llr0 + llr1);
    }

    return true;
}

bool prepare_branch_metrics_neon(const SoftBitBuffer& soft_in,
                                 BranchMetricTables& tables) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    if (soft_in.empty() || (soft_in.size() % 2) != 0) {
        log_error("prepare_branch_metrics_neon: expected an even number of soft bits");
        return false;
    }

    const size_t symbol_count = soft_in.size() / 2;
    resize_metric_tables(symbol_count, tables);

    size_t symbol_index = 0;
    for (; symbol_index + 16 <= symbol_count; symbol_index += 16) {
        const auto* interleaved_ptr =
            reinterpret_cast<const int8_t*>(soft_in.data() + 2 * symbol_index);
        const int8x16x2_t pair = vld2q_s8(interleaved_ptr);

        const int16x8_t llr0_lo = vmovl_s8(vget_low_s8(pair.val[0]));
        const int16x8_t llr0_hi = vmovl_s8(vget_high_s8(pair.val[0]));
        const int16x8_t llr1_lo = vmovl_s8(vget_low_s8(pair.val[1]));
        const int16x8_t llr1_hi = vmovl_s8(vget_high_s8(pair.val[1]));

        const int16x8_t sum_lo = vaddq_s16(llr0_lo, llr1_lo);
        const int16x8_t sum_hi = vaddq_s16(llr0_hi, llr1_hi);
        const int16x8_t diff_lo = vsubq_s16(llr0_lo, llr1_lo);
        const int16x8_t diff_hi = vsubq_s16(llr0_hi, llr1_hi);
        const int16x8_t inv_diff_lo = vsubq_s16(llr1_lo, llr0_lo);
        const int16x8_t inv_diff_hi = vsubq_s16(llr1_hi, llr0_hi);

        vst1q_s16(tables.metric_by_symbol_type[0].data() + symbol_index, vnegq_s16(sum_lo));
        vst1q_s16(tables.metric_by_symbol_type[0].data() + symbol_index + 8, vnegq_s16(sum_hi));
        vst1q_s16(tables.metric_by_symbol_type[1].data() + symbol_index, inv_diff_lo);
        vst1q_s16(tables.metric_by_symbol_type[1].data() + symbol_index + 8, inv_diff_hi);
        vst1q_s16(tables.metric_by_symbol_type[2].data() + symbol_index, diff_lo);
        vst1q_s16(tables.metric_by_symbol_type[2].data() + symbol_index + 8, diff_hi);
        vst1q_s16(tables.metric_by_symbol_type[3].data() + symbol_index, sum_lo);
        vst1q_s16(tables.metric_by_symbol_type[3].data() + symbol_index + 8, sum_hi);
    }

    for (; symbol_index < symbol_count; ++symbol_index) {
        const int llr0 = static_cast<int>(soft_in[2 * symbol_index]);
        const int llr1 = static_cast<int>(soft_in[2 * symbol_index + 1]);
        tables.metric_by_symbol_type[0][symbol_index] =
            static_cast<int16_t>(-(llr0 + llr1));
        tables.metric_by_symbol_type[1][symbol_index] =
            static_cast<int16_t>(llr1 - llr0);
        tables.metric_by_symbol_type[2][symbol_index] =
            static_cast<int16_t>(llr0 - llr1);
        tables.metric_by_symbol_type[3][symbol_index] =
            static_cast<int16_t>(llr0 + llr1);
    }

    return true;
#else
    return prepare_branch_metrics_reference(soft_in, tables);
#endif
}

bool viterbi_decode_from_metrics(const BranchMetricTables& tables,
                                 std::vector<uint8_t>& hard_out) {
    constexpr int kStateCount = 4;
    constexpr int kNegInf = std::numeric_limits<int>::min() / 4;
    const size_t symbol_count = tables.symbol_count;
    if (symbol_count == 0) {
        log_error("viterbi_decode_from_metrics: empty metric table");
        return false;
    }

    for (const auto& metric_vector : tables.metric_by_symbol_type) {
        if (metric_vector.size() != symbol_count) {
            log_error("viterbi_decode_from_metrics: inconsistent metric table sizes");
            return false;
        }
    }

    std::vector<int> path_metrics(kStateCount, kNegInf);
    std::vector<int> next_metrics(kStateCount, kNegInf);
    std::vector<uint8_t> decisions(symbol_count * kStateCount, 0U);
    std::vector<uint8_t> predecessors(symbol_count * kStateCount, 0U);
    path_metrics[0] = 0;

    for (size_t t = 0; t < symbol_count; ++t) {
        std::fill(next_metrics.begin(), next_metrics.end(), kNegInf);

        for (uint8_t state = 0; state < kStateCount; ++state) {
            if (path_metrics[state] == kNegInf) {
                continue;
            }

            for (uint8_t input_bit = 0; input_bit < 2; ++input_bit) {
                const auto encoded = encode_symbol(state, input_bit);
                const uint8_t next_state =
                    static_cast<uint8_t>(((state << 1U) | input_bit) & 0x3U);
                const uint8_t metric_index =
                    static_cast<uint8_t>((encoded.first << 1U) | encoded.second);
                const int candidate_metric =
                    path_metrics[state] +
                    static_cast<int>(tables.metric_by_symbol_type[metric_index][t]);

                if (candidate_metric > next_metrics[next_state]) {
                    next_metrics[next_state] = candidate_metric;
                    decisions[t * kStateCount + next_state] = input_bit;
                    predecessors[t * kStateCount + next_state] = state;
                }
            }
        }

        path_metrics.swap(next_metrics);
    }

    uint8_t best_state = 0;
    int best_metric = path_metrics[0];
    for (uint8_t state = 1; state < kStateCount; ++state) {
        if (path_metrics[state] > best_metric) {
            best_metric = path_metrics[state];
            best_state = state;
        }
    }

    std::vector<uint8_t> decoded_bits(symbol_count, 0U);
    for (size_t t = symbol_count; t-- > 0;) {
        decoded_bits[t] = decisions[t * kStateCount + best_state];
        best_state = predecessors[t * kStateCount + best_state];
    }

    if (decoded_bits.size() < kConvConstraintLength - 1) {
        log_error("viterbi_decode_from_metrics: decoded output shorter than tail");
        return false;
    }

    hard_out.assign(decoded_bits.begin(),
                    decoded_bits.end() - (kConvConstraintLength - 1));
    return true;
}

bool viterbi_decode_reference(const SoftBitBuffer& soft_in,
                              std::vector<uint8_t>& hard_out) {
    BranchMetricTables tables;
    if (!prepare_branch_metrics_reference(soft_in, tables)) {
        return false;
    }
    if (!viterbi_decode_from_metrics(tables, hard_out)) {
        return false;
    }
    return true;
}

uint8_t crc8_bytes(const std::vector<uint8_t>& bytes) {
    uint8_t crc = 0x00U;
    for (uint8_t value : bytes) {
        crc ^= value;
        for (int i = 0; i < 8; ++i) {
            const bool msb = (crc & 0x80U) != 0U;
            crc <<= 1U;
            if (msb) {
                crc ^= 0x07U;
            }
        }
    }
    return crc;
}

std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> bits;
    bits.reserve(bytes.size() * 8);
    for (uint8_t value : bytes) {
        for (int bit = 7; bit >= 0; --bit) {
            bits.push_back(static_cast<uint8_t>((value >> bit) & 0x1U));
        }
    }
    return bits;
}

std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0U);
    for (size_t i = 0; i < bits.size(); ++i) {
        bytes[i / 8] = static_cast<uint8_t>(
            (bytes[i / 8] << 1U) | (bits[i] & 0x1U));
        if ((i % 8) == 7) {
            continue;
        }
    }

    if ((bits.size() % 8) != 0) {
        bytes.back() <<= static_cast<uint8_t>(8 - (bits.size() % 8));
    }

    return bytes;
}

std::string bytes_to_ascii(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

}  // namespace satcomfec
