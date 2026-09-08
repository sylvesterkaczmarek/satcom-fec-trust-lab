#include "acquisition_benchmark.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

bool parse_size(const char* text, std::size_t& value) {
    if (text == nullptr || *text == '\0' || *text == '-' ||
        std::isspace(static_cast<unsigned char>(*text))) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_u64(const char* text, std::uint64_t& value) {
    if (text == nullptr || *text == '\0' || *text == '-' ||
        std::isspace(static_cast<unsigned char>(*text))) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    // Preserve the historical interpretation so an existing seed always
    // selects the same deterministic input and execution order.
    const unsigned long long parsed = std::strtoull(text, &end, 0);
    if (end == text || *end != '\0' || errno == ERANGE ||
        parsed > std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool parse_positive_double(const char* text, double& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    value = std::strtod(text, &end);
    return end != text && *end == '\0' && errno != ERANGE &&
           std::isfinite(value) && value > 0.0;
}

bool write_file(const std::string& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    // Closing can report a failed buffered write, including a full filesystem.
    output.close();
    return output.good();
}

bool validate_output_paths(
    const std::string& json_path,
    const std::string& csv_path,
    std::string& error_message) {
    std::filesystem::path resolved_json;
    std::filesystem::path resolved_csv;
    for (const auto& entry : {
             std::pair<const std::string*, std::filesystem::path*>{
                 &json_path, &resolved_json},
             {&csv_path, &resolved_csv}}) {
        if (entry.first->empty()) {
            continue;
        }
        std::error_code error;
        const auto absolute_path = std::filesystem::absolute(*entry.first, error);
        if (!error) {
            *entry.second = std::filesystem::weakly_canonical(absolute_path, error);
        }
        if (error) {
            error_message = "cannot resolve report path: " + *entry.first;
            return false;
        }
        if (std::filesystem::exists(*entry.second, error) &&
            !std::filesystem::is_regular_file(*entry.second, error)) {
            error_message = "report path must be a regular file: " + *entry.first;
            return false;
        }
        if (error || !std::filesystem::is_directory(entry.second->parent_path(), error)) {
            error_message = "report parent directory is unavailable: " + *entry.first;
            return false;
        }
    }
    if (!json_path.empty() && !csv_path.empty()) {
        std::error_code error;
        const bool same_file = std::filesystem::equivalent(json_path, csv_path, error);
        if (resolved_json == resolved_csv || same_file) {
            error_message = "JSON and CSV reports must use different files";
            return false;
        }
    }
    return true;
}

void print_usage() {
    std::cout
        << "Usage: benchmark_acquisition [options]\n"
        << "\n"
        << "Options:\n"
        << "  --workload NAME       small, medium, large, very-large, or all\n"
        << "  --warmup-rounds N     warm-up rounds per workload (default: 2)\n"
        << "  --samples N           independent timed samples (default: 15)\n"
        << "  --min-sample-ms MS    minimum duration of each timed sample (default: 50)\n"
        << "  --seed N              deterministic seed; decimal, 0x hex, or leading-zero octal\n"
        << "  --json PATH           also write the authoritative JSON report to PATH\n"
        << "  --csv PATH            also write a compact CSV summary to PATH\n"
        << "  --list-workloads      print fixed workload definitions and exit\n"
        << "  --help                show this help\n";
}

void print_workloads() {
    for (const auto& workload :
         satcomfec::benchmark::acquisition_benchmark_workloads()) {
        std::cout << workload.name
                  << ": iq_samples=" << workload.iq_sample_count
                  << ", preamble=" << workload.preamble_length
                  << ", timing_hypotheses=" << workload.timing_hypothesis_count
                  << ", cfo_hypotheses=" << workload.cfo_hypothesis_count
                  << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    satcomfec::benchmark::BenchmarkOptions options;
    std::string json_path;
    std::string csv_path;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return EXIT_SUCCESS;
        }
        if (argument == "--list-workloads") {
            print_workloads();
            return EXIT_SUCCESS;
        }
        if (argument == "--workload" || argument == "--warmup-rounds" ||
            argument == "--samples" || argument == "--min-sample-ms" ||
            argument == "--seed" || argument == "--json" || argument == "--csv") {
            if (index + 1 >= argc || argv[index + 1][0] == '\0' ||
                std::string(argv[index + 1]).rfind("--", 0) == 0) {
                std::cerr << "error: missing value for " << argument << "\n";
                return EXIT_FAILURE;
            }
        }
        if (argument == "--workload" && index + 1 < argc) {
            const std::string workload = argv[++index];
            if (workload == "all") {
                options.workload_names.clear();
            } else {
                options.workload_names.push_back(workload);
            }
            continue;
        }
        if (argument == "--warmup-rounds" && index + 1 < argc) {
            if (!parse_size(argv[++index], options.warmup_rounds)) {
                std::cerr << "error: --warmup-rounds requires a non-negative integer\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (argument == "--samples" && index + 1 < argc) {
            if (!parse_size(argv[++index], options.timed_sample_count) ||
                options.timed_sample_count < 3) {
                std::cerr << "error: --samples must be an integer of at least 3\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (argument == "--min-sample-ms" && index + 1 < argc) {
            if (!parse_positive_double(
                    argv[++index], options.minimum_sample_duration_ms)) {
                std::cerr << "error: --min-sample-ms must be finite and positive\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (argument == "--seed" && index + 1 < argc) {
            if (!parse_u64(argv[++index], options.deterministic_seed)) {
                std::cerr << "error: --seed requires an unsigned integer\n";
                return EXIT_FAILURE;
            }
            continue;
        }
        if (argument == "--json" && index + 1 < argc) {
            json_path = argv[++index];
            continue;
        }
        if (argument == "--csv" && index + 1 < argc) {
            csv_path = argv[++index];
            continue;
        }

        std::cerr << "error: unknown or incomplete argument: " << argument << "\n";
        return EXIT_FAILURE;
    }

    std::string output_error;
    if (!validate_output_paths(json_path, csv_path, output_error)) {
        std::cerr << "error: " << output_error << "\n";
        return EXIT_FAILURE;
    }

    const satcomfec::benchmark::BenchmarkArtifacts artifacts =
        satcomfec::benchmark::run_acquisition_benchmark(options);
    if (!artifacts.error_message.empty()) {
        std::cerr << "error: " << artifacts.error_message << "\n";
        return EXIT_FAILURE;
    }

    if (!json_path.empty() && !write_file(json_path, artifacts.json)) {
        std::cerr << "error: failed to write JSON report: " << json_path << "\n";
        return EXIT_FAILURE;
    }
    if (!csv_path.empty() && !write_file(csv_path, artifacts.csv)) {
        std::cerr << "error: failed to write CSV report: " << csv_path << "\n";
        return EXIT_FAILURE;
    }

    std::cout << artifacts.json;
    return artifacts.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
