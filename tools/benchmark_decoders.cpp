#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../app/src/main/cpp/demo/replay_pipeline.h"
#include "../app/src/main/cpp/fec/convolutional_codec.h"
#include "../app/src/main/cpp/fec/viterbi_decoder_neon.h"
#include "../app/src/main/cpp/fec/viterbi_decoder_sme2.h"

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

const char* compile_target_label() {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    return "arm-neon-capable";
#else
    return "non-neon";
#endif
}

struct PathBenchmark {
    std::string decoder_name;
    std::string implementation_class;
    std::string implementation_summary;
    double elapsed_ms = 0.0;
    double ns_per_iteration = 0.0;
    std::vector<uint8_t> decoded_bits;
    bool ok = false;
};

template <typename DecodeFn>
PathBenchmark run_path_benchmark(const satcomfec::PreparedReplayFrame& prepared,
                                 const satcomfec::ImplementationInfo& info,
                                 int warmup_iterations,
                                 int timed_iterations,
                                 DecodeFn decode_fn) {
    PathBenchmark benchmark;
    benchmark.decoder_name = info.path_name;
    benchmark.implementation_class =
        satcomfec::implementation_class_label(info.implementation_class);
    benchmark.implementation_summary = info.summary;

    std::vector<uint8_t> scratch_bits;
    for (int i = 0; i < warmup_iterations; ++i) {
        if (!decode_fn(prepared.frame_soft_bits, scratch_bits)) {
            return benchmark;
        }
    }

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < timed_iterations; ++i) {
        if (!decode_fn(prepared.frame_soft_bits, scratch_bits)) {
            return benchmark;
        }
    }
    const auto stop = std::chrono::steady_clock::now();

    benchmark.decoded_bits = scratch_bits;
    benchmark.ok = true;
    benchmark.elapsed_ms =
        std::chrono::duration<double, std::milli>(stop - start).count();
    benchmark.ns_per_iteration =
        std::chrono::duration<double, std::nano>(stop - start).count() /
        static_cast<double>(timed_iterations);
    return benchmark;
}

}  // namespace

int main(int argc, char** argv) {
    std::string iq_path = "data/synthetic/canned_replay/demo_conv_bpsk.iq";
    int warmup_iterations = 10;
    int timed_iterations = 500;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iq" && (i + 1) < argc) {
            iq_path = argv[++i];
            continue;
        }
        if (arg == "--warmup" && (i + 1) < argc) {
            warmup_iterations = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "--iterations" && (i + 1) < argc) {
            timed_iterations = std::atoi(argv[++i]);
            continue;
        }
    }

    const satcomfec::ReplayConfig config {
        iq_path,
        satcomfec::ReplayDecoder::kViterbiNeon,
        8,
    };
    const satcomfec::PreparedReplayFrame prepared =
        satcomfec::prepare_demo_frame(config);
    if (!prepared.ok) {
        std::cerr << "Failed to prepare replay frame: " << prepared.error_message << "\n";
        return EXIT_FAILURE;
    }

    const PathBenchmark neon = run_path_benchmark(
        prepared,
        satcomfec::viterbi_neon_implementation_info(),
        warmup_iterations,
        timed_iterations,
        satcomfec::viterbi_decode_neon);
    const PathBenchmark sme2 = run_path_benchmark(
        prepared,
        satcomfec::viterbi_sme2_implementation_info(),
        warmup_iterations,
        timed_iterations,
        satcomfec::viterbi_decode_sme2);

    if (!neon.ok || !sme2.ok) {
        std::cerr << "One of the decoder paths failed during benchmark execution\n";
        return EXIT_FAILURE;
    }

    const bool outputs_match = (neon.decoded_bits == sme2.decoded_bits);
    const std::vector<uint8_t> decoded_bytes =
        satcomfec::bits_to_bytes(neon.decoded_bits);
    std::vector<uint8_t> payload_bytes(decoded_bytes.begin(), decoded_bytes.end());
    if (!payload_bytes.empty()) {
        payload_bytes.pop_back();
    }
    const std::string decoded_text =
        escape_json(satcomfec::bytes_to_ascii(payload_bytes));

    std::cout << "{\n";
    std::cout << "  \"ok\": " << ((neon.ok && sme2.ok) ? "true" : "false") << ",\n";
    std::cout << "  \"assumptions\": {\n";
    std::cout << "    \"iq_path\": \"" << escape_json(iq_path) << "\",\n";
    std::cout << "    \"samples_per_symbol\": 8,\n";
    std::cout << "    \"coded_bits_per_frame\": " << prepared.frame_soft_bits.size() << ",\n";
    std::cout << "    \"same_input_frame\": true,\n";
    std::cout << "    \"same_decoder_settings\": true,\n";
    std::cout << "    \"same_evaluation_window\": true,\n";
    std::cout << "    \"warmup_iterations\": " << warmup_iterations << ",\n";
    std::cout << "    \"timed_iterations\": " << timed_iterations << ",\n";
    std::cout << "    \"timer\": \"std::chrono::steady_clock\",\n";
    std::cout << "    \"compile_target\": \"" << compile_target_label() << "\"\n";
    std::cout << "  },\n";
    std::cout << "  \"paths\": [\n";
    std::cout << "    {\n";
    std::cout << "      \"decoder\": \"" << escape_json(neon.decoder_name) << "\",\n";
    std::cout << "      \"implementation_class\": \"" << escape_json(neon.implementation_class)
              << "\",\n";
    std::cout << "      \"implementation_summary\": \""
              << escape_json(neon.implementation_summary) << "\",\n";
    std::cout << "      \"elapsed_ms\": " << neon.elapsed_ms << ",\n";
    std::cout << "      \"ns_per_iteration\": " << neon.ns_per_iteration << "\n";
    std::cout << "    },\n";
    std::cout << "    {\n";
    std::cout << "      \"decoder\": \"" << escape_json(sme2.decoder_name) << "\",\n";
    std::cout << "      \"implementation_class\": \"" << escape_json(sme2.implementation_class)
              << "\",\n";
    std::cout << "      \"implementation_summary\": \""
              << escape_json(sme2.implementation_summary) << "\",\n";
    std::cout << "      \"elapsed_ms\": " << sme2.elapsed_ms << ",\n";
    std::cout << "      \"ns_per_iteration\": " << sme2.ns_per_iteration << "\n";
    std::cout << "    }\n";
    std::cout << "  ],\n";
    std::cout << "  \"outputs_match\": " << (outputs_match ? "true" : "false") << ",\n";
    std::cout << "  \"decoded_text\": \"" << decoded_text << "\"\n";
    std::cout << "}\n";

    return outputs_match ? EXIT_SUCCESS : EXIT_FAILURE;
}
