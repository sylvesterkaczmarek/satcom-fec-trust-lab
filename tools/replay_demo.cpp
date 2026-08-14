#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include "../src/demo/replay_pipeline.h"
#include "json_output.h"

namespace {

struct FixtureMetadata {
    satcomfec::ReplayAcquisitionGroundTruth ground_truth;
    double sample_rate_hz = 48000.0;
    std::size_t samples_per_symbol = 8;
    std::string preamble_path;
};

satcomfec::ReplayDecoder parse_decoder(const std::string& value) {
    if (value == "viterbi-sme2") {
        return satcomfec::ReplayDecoder::kViterbiSme2;
    }
    if (value == "viterbi-reference") {
        return satcomfec::ReplayDecoder::kViterbiReference;
    }
    return satcomfec::ReplayDecoder::kViterbiNeon;
}

satcomfec::acquisition::AcquisitionImplementation parse_acquisition(
    const std::string& value) {
    if (value == "neon") {
        return satcomfec::acquisition::AcquisitionImplementation::kNeon;
    }
    if (value == "sme2") {
        return satcomfec::acquisition::AcquisitionImplementation::kSme2;
    }
    return satcomfec::acquisition::AcquisitionImplementation::kReference;
}

bool read_text_file(const std::string& path, std::string& text) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    return input.good() || input.eof();
}

bool find_json_value(
    const std::string& document,
    const std::string& key,
    std::size_t& value_start) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_position = document.find(quoted_key);
    if (key_position == std::string::npos) {
        return false;
    }
    const std::size_t colon = document.find(':', key_position + quoted_key.size());
    if (colon == std::string::npos) {
        return false;
    }
    value_start = document.find_first_not_of(" \t\r\n", colon + 1);
    return value_start != std::string::npos;
}

bool parse_string_field(
    const std::string& document,
    const std::string& key,
    std::string& value) {
    std::size_t start = 0;
    if (!find_json_value(document, key, start) || document[start] != '"') {
        return false;
    }
    const std::size_t end = document.find('"', start + 1);
    if (end == std::string::npos) {
        return false;
    }
    value = document.substr(start + 1, end - start - 1);
    return true;
}

bool parse_bool_field(
    const std::string& document,
    const std::string& key,
    bool& value) {
    std::size_t start = 0;
    if (!find_json_value(document, key, start)) {
        return false;
    }
    if (document.compare(start, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (document.compare(start, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

bool parse_optional_double_field(
    const std::string& document,
    const std::string& key,
    bool& has_value,
    double& value) {
    std::size_t start = 0;
    if (!find_json_value(document, key, start)) {
        return false;
    }
    if (document.compare(start, 4, "null") == 0) {
        has_value = false;
        value = 0.0;
        return true;
    }
    char* end = nullptr;
    value = std::strtod(document.c_str() + start, &end);
    if (end == document.c_str() + start || !std::isfinite(value)) {
        return false;
    }
    has_value = true;
    return true;
}

bool parse_optional_size_field(
    const std::string& document,
    const std::string& key,
    bool& has_value,
    std::size_t& value) {
    std::size_t start = 0;
    if (!find_json_value(document, key, start)) {
        return false;
    }
    if (document.compare(start, 4, "null") == 0) {
        has_value = false;
        value = 0;
        return true;
    }
    if (document[start] == '-') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long parsed =
        std::strtoull(document.c_str() + start, &end, 10);
    if (end == document.c_str() + start ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    has_value = true;
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_required_double_field(
    const std::string& document,
    const std::string& key,
    double& value) {
    bool has_value = false;
    return parse_optional_double_field(document, key, has_value, value) && has_value;
}

bool parse_required_size_field(
    const std::string& document,
    const std::string& key,
    std::size_t& value) {
    bool has_value = false;
    return parse_optional_size_field(document, key, has_value, value) && has_value;
}

bool load_fixture_metadata(
    const std::string& path,
    FixtureMetadata& metadata,
    std::string& error_message) {
    std::string document;
    if (!read_text_file(path, document)) {
        error_message = "Failed to read replay metadata: " + path;
        return false;
    }

    std::string schema;
    std::string preamble_file;
    if (!parse_string_field(document, "schema", schema) ||
        schema != "satcom-fec-trust-lab/replay-fixture-v2" ||
        !parse_string_field(document, "scenario", metadata.ground_truth.scenario) ||
        !parse_bool_field(document, "signal_present", metadata.ground_truth.signal_present) ||
        !parse_optional_size_field(
            document,
            "true_timing_offset",
            metadata.ground_truth.has_timing_offset,
            metadata.ground_truth.timing_offset) ||
        !parse_optional_double_field(
            document,
            "true_cfo_hz",
            metadata.ground_truth.has_cfo_hz,
            metadata.ground_truth.cfo_hz) ||
        !parse_required_double_field(
            document, "sample_rate_hz", metadata.sample_rate_hz) ||
        !parse_required_size_field(
            document, "samples_per_symbol", metadata.samples_per_symbol) ||
        !parse_string_field(document, "preamble_file", preamble_file)) {
        error_message = "Replay metadata does not match the v2 fixture contract";
        return false;
    }
    if (metadata.ground_truth.signal_present &&
        (!metadata.ground_truth.has_timing_offset ||
         !metadata.ground_truth.has_cfo_hz)) {
        error_message = "Signal-bearing metadata is missing timing or CFO ground truth";
        return false;
    }

    metadata.ground_truth.available = true;
    metadata.preamble_path =
        (std::filesystem::path(path).parent_path() / preamble_file).string();
    return true;
}

std::string inferred_metadata_path(const std::string& iq_path) {
    std::filesystem::path path(iq_path);
    path.replace_extension(".json");
    return path.string();
}

void print_nullable_size(bool valid, std::size_t value) {
    if (valid) {
        std::cout << value;
    } else {
        std::cout << "null";
    }
}

void print_nullable_integer(bool valid, std::int64_t value) {
    if (valid) {
        std::cout << value;
    } else {
        std::cout << "null";
    }
}

void print_nullable_float(bool valid, double value) {
    if (valid) {
        std::cout << satcomfec::tools::format_float(value);
    } else {
        std::cout << "null";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string iq_path = "data/synthetic/canned_replay/demo_conv_bpsk.iq";
    std::string metadata_path;
    std::string preamble_path;
    std::string decoder_name = "viterbi-neon";
    std::string acquisition_name = "reference";
    bool metadata_explicit = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iq" && (i + 1) < argc) {
            iq_path = argv[++i];
            continue;
        }
        if (arg == "--metadata" && (i + 1) < argc) {
            metadata_path = argv[++i];
            metadata_explicit = true;
            continue;
        }
        if (arg == "--preamble" && (i + 1) < argc) {
            preamble_path = argv[++i];
            continue;
        }
        if (arg == "--decoder" && (i + 1) < argc) {
            decoder_name = argv[++i];
            continue;
        }
        if (arg == "--acquisition" && (i + 1) < argc) {
            acquisition_name = argv[++i];
            continue;
        }
        if (arg == "--help") {
            std::cout
                << "Usage: replay_demo [--iq path] [--metadata path] "
                   "[--preamble path] [--acquisition reference|neon|sme2] "
                   "[--decoder viterbi-neon|viterbi-sme2|viterbi-reference]\n";
            return 0;
        }
        std::cerr << "Unknown or incomplete argument: " << arg << "\n";
        return EXIT_FAILURE;
    }

    if (decoder_name != "viterbi-neon" && decoder_name != "viterbi-sme2" &&
        decoder_name != "viterbi-reference") {
        std::cerr << "Unsupported decoder: " << decoder_name << "\n";
        return EXIT_FAILURE;
    }
    if (acquisition_name != "reference" && acquisition_name != "neon" &&
        acquisition_name != "sme2") {
        std::cerr << "Unsupported acquisition implementation: "
                  << acquisition_name << "\n";
        return EXIT_FAILURE;
    }

    if (metadata_path.empty()) {
        const std::string inferred = inferred_metadata_path(iq_path);
        if (std::filesystem::exists(inferred)) {
            metadata_path = inferred;
        }
    }

    FixtureMetadata metadata;
    if (!metadata_path.empty()) {
        std::string metadata_error;
        if (!load_fixture_metadata(metadata_path, metadata, metadata_error)) {
            if (metadata_explicit) {
                std::cerr << metadata_error << "\n";
                return EXIT_FAILURE;
            }
            metadata_path.clear();
        }
    }
    if (preamble_path.empty()) {
        preamble_path = metadata.preamble_path.empty()
                            ? "data/synthetic/canned_replay/preamble_qpsk_256.iq"
                            : metadata.preamble_path;
    }

    satcomfec::ReplayConfig config;
    config.iq_path = iq_path;
    config.preamble_iq_path = preamble_path;
    config.decoder = parse_decoder(decoder_name);
    config.samples_per_symbol = metadata.samples_per_symbol;
    config.acquisition.sample_rate_hz = metadata.sample_rate_hz;
    config.acquisition.implementation = parse_acquisition(acquisition_name);
    config.acquisition.cfo_hypotheses_hz = {
        -500.0, -250.0, 0.0, 250.0, 500.0,
    };
    config.ground_truth = metadata.ground_truth;

    const satcomfec::ReplayResult result = satcomfec::run_demo_replay(config);

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n";
    std::cout << "  \"iq_path\": \""
              << satcomfec::tools::escape_json(result.iq_path) << "\",\n";
    std::cout << "  \"metadata_path\": \""
              << satcomfec::tools::escape_json(metadata_path) << "\",\n";
    std::cout << "  \"decoder\": \""
              << satcomfec::tools::escape_json(result.decoder_name) << "\",\n";
    std::cout << "  \"implementation_class\": \""
              << satcomfec::tools::escape_json(result.implementation_class)
              << "\",\n";
    std::cout << "  \"branch_metric_implementation\": \""
              << satcomfec::tools::escape_json(
                     result.branch_metric_implementation)
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

    const satcomfec::ReplayAcquisitionDiagnostics& acquisition = result.acquisition;
    std::cout << "  \"acquisition\": {\n";
    std::cout << "    \"requested_implementation\": \""
              << satcomfec::tools::escape_json(acquisition.requested_implementation)
              << "\",\n";
    std::cout << "    \"selected_implementation\": \""
              << satcomfec::tools::escape_json(acquisition.selected_implementation)
              << "\",\n";
    std::cout << "    \"search_completed\": "
              << (acquisition.search_completed ? "true" : "false") << ",\n";
    std::cout << "    \"acquisition_success\": "
              << (acquisition.accepted ? "true" : "false") << ",\n";
    std::cout << "    \"sample_count\": " << acquisition.sample_count << ",\n";
    std::cout << "    \"preamble_length\": " << acquisition.preamble_length << ",\n";
    std::cout << "    \"timing_hypothesis_count\": "
              << acquisition.timing_hypothesis_count << ",\n";
    std::cout << "    \"cfo_hypothesis_count\": "
              << acquisition.cfo_hypothesis_count << ",\n";
    std::cout << "    \"evaluated_candidate_count\": "
              << acquisition.evaluated_candidate_count << ",\n";
    std::cout << "    \"detected_timing_offset\": ";
    print_nullable_size(
        acquisition.detected_candidate_valid,
        acquisition.detected_timing_offset);
    std::cout << ",\n";
    std::cout << "    \"detected_cfo_hz\": ";
    print_nullable_float(
        acquisition.detected_candidate_valid,
        acquisition.detected_cfo_hz);
    std::cout << ",\n";
    std::cout << "    \"correlation_score\": "
              << satcomfec::tools::format_float(acquisition.correlation_score)
              << ",\n";
    std::cout << "    \"second_best_score\": "
              << satcomfec::tools::format_float(acquisition.second_best_score)
              << ",\n";
    std::cout << "    \"peak_ratio\": "
              << satcomfec::tools::format_float(acquisition.peak_ratio) << ",\n";
    std::cout << "    \"normalized_peak\": "
              << satcomfec::tools::format_float(acquisition.normalized_peak)
              << ",\n";
    std::cout << "    \"second_best_normalized_peak\": "
              << satcomfec::tools::format_float(
                     acquisition.second_best_normalized_peak)
              << ",\n";
    std::cout << "    \"normalized_peak_separation\": "
              << satcomfec::tools::format_float(
                     acquisition.normalized_peak_separation)
              << ",\n";
    std::cout << "    \"confidence\": "
              << satcomfec::tools::format_float(acquisition.confidence) << ",\n";
    std::cout << "    \"confidence_calibrated\": false,\n";
    std::cout << "    \"minimum_normalized_peak\": "
              << satcomfec::tools::format_float(
                     acquisition.minimum_normalized_peak)
              << ",\n";
    std::cout << "    \"minimum_peak_separation\": "
              << satcomfec::tools::format_float(
                     acquisition.minimum_peak_separation)
              << ",\n";
    std::cout << "    \"ground_truth\": {\n";
    std::cout << "      \"available\": "
              << (acquisition.ground_truth.available ? "true" : "false") << ",\n";
    std::cout << "      \"scenario\": \""
              << satcomfec::tools::escape_json(
                     acquisition.ground_truth.scenario)
              << "\",\n";
    std::cout << "      \"signal_present\": "
              << (acquisition.ground_truth.signal_present ? "true" : "false")
              << ",\n";
    std::cout << "      \"timing_offset\": ";
    print_nullable_size(
        acquisition.ground_truth.has_timing_offset,
        acquisition.ground_truth.timing_offset);
    std::cout << ",\n";
    std::cout << "      \"cfo_hz\": ";
    print_nullable_float(
        acquisition.ground_truth.has_cfo_hz,
        acquisition.ground_truth.cfo_hz);
    std::cout << ",\n";
    std::cout << "      \"timing_error_samples\": ";
    print_nullable_integer(
        acquisition.has_timing_error,
        acquisition.timing_error_samples);
    std::cout << ",\n";
    std::cout << "      \"cfo_hypothesis_error_hz\": ";
    print_nullable_float(
        acquisition.has_cfo_hypothesis_error,
        acquisition.cfo_hypothesis_error_hz);
    std::cout << "\n    }\n";
    std::cout << "  },\n";

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
              << "\n  },\n";
    std::cout << "  \"demod\": {\n";
    std::cout << "    \"symbol_count\": " << result.demod_stats.symbol_count << ",\n";
    std::cout << "    \"samples_per_symbol\": " << result.demod_stats.samples_per_symbol
              << ",\n";
    std::cout << "    \"max_abs_symbol_mean\": "
              << satcomfec::tools::format_float(result.demod_stats.max_abs_symbol_mean)
              << ",\n";
    std::cout << "    \"clipped_symbol_count\": "
              << result.demod_stats.clipped_symbol_count << "\n  },\n";
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
              << result.frame.second_best_correlation_score << "\n  },\n";
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
    std::cout << "    \"normalized_acquisition_peak\": "
              << satcomfec::tools::format_float(
                     result.trust_features.normalized_acquisition_peak)
              << ",\n";
    std::cout << "    \"acquisition_peak_separation\": "
              << satcomfec::tools::format_float(
                     result.trust_features.acquisition_peak_separation)
              << ",\n";
    std::cout << "    \"timing_ambiguity\": "
              << satcomfec::tools::format_float(result.trust_features.timing_ambiguity)
              << ",\n";
    std::cout << "    \"residual_acquisition_uncertainty\": "
              << satcomfec::tools::format_float(
                     result.trust_features.residual_acquisition_uncertainty)
              << ",\n";
    std::cout << "    \"acquisition_accepted\": "
              << satcomfec::tools::format_float(result.trust_features.acquisition_accepted)
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
    std::cout << "    \"crc_evaluated\": "
              << satcomfec::tools::format_float(result.trust_features.crc_evaluated)
              << ",\n";
    std::cout << "    \"crc_pass\": "
              << satcomfec::tools::format_float(result.trust_features.crc_pass)
              << "\n  },\n";
    std::cout << "  \"trust_breakdown\": {\n";
    std::cout << "    \"llr_strength\": "
              << satcomfec::tools::format_float(result.trust_breakdown.llr_strength)
              << ",\n";
    std::cout << "    \"llr_consistency\": "
              << satcomfec::tools::format_float(result.trust_breakdown.llr_consistency)
              << ",\n";
    std::cout << "    \"acquisition_strength\": "
              << satcomfec::tools::format_float(
                     result.trust_breakdown.acquisition_strength)
              << ",\n";
    std::cout << "    \"acquisition_separation\": "
              << satcomfec::tools::format_float(
                     result.trust_breakdown.acquisition_separation)
              << ",\n";
    std::cout << "    \"acquisition_certainty\": "
              << satcomfec::tools::format_float(
                     result.trust_breakdown.acquisition_certainty)
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
    std::cout << "    \"capped_by_acquisition_rejection\": "
              << (result.trust_breakdown.capped_by_acquisition_rejection
                      ? "true"
                      : "false")
              << ",\n";
    std::cout << "    \"capped_by_crc_failure\": "
              << (result.trust_breakdown.capped_by_crc_failure ? "true" : "false")
              << ",\n";
    std::cout << "    \"score\": "
              << satcomfec::tools::format_float(result.trust_breakdown.score)
              << "\n  },\n";
    std::cout << "  \"trust_assessment\": {\n";
    std::cout << "    \"band\": \""
              << satcomfec::tools::escape_json(result.trust_assessment.band) << "\",\n";
    std::cout << "    \"weak_soft_bits\": "
              << (result.trust_assessment.weak_soft_bits ? "true" : "false") << ",\n";
    std::cout << "    \"ambiguous_acquisition\": "
              << (result.trust_assessment.ambiguous_acquisition ? "true" : "false")
              << ",\n";
    std::cout << "    \"acquisition_rejected\": "
              << (result.trust_assessment.acquisition_rejected ? "true" : "false")
              << ",\n";
    std::cout << "    \"ambiguous_sync\": "
              << (result.trust_assessment.ambiguous_sync ? "true" : "false") << ",\n";
    std::cout << "    \"demod_clipping\": "
              << (result.trust_assessment.demod_clipping ? "true" : "false") << ",\n";
    std::cout << "    \"crc_not_evaluated\": "
              << (result.trust_assessment.crc_not_evaluated ? "true" : "false")
              << ",\n";
    std::cout << "    \"crc_failed\": "
              << (result.trust_assessment.crc_failed ? "true" : "false") << "\n  },\n";
    std::cout << "  \"trust_score\": "
              << satcomfec::tools::format_float(result.trust_score) << ",\n";
    std::cout << "  \"error\": \""
              << satcomfec::tools::escape_json(result.error_message) << "\"\n";
    std::cout << "}\n";

    return result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
