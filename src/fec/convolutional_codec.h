#ifndef SATCOMFEC_CONVOLUTIONAL_CODEC_H
#define SATCOMFEC_CONVOLUTIONAL_CODEC_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../dsp/framing.h"

namespace satcomfec {

constexpr size_t kConvConstraintLength = 3;
constexpr uint8_t kConvGenerator0 = 0b111;
constexpr uint8_t kConvGenerator1 = 0b101;

enum class ImplementationClass {
    kReal,
    kSimplified,
    kPlaceholder,
};

struct ImplementationInfo {
    const char* path_name = "";
    ImplementationClass implementation_class = ImplementationClass::kPlaceholder;
    const char* summary = "";
};

struct BranchMetricTables {
    std::array<std::vector<int16_t>, 4> metric_by_symbol_type;
    size_t symbol_count = 0;
};

std::vector<uint8_t> convolutional_encode(const std::vector<uint8_t>& input_bits);
bool viterbi_decode_reference(const SoftBitBuffer& soft_in,
                              std::vector<uint8_t>& hard_out);
bool prepare_branch_metrics_reference(const SoftBitBuffer& soft_in,
                                      BranchMetricTables& tables);
bool prepare_branch_metrics_neon(const SoftBitBuffer& soft_in,
                                 BranchMetricTables& tables);
bool viterbi_decode_from_metrics(const BranchMetricTables& tables,
                                 std::vector<uint8_t>& hard_out);

const ImplementationInfo& viterbi_neon_implementation_info();
const ImplementationInfo& viterbi_sme2_implementation_info();
const char* implementation_class_label(ImplementationClass value);

uint8_t crc8_bytes(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> bytes_to_bits(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> bits_to_bytes(const std::vector<uint8_t>& bits);
std::string bytes_to_ascii(const std::vector<uint8_t>& bytes);

}  // namespace satcomfec

#endif  // SATCOMFEC_CONVOLUTIONAL_CODEC_H
