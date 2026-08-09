#include "acquisition/acquisition_reference.h"
#include "json_output.h"
#include "util/iq_reader.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct FixtureMetadata {
    std::string scenario;
    std::string preamble_file;
    std::size_t sample_count = 0;
    std::size_t preamble_length = 0;
    double sample_rate_hz = 0.0;
    std::size_t timing_search_start = 0;
    std::size_t timing_search_stop_inclusive = 0;
    std::size_t timing_search_step = 0;
    std::vector<double> cfo_hypotheses_hz;
    std::size_t true_timing_offset = 0;
    double true_cfo_hz = 0.0;
};

bool read_text_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    return input.good() || input.eof();
}

bool find_json_value(const std::string& json, const std::string& key, std::size_t& position) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_position = json.find(quoted_key);
    if (key_position == std::string::npos) {
        return false;
    }
    const std::size_t colon_position = json.find(':', key_position + quoted_key.size());
    if (colon_position == std::string::npos) {
        return false;
    }
    position = json.find_first_not_of(" \t\r\n", colon_position + 1);
    return position != std::string::npos;
}

bool parse_json_string(
    const std::string& json,
    const std::string& key,
    std::string& value) {
    std::size_t position = 0;
    if (!find_json_value(json, key, position) || json[position] != '"') {
        return false;
    }
    const std::size_t end = json.find('"', position + 1);
    if (end == std::string::npos) {
        return false;
    }
    value = json.substr(position + 1, end - position - 1);
    return true;
}

bool parse_json_number(
    const std::string& json,
    const std::string& key,
    double& value) {
    std::size_t position = 0;
    if (!find_json_value(json, key, position)) {
        return false;
    }
    const char* begin = json.c_str() + position;
    char* end = nullptr;
    errno = 0;
    value = std::strtod(begin, &end);
    return end != begin && errno != ERANGE && std::isfinite(value);
}

bool parse_json_size(
    const std::string& json,
    const std::string& key,
    std::size_t& value) {
    double parsed = 0.0;
    if (!parse_json_number(json, key, parsed) || parsed < 0.0 ||
        parsed > static_cast<double>(std::numeric_limits<std::size_t>::max()) ||
        std::floor(parsed) != parsed) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_json_number_array(
    const std::string& json,
    const std::string& key,
    std::vector<double>& values) {
    std::size_t position = 0;
    if (!find_json_value(json, key, position) || json[position] != '[') {
        return false;
    }
    const std::size_t end_position = json.find(']', position + 1);
    if (end_position == std::string::npos) {
        return false;
    }

    values.clear();
    ++position;
    while (position < end_position) {
        position = json.find_first_not_of(" \t\r\n,", position);
        if (position == std::string::npos || position >= end_position) {
            break;
        }
        const char* begin = json.c_str() + position;
        char* number_end = nullptr;
        errno = 0;
        const double parsed = std::strtod(begin, &number_end);
        if (number_end == begin || errno == ERANGE || !std::isfinite(parsed)) {
            return false;
        }
        position = static_cast<std::size_t>(number_end - json.c_str());
        values.push_back(parsed);
    }
    return !values.empty();
}

bool load_fixture_metadata(
    const std::filesystem::path& path,
    FixtureMetadata& metadata,
    std::string& error_message) {
    std::string json;
    if (!read_text_file(path, json)) {
        error_message = "failed to read fixture metadata: " + path.string();
        return false;
    }

    const bool complete =
        parse_json_string(json, "scenario", metadata.scenario) &&
        parse_json_string(json, "preamble_file", metadata.preamble_file) &&
        parse_json_size(json, "sample_count", metadata.sample_count) &&
        parse_json_size(json, "preamble_length", metadata.preamble_length) &&
        parse_json_number(json, "sample_rate_hz", metadata.sample_rate_hz) &&
        parse_json_size(json, "timing_search_start", metadata.timing_search_start) &&
        parse_json_size(
            json, "timing_search_stop_inclusive", metadata.timing_search_stop_inclusive) &&
        parse_json_size(json, "timing_search_step", metadata.timing_search_step) &&
        parse_json_number_array(
            json, "cfo_hypotheses_hz", metadata.cfo_hypotheses_hz) &&
        parse_json_size(json, "true_timing_offset", metadata.true_timing_offset) &&
        parse_json_number(json, "true_cfo_hz", metadata.true_cfo_hz);
    if (!complete) {
        error_message = "fixture metadata is missing a required acquisition field";
        return false;
    }
    if (metadata.sample_count == 0 || metadata.preamble_length == 0 ||
        metadata.sample_rate_hz <= 0.0 || metadata.timing_search_step == 0 ||
        metadata.timing_search_start > metadata.timing_search_stop_inclusive) {
        error_message = "fixture metadata contains an invalid acquisition range";
        return false;
    }
    return true;
}

bool build_timing_hypotheses(
    const FixtureMetadata& metadata,
    std::vector<std::size_t>& timing_offsets,
    std::string& error_message) {
    timing_offsets.clear();
    for (std::size_t timing = metadata.timing_search_start;;) {
        timing_offsets.push_back(timing);
        if (metadata.timing_search_stop_inclusive - timing < metadata.timing_search_step) {
            break;
        }
        timing += metadata.timing_search_step;
    }
    if (timing_offsets.back() != metadata.timing_search_stop_inclusive) {
        error_message = "timing search range is not divisible by timing_search_step";
        return false;
    }
    return true;
}

void print_error_json(
    const std::string& iq_path,
    const std::string& metadata_path,
    const std::string& error_message) {
    std::cout << "{\n"
              << "  \"ok\": false,\n"
              << "  \"fixture_path\": \""
              << satcomfec::tools::escape_json(iq_path) << "\",\n"
              << "  \"metadata_path\": \""
              << satcomfec::tools::escape_json(metadata_path) << "\",\n"
              << "  \"implementation\": \"reference\",\n"
              << "  \"error\": \""
              << satcomfec::tools::escape_json(error_message) << "\"\n"
              << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string iq_path = "data/synthetic/acquisition/clean.iq";
    std::string metadata_path;

    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string argument = argv[argument_index];
        if (argument == "--iq" && argument_index + 1 < argc) {
            iq_path = argv[++argument_index];
            continue;
        }
        if (argument == "--metadata" && argument_index + 1 < argc) {
            metadata_path = argv[++argument_index];
            continue;
        }
        if (argument == "--help") {
            std::cout << "Usage: acquisition_demo [--iq fixture.iq] "
                         "[--metadata fixture.json]\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "Unknown or incomplete argument: " << argument << "\n";
        return EXIT_FAILURE;
    }

    if (metadata_path.empty()) {
        std::filesystem::path inferred(iq_path);
        inferred.replace_extension(".json");
        metadata_path = inferred.string();
    }

    FixtureMetadata metadata;
    std::string error_message;
    const std::filesystem::path metadata_file(metadata_path);
    if (!load_fixture_metadata(metadata_file, metadata, error_message)) {
        print_error_json(iq_path, metadata_path, error_message);
        return EXIT_FAILURE;
    }

    std::vector<satcomfec::ComplexF> received_iq;
    if (!satcomfec::load_iq_from_file(iq_path, received_iq)) {
        print_error_json(iq_path, metadata_path, "failed to load fixture IQ samples");
        return EXIT_FAILURE;
    }
    if (received_iq.size() != metadata.sample_count) {
        print_error_json(iq_path, metadata_path, "IQ sample count does not match metadata");
        return EXIT_FAILURE;
    }

    const std::filesystem::path preamble_path =
        (metadata_file.parent_path() / metadata.preamble_file).lexically_normal();
    std::vector<satcomfec::ComplexF> preamble;
    if (!satcomfec::load_iq_from_file(preamble_path.string(), preamble)) {
        print_error_json(iq_path, metadata_path, "failed to load preamble IQ samples");
        return EXIT_FAILURE;
    }
    if (preamble.size() != metadata.preamble_length) {
        print_error_json(iq_path, metadata_path, "preamble length does not match metadata");
        return EXIT_FAILURE;
    }

    satcomfec::acquisition::AcquisitionConfig config;
    config.sample_rate_hz = metadata.sample_rate_hz;
    config.frequency_offsets_hz = metadata.cfo_hypotheses_hz;
    if (!build_timing_hypotheses(metadata, config.timing_offsets, error_message)) {
        print_error_json(iq_path, metadata_path, error_message);
        return EXIT_FAILURE;
    }

    satcomfec::acquisition::AcquisitionPlan plan;
    if (!satcomfec::acquisition::prepare_reference_acquisition(
            config, preamble, plan, error_message)) {
        print_error_json(iq_path, metadata_path, error_message);
        return EXIT_FAILURE;
    }
    const satcomfec::acquisition::AcquisitionResult result =
        satcomfec::acquisition::run_reference_acquisition(received_iq, plan);

    const bool acquisition_success =
        result.ok && result.best.hypothesis.timing_offset == metadata.true_timing_offset &&
        std::abs(result.best.hypothesis.frequency_offset_hz - metadata.true_cfo_hz) < 1e-9;

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n";
    std::cout << "  \"fixture_path\": \""
              << satcomfec::tools::escape_json(iq_path) << "\",\n";
    std::cout << "  \"metadata_path\": \""
              << satcomfec::tools::escape_json(metadata_path) << "\",\n";
    std::cout << "  \"scenario\": \""
              << satcomfec::tools::escape_json(metadata.scenario) << "\",\n";
    std::cout << "  \"sample_count\": " << received_iq.size() << ",\n";
    std::cout << "  \"preamble_length\": " << preamble.size() << ",\n";
    std::cout << "  \"timing_hypothesis_count\": " << config.timing_offsets.size()
              << ",\n";
    std::cout << "  \"cfo_hypothesis_count\": "
              << config.frequency_offsets_hz.size() << ",\n";
    std::cout << "  \"evaluated_candidate_count\": "
              << result.evaluated_candidate_count << ",\n";
    std::cout << "  \"true_timing_offset\": " << metadata.true_timing_offset << ",\n";
    std::cout << "  \"true_cfo_hz\": "
              << satcomfec::tools::format_float(metadata.true_cfo_hz, 3) << ",\n";
    std::cout << "  \"detected_timing_offset\": "
              << result.best.hypothesis.timing_offset << ",\n";
    std::cout << "  \"detected_cfo_hz\": "
              << satcomfec::tools::format_float(
                     result.best.hypothesis.frequency_offset_hz, 3)
              << ",\n";
    std::cout << "  \"best_score\": "
              << satcomfec::tools::format_float(result.best.score, 6) << ",\n";
    std::cout << "  \"second_best_timing_offset\": "
              << result.second_best.hypothesis.timing_offset << ",\n";
    std::cout << "  \"second_best_cfo_hz\": "
              << satcomfec::tools::format_float(
                     result.second_best.hypothesis.frequency_offset_hz, 3)
              << ",\n";
    std::cout << "  \"second_best_score\": "
              << satcomfec::tools::format_float(result.second_best.score, 6) << ",\n";
    std::cout << "  \"peak_ratio\": "
              << satcomfec::tools::format_float(result.peak_ratio, 6) << ",\n";
    std::cout << "  \"normalized_peak_separation\": "
              << satcomfec::tools::format_float(result.normalized_peak_separation, 6)
              << ",\n";
    std::cout << "  \"acquisition_success\": "
              << (acquisition_success ? "true" : "false") << ",\n";
    std::cout << "  \"implementation\": \"reference\",\n";
    std::cout << "  \"error\": \""
              << satcomfec::tools::escape_json(result.error_message) << "\"\n";
    std::cout << "}\n";

    return acquisition_success ? EXIT_SUCCESS : EXIT_FAILURE;
}
