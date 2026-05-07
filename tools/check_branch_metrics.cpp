#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../src/fec/branch_metrics_sme2.h"
#include "../src/fec/convolutional_codec.h"

namespace {

satcomfec::SoftBitBuffer make_soft_bits(size_t symbol_count) {
    satcomfec::SoftBitBuffer soft_bits;
    soft_bits.reserve(symbol_count * 2);
    for (size_t i = 0; i < symbol_count * 2; ++i) {
        const int value = static_cast<int>((i * 37 + 19) % 241) - 120;
        soft_bits.push_back(static_cast<satcomfec::SoftBit>(value));
    }
    return soft_bits;
}

bool same_tables(const satcomfec::BranchMetricTables& lhs,
                 const satcomfec::BranchMetricTables& rhs) {
    return lhs.symbol_count == rhs.symbol_count &&
           lhs.metric_by_symbol_type == rhs.metric_by_symbol_type;
}

struct MetricCheck {
    size_t symbol_count = 0;
    bool neon_matches_reference = false;
    bool sme2_matches_reference = false;
};

}  // namespace

int main() {
    const std::vector<size_t> symbol_counts {
        1, 2, 3, 7, 15, 16, 17, 31, 32, 33, 61, 122,
    };

    std::vector<MetricCheck> checks;
    checks.reserve(symbol_counts.size());
    bool ok = true;

    for (size_t symbol_count : symbol_counts) {
        const satcomfec::SoftBitBuffer soft_bits = make_soft_bits(symbol_count);
        satcomfec::BranchMetricTables reference;
        satcomfec::BranchMetricTables neon;
        satcomfec::BranchMetricTables sme2;

        const bool reference_ok =
            satcomfec::prepare_branch_metrics_reference(soft_bits, reference);
        const bool neon_ok =
            satcomfec::prepare_branch_metrics_neon(soft_bits, neon);
        const bool sme2_ok =
            satcomfec::prepare_branch_metrics_sme2(soft_bits, sme2);

        MetricCheck check;
        check.symbol_count = symbol_count;
        check.neon_matches_reference =
            reference_ok && neon_ok && same_tables(reference, neon);
        check.sme2_matches_reference =
            reference_ok && sme2_ok && same_tables(reference, sme2);
        checks.push_back(check);
        ok = ok && check.neon_matches_reference && check.sme2_matches_reference;
    }

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (ok ? "true" : "false") << ",\n";
    std::cout << "  \"implementations\": {\n";
    std::cout << "    \"reference\": {\n";
    std::cout << "      \"selected\": \"reference\",\n";
    std::cout << "      \"kernel_compiled\": true\n";
    std::cout << "    },\n";
    std::cout << "    \"neon\": {\n";
    std::cout << "      \"selected\": \""
              << satcomfec::branch_metrics_neon_selected_implementation() << "\",\n";
    std::cout << "      \"kernel_compiled\": "
              << (satcomfec::branch_metrics_neon_kernel_compiled() ? "true" : "false")
              << "\n";
    std::cout << "    },\n";
    std::cout << "    \"sme2\": {\n";
    std::cout << "      \"selected\": \""
              << satcomfec::branch_metrics_sme2_selected_implementation() << "\",\n";
    std::cout << "      \"kernel_compiled\": "
              << (satcomfec::branch_metrics_sme2_kernel_compiled() ? "true" : "false")
              << "\n";
    std::cout << "    }\n";
    std::cout << "  },\n";
    std::cout << "  \"cases\": [\n";
    for (size_t i = 0; i < checks.size(); ++i) {
        const MetricCheck& check = checks[i];
        std::cout << "    {\n";
        std::cout << "      \"symbols\": " << check.symbol_count << ",\n";
        std::cout << "      \"neon_matches_reference\": "
                  << (check.neon_matches_reference ? "true" : "false") << ",\n";
        std::cout << "      \"sme2_matches_reference\": "
                  << (check.sme2_matches_reference ? "true" : "false") << "\n";
        std::cout << "    }" << ((i + 1) == checks.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n";
    std::cout << "}\n";

    return ok ? 0 : 1;
}
