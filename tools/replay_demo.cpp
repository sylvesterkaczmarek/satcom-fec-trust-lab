#include <cstdlib>
#include <iostream>
#include <string>

#include "../src/demo/replay_pipeline.h"
#include "json_output.h"

namespace {

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
        std::cerr << "Unknown or incomplete argument: " << arg << "\n";
        return EXIT_FAILURE;
    }

    const satcomfec::ReplayResult result = satcomfec::run_demo_replay(
        satcomfec::ReplayConfig {
            iq_path,
            parse_decoder(decoder_name),
            8,
        });

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n";
    std::cout << "  \"iq_path\": \""
              << satcomfec::tools::escape_json(result.iq_path) << "\",\n";
    std::cout << "  \"decoder\": \""
              << satcomfec::tools::escape_json(result.decoder_name) << "\",\n";
    std::cout << "  \"implementation_class\": \""
              << satcomfec::tools::escape_json(result.implementation_class)
              << "\",\n";
    std::cout << "  \"implementation_summary\": \""
              << satcomfec::tools::escape_json(result.implementation_summary) << "\",\n";
    std::cout << "  \"samples_per_symbol\": " << result.samples_per_symbol << ",\n";
    std::cout << "  \"frame_soft_bits\": " << result.frame_soft_bits << ",\n";
    std::cout << "  \"expected_payload_bytes\": " << result.expected_payload_bytes << ",\n";
    std::cout << "  \"decoded_payload_bytes\": " << result.decoded_payload_bytes << ",\n";
    std::cout << "  \"decoded_text\": \""
              << satcomfec::tools::escape_json(result.decoded_text) << "\",\n";
    std::cout << "  \"crc_ok\": " << (result.crc_ok ? "true" : "false") << ",\n";
    std::cout << "  \"front_end\": {\n";
    std::cout << "    \"sample_count\": " << result.front_end_stats.sample_count << ",\n";
    std::cout << "    \"dc_i\": " << satcomfec::tools::format_float(result.front_end_stats.dc_i)
              << ",\n";
    std::cout << "    \"dc_q\": " << satcomfec::tools::format_float(result.front_end_stats.dc_q)
              << ",\n";
    std::cout << "    \"rms_before_normalization\": "
              << satcomfec::tools::format_float(
                     result.front_end_stats.rms_before_normalization)
              << ",\n";
    std::cout << "    \"rms_after_normalization\": "
              << satcomfec::tools::format_float(
                     result.front_end_stats.rms_after_normalization)
              << "\n";
    std::cout << "  },\n";
    std::cout << "  \"demod\": {\n";
    std::cout << "    \"symbol_count\": " << result.demod_stats.symbol_count << ",\n";
    std::cout << "    \"samples_per_symbol\": " << result.demod_stats.samples_per_symbol
              << ",\n";
    std::cout << "    \"max_abs_symbol_mean\": "
              << satcomfec::tools::format_float(result.demod_stats.max_abs_symbol_mean)
              << ",\n";
    std::cout << "    \"clipped_symbol_count\": "
              << result.demod_stats.clipped_symbol_count << "\n";
    std::cout << "  },\n";
    std::cout << "  \"framing\": {\n";
    std::cout << "    \"sync_start_index\": " << result.frame.sync_start_index << ",\n";
    std::cout << "    \"frame_start_index\": " << result.frame.start_index << ",\n";
    std::cout << "    \"frame_length\": " << result.frame.length << ",\n";
    std::cout << "    \"sync_score\": " << result.frame.correlation_score << ",\n";
    std::cout << "    \"has_second_best_correlation\": "
              << (result.frame.has_second_best_correlation ? "true" : "false") << ",\n";
    std::cout << "    \"second_best_sync_start_index\": "
              << result.frame.second_best_sync_start_index << ",\n";
    std::cout << "    \"second_best_sync_score\": "
              << result.frame.second_best_correlation_score << "\n";
    std::cout << "  },\n";
    std::cout << "  \"trust_features\": {\n";
    std::cout << "    \"mean_abs_llr\": "
              << satcomfec::tools::format_float(result.trust_features.mean_abs_llr, 3)
              << ",\n";
    std::cout << "    \"normalized_mean_abs_llr\": "
              << satcomfec::tools::format_float(
                     result.trust_features.normalized_mean_abs_llr)
              << ",\n";
    std::cout << "    \"weak_llr_fraction\": "
              << satcomfec::tools::format_float(result.trust_features.weak_llr_fraction)
              << ",\n";
    std::cout << "    \"normalized_sync_score\": "
              << satcomfec::tools::format_float(
                     result.trust_features.normalized_sync_score)
              << ",\n";
    std::cout << "    \"normalized_sync_margin\": "
              << satcomfec::tools::format_float(
                     result.trust_features.normalized_sync_margin)
              << ",\n";
    std::cout << "    \"clipped_symbol_fraction\": "
              << satcomfec::tools::format_float(
                     result.trust_features.clipped_symbol_fraction)
              << ",\n";
    std::cout << "    \"crc_pass\": "
              << satcomfec::tools::format_float(result.trust_features.crc_pass)
              << "\n";
    std::cout << "  },\n";
    std::cout << "  \"trust_breakdown\": {\n";
    std::cout << "    \"llr_strength\": "
              << satcomfec::tools::format_float(result.trust_breakdown.llr_strength)
              << ",\n";
    std::cout << "    \"llr_consistency\": "
              << satcomfec::tools::format_float(result.trust_breakdown.llr_consistency)
              << ",\n";
    std::cout << "    \"sync_quality\": "
              << satcomfec::tools::format_float(result.trust_breakdown.sync_quality)
              << ",\n";
    std::cout << "    \"sync_margin_quality\": "
              << satcomfec::tools::format_float(
                     result.trust_breakdown.sync_margin_quality)
              << ",\n";
    std::cout << "    \"demod_quality\": "
              << satcomfec::tools::format_float(result.trust_breakdown.demod_quality)
              << ",\n";
    std::cout << "    \"crc_quality\": "
              << satcomfec::tools::format_float(result.trust_breakdown.crc_quality)
              << ",\n";
    std::cout << "    \"capped_by_crc_failure\": "
              << (result.trust_breakdown.capped_by_crc_failure ? "true" : "false")
              << ",\n";
    std::cout << "    \"score\": "
              << satcomfec::tools::format_float(result.trust_breakdown.score)
              << "\n";
    std::cout << "  },\n";
    std::cout << "  \"trust_assessment\": {\n";
    std::cout << "    \"band\": \""
              << satcomfec::tools::escape_json(result.trust_assessment.band) << "\",\n";
    std::cout << "    \"weak_soft_bits\": "
              << (result.trust_assessment.weak_soft_bits ? "true" : "false") << ",\n";
    std::cout << "    \"ambiguous_sync\": "
              << (result.trust_assessment.ambiguous_sync ? "true" : "false") << ",\n";
    std::cout << "    \"demod_clipping\": "
              << (result.trust_assessment.demod_clipping ? "true" : "false") << ",\n";
    std::cout << "    \"crc_failed\": "
              << (result.trust_assessment.crc_failed ? "true" : "false") << "\n";
    std::cout << "  },\n";
    std::cout << "  \"trust_score\": "
              << satcomfec::tools::format_float(result.trust_score) << ",\n";
    std::cout << "  \"error\": \""
              << satcomfec::tools::escape_json(result.error_message) << "\"\n";
    std::cout << "}\n";

    return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
