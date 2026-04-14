#include <cstdlib>
#include <iostream>
#include <string>

#include "../app/src/main/cpp/demo/replay_pipeline.h"

namespace {

std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped += ch;
                break;
        }
    }
    return escaped;
}

satcomfec::ReplayDecoder parse_decoder(const std::string& value) {
    if (value == "viterbi-sme2") {
        return satcomfec::ReplayDecoder::kViterbiSme2;
    }
    return satcomfec::ReplayDecoder::kViterbiNeon;
}

}  // namespace

int main(int argc, char** argv) {
    std::string iq_path = "data/synthetic/canned_replay/demo_conv_bpsk.iq";
    std::string decoder_name = "viterbi-neon";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iq" && (i + 1) < argc) {
            iq_path = argv[++i];
            continue;
        }
        if (arg == "--decoder" && (i + 1) < argc) {
            decoder_name = argv[++i];
            continue;
        }
        if (arg == "--help") {
            std::cout
                << "Usage: replay_demo [--iq path] [--decoder viterbi-neon|viterbi-sme2]\n";
            return 0;
        }
    }

    const satcomfec::ReplayResult result = satcomfec::run_demo_replay(
        satcomfec::ReplayConfig {
            iq_path,
            parse_decoder(decoder_name),
            8,
        });

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n";
    std::cout << "  \"decoder\": \"" << escape_json(result.decoder_name) << "\",\n";
    std::cout << "  \"implementation_class\": \"" << escape_json(result.implementation_class)
              << "\",\n";
    std::cout << "  \"implementation_summary\": \""
              << escape_json(result.implementation_summary) << "\",\n";
    std::cout << "  \"decoded_text\": \"" << escape_json(result.decoded_text) << "\",\n";
    std::cout << "  \"crc_ok\": " << (result.crc_ok ? "true" : "false") << ",\n";
    std::cout << "  \"sync_score\": " << result.sync_score << ",\n";
    std::cout << "  \"mean_abs_llr\": " << result.trust_features.mean_abs_llr << ",\n";
    std::cout << "  \"trust_score\": " << result.trust_score << ",\n";
    std::cout << "  \"error\": \"" << escape_json(result.error_message) << "\"\n";
    std::cout << "}\n";

    return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
