#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/demo/replay_pipeline.h"
#include "../src/fec/branch_metrics_sme2.h"
#include "../src/fec/convolutional_codec.h"
#include "../src/fec/viterbi_decoder_neon.h"
#include "../src/fec/viterbi_decoder_reference.h"
#include "../src/fec/viterbi_decoder_sme2.h"
#include "json_output.h"

namespace {

const char* compile_target_label() {
#if defined(__ARM_FEATURE_SME2)
    return "arm-sme2-capable";
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    return "arm-neon-capable";
#else
    return "non-neon";
#endif
}

struct PathBenchmark {
    std::string decoder_name;
    std::string implementation_class;
    std::string implementation_summary;
    std::string branch_metric_selected;
    double branch_metric_elapsed_ms = 0.0;
    double branch_metric_ns_per_iteration = 0.0;
    uint32_t branch_metric_checksum = 0;
    double full_decode_elapsed_ms = 0.0;
    double full_decode_ns_per_iteration = 0.0;
    size_t decoded_bit_count = 0;
    uint32_t decoded_bit_checksum = 0;
    std::vector<uint8_t> decoded_bits;
    bool ok = false;
};

uint32_t fnv1a_checksum(const std::vector<uint8_t>& values) {
    uint32_t hash = 2166136261u;
    for (uint8_t value : values) {
        hash ^= static_cast<uint32_t>(value);
        hash *= 16777619u;
    }
    return hash;
}

uint32_t checksum_metric_tables(const satcomfec::BranchMetricTables& tables) {
    uint32_t hash = 2166136261u;
    hash ^= static_cast<uint32_t>(tables.symbol_count);
    hash *= 16777619u;
    for (const auto& metric_vector : tables.metric_by_symbol_type) {
        for (int16_t value : metric_vector) {
            hash ^= static_cast<uint32_t>(static_cast<uint16_t>(value));
            hash *= 16777619u;
        }
    }
    return hash;
}

template <typename PrepareMetricsFn, typename DecodeFn>
PathBenchmark run_path_benchmark(const satcomfec::PreparedReplayFrame& prepared,
                                 const satcomfec::ImplementationInfo& info,
                                 const std::string& branch_metric_selected,
                                 int warmup_iterations,
                                 int timed_iterations,
                                 PrepareMetricsFn prepare_metrics_fn,
                                 DecodeFn decode_fn) {
    PathBenchmark benchmark;
    benchmark.decoder_name = info.path_name;
    benchmark.implementation_class =
        satcomfec::implementation_class_label(info.implementation_class);
    benchmark.implementation_summary = info.summary;
    benchmark.branch_metric_selected = branch_metric_selected;

    satcomfec::BranchMetricTables scratch_tables;
    for (int i = 0; i < warmup_iterations; ++i) {
        if (!prepare_metrics_fn(prepared.frame_soft_bits, scratch_tables)) {
            return benchmark;
        }
    }

    const auto branch_start = std::chrono::steady_clock::now();
    for (int i = 0; i < timed_iterations; ++i) {
        if (!prepare_metrics_fn(prepared.frame_soft_bits, scratch_tables)) {
            return benchmark;
        }
    }
    const auto branch_stop = std::chrono::steady_clock::now();

    benchmark.branch_metric_checksum = checksum_metric_tables(scratch_tables);
    benchmark.branch_metric_elapsed_ms =
        std::chrono::duration<double, std::milli>(branch_stop - branch_start).count();
    benchmark.branch_metric_ns_per_iteration =
        std::chrono::duration<double, std::nano>(branch_stop - branch_start).count() /
        static_cast<double>(timed_iterations);

    std::vector<uint8_t> scratch_bits;
    for (int i = 0; i < warmup_iterations; ++i) {
        if (!decode_fn(prepared.frame_soft_bits, scratch_bits)) {
            return benchmark;
        }
    }

    const auto decode_start = std::chrono::steady_clock::now();
    for (int i = 0; i < timed_iterations; ++i) {
        if (!decode_fn(prepared.frame_soft_bits, scratch_bits)) {
            return benchmark;
        }
    }
    const auto decode_stop = std::chrono::steady_clock::now();

    benchmark.decoded_bits = scratch_bits;
    benchmark.ok = true;
    benchmark.decoded_bit_count = scratch_bits.size();
    benchmark.decoded_bit_checksum = fnv1a_checksum(scratch_bits);
    benchmark.full_decode_elapsed_ms =
        std::chrono::duration<double, std::milli>(decode_stop - decode_start).count();
    benchmark.full_decode_ns_per_iteration =
        std::chrono::duration<double, std::nano>(decode_stop - decode_start).count() /
        static_cast<double>(timed_iterations);
    return benchmark;
}

void print_path_json(const PathBenchmark& path, bool trailing_comma) {
    std::cout << "    {\n";
    std::cout << "      \"decoder\": \""
              << satcomfec::tools::escape_json(path.decoder_name) << "\",\n";
    std::cout << "      \"implementation_class\": \""
              << satcomfec::tools::escape_json(path.implementation_class)
              << "\",\n";
    std::cout << "      \"implementation_summary\": \""
              << satcomfec::tools::escape_json(path.implementation_summary) << "\",\n";
    std::cout << "      \"branch_metric\": {\n";
    std::cout << "        \"selected_implementation\": \""
              << satcomfec::tools::escape_json(path.branch_metric_selected) << "\",\n";
    std::cout << "        \"elapsed_ms\": "
              << satcomfec::tools::format_float(path.branch_metric_elapsed_ms, 6)
              << ",\n";
    std::cout << "        \"ns_per_iteration\": "
              << satcomfec::tools::format_float(path.branch_metric_ns_per_iteration, 3)
              << ",\n";
    std::cout << "        \"metric_checksum\": " << path.branch_metric_checksum << "\n";
    std::cout << "      },\n";
    std::cout << "      \"full_decode\": {\n";
    std::cout << "        \"elapsed_ms\": "
              << satcomfec::tools::format_float(path.full_decode_elapsed_ms, 6)
              << ",\n";
    std::cout << "        \"ns_per_iteration\": "
              << satcomfec::tools::format_float(path.full_decode_ns_per_iteration, 3)
              << "\n";
    std::cout << "      },\n";
    std::cout << "      \"decode_ok\": " << (path.ok ? "true" : "false") << ",\n";
    std::cout << "      \"decoded_bit_count\": " << path.decoded_bit_count << ",\n";
    std::cout << "      \"decoded_bit_checksum\": " << path.decoded_bit_checksum << "\n";
    std::cout << "    }" << (trailing_comma ? "," : "") << "\n";
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
        if (arg == "--help") {
            std::cout
                << "Usage: benchmark_decoders [--iq path] [--warmup N] [--iterations N]\n";
            return 0;
        }
        std::cerr << "Unknown or incomplete argument: " << arg << "\n";
        return EXIT_FAILURE;
    }

    if (warmup_iterations < 0 || timed_iterations <= 0) {
        std::cerr << "warmup must be non-negative and iterations must be positive\n";
        return EXIT_FAILURE;
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
        satcomfec::branch_metrics_neon_selected_implementation(),
        warmup_iterations,
        timed_iterations,
        satcomfec::prepare_branch_metrics_neon,
        satcomfec::viterbi_decode_neon);
    const PathBenchmark reference = run_path_benchmark(
        prepared,
        satcomfec::viterbi_reference_implementation_info(),
        "reference",
        warmup_iterations,
        timed_iterations,
        satcomfec::prepare_branch_metrics_reference,
        satcomfec::viterbi_decode_reference_path);
    const PathBenchmark sme2 = run_path_benchmark(
        prepared,
        satcomfec::viterbi_sme2_implementation_info(),
        satcomfec::branch_metrics_sme2_selected_implementation(),
        warmup_iterations,
        timed_iterations,
        satcomfec::prepare_branch_metrics_sme2,
        satcomfec::viterbi_decode_sme2);

    if (!neon.ok || !reference.ok || !sme2.ok) {
        std::cerr << "One of the decoder paths failed during benchmark execution\n";
        return EXIT_FAILURE;
    }

    const bool outputs_match = (neon.decoded_bits == reference.decoded_bits) &&
                               (sme2.decoded_bits == reference.decoded_bits);
    const bool decoded_bit_count_match =
        (neon.decoded_bit_count == reference.decoded_bit_count) &&
        (sme2.decoded_bit_count == reference.decoded_bit_count);
    const bool decoded_bit_checksum_match =
        (neon.decoded_bit_checksum == reference.decoded_bit_checksum) &&
        (sme2.decoded_bit_checksum == reference.decoded_bit_checksum);
    const std::vector<uint8_t> decoded_bytes =
        satcomfec::bits_to_bytes(neon.decoded_bits);
    std::vector<uint8_t> payload_bytes(decoded_bytes.begin(), decoded_bytes.end());
    if (!payload_bytes.empty()) {
        payload_bytes.pop_back();
    }
    const std::string decoded_text =
        satcomfec::tools::escape_json(satcomfec::bytes_to_ascii(payload_bytes));
    const std::vector<uint8_t> prepared_frame_bytes(
        prepared.frame_soft_bits.begin(), prepared.frame_soft_bits.end());
    const uint32_t prepared_frame_checksum = fnv1a_checksum(prepared_frame_bytes);

    std::cout << "{\n";
    std::cout << "  \"ok\": "
              << ((neon.ok && reference.ok && sme2.ok) ? "true" : "false") << ",\n";
    std::cout << "  \"benchmark\": {\n";
    std::cout << "    \"kind\": \"decoder-path-local-timing\",\n";
    std::cout << "    \"scope\": \"Same prepared replay frame and decoder settings; reports branch-metric preparation timing separately from full decode timing. Viterbi add-compare-select and traceback remain scalar in all paths; local timing only.\",\n";
    std::cout << "    \"local_timing_only\": true,\n";
    std::cout << "    \"warmup_iterations\": " << warmup_iterations << ",\n";
    std::cout << "    \"timed_iterations\": " << timed_iterations << ",\n";
    std::cout << "    \"timer\": \"std::chrono::steady_clock\",\n";
    std::cout << "    \"compile_target\": \"" << compile_target_label() << "\"\n";
    std::cout << "  },\n";
    std::cout << "  \"input\": {\n";
    std::cout << "    \"iq_path\": \"" << satcomfec::tools::escape_json(iq_path) << "\",\n";
    std::cout << "    \"decoder_settings\": {\n";
    std::cout << "      \"samples_per_symbol\": 8,\n";
    std::cout << "      \"coded_bits_per_frame\": " << prepared.frame_soft_bits.size() << "\n";
    std::cout << "    }\n";
    std::cout << "  },\n";
    std::cout << "  \"prepared_frame\": {\n";
    std::cout << "    \"sample_count\": " << prepared.front_end_stats.sample_count << ",\n";
    std::cout << "    \"demodulated_symbols\": " << prepared.demod_stats.symbol_count << ",\n";
    std::cout << "    \"frame_start_index\": " << prepared.frame.start_index << ",\n";
    std::cout << "    \"frame_length\": " << prepared.frame.length << ",\n";
    std::cout << "    \"sync_score\": " << prepared.frame.correlation_score << ",\n";
    std::cout << "    \"soft_bit_checksum\": " << prepared_frame_checksum << "\n";
    std::cout << "  },\n";
    std::cout << "  \"assumptions\": {\n";
    std::cout << "    \"samples_per_symbol\": 8,\n";
    std::cout << "    \"coded_bits_per_frame\": " << prepared.frame_soft_bits.size() << ",\n";
    std::cout << "    \"same_input_frame\": true,\n";
    std::cout << "    \"same_decoder_settings\": true,\n";
    std::cout << "    \"same_evaluation_window\": true,\n";
    std::cout << "    \"same_traceback_core\": true,\n";
    std::cout << "    \"same_state_machine\": true,\n";
    std::cout << "    \"branch_metric_timed_separately\": true,\n";
    std::cout << "    \"full_decode_includes_branch_metric_preparation\": true,\n";
    std::cout << "    \"same_prepared_soft_bits\": true\n";
    std::cout << "  },\n";
    std::cout << "  \"paths\": [\n";
    print_path_json(neon, true);
    print_path_json(reference, true);
    print_path_json(sme2, false);
    std::cout << "  ],\n";
    std::cout << "  \"alignment\": {\n";
    std::cout << "    \"decoded_bit_count_match\": "
              << (decoded_bit_count_match ? "true" : "false") << ",\n";
    std::cout << "    \"decoded_bit_checksum_match\": "
              << (decoded_bit_checksum_match ? "true" : "false") << ",\n";
    std::cout << "    \"payload_text_match\": " << (outputs_match ? "true" : "false") << "\n";
    std::cout << "  },\n";
    std::cout << "  \"outputs_match\": " << (outputs_match ? "true" : "false") << ",\n";
    std::cout << "  \"decoded_text\": \"" << decoded_text << "\"\n";
    std::cout << "}\n";

    return outputs_match ? EXIT_SUCCESS : EXIT_FAILURE;
}
