#ifndef SATCOMFEC_TOOLS_ACQUISITION_BENCHMARK_H
#define SATCOMFEC_TOOLS_ACQUISITION_BENCHMARK_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace satcomfec::benchmark {

struct WorkloadDefinition {
    std::string name;
    std::size_t iq_sample_count = 0;
    std::size_t preamble_length = 0;
    std::size_t timing_hypothesis_count = 0;
    std::size_t cfo_hypothesis_count = 0;
};

struct BenchmarkOptions {
    std::vector<std::string> workload_names;
    std::size_t warmup_rounds = 2;
    std::size_t timed_sample_count = 15;
    double minimum_sample_duration_ms = 50.0;
    std::uint64_t deterministic_seed = 0x534154434F4D4645ULL;
};

struct BenchmarkArtifacts {
    bool ok = false;
    std::string json;
    std::string csv;
    std::string error_message;
};

const std::vector<WorkloadDefinition>& acquisition_benchmark_workloads();

BenchmarkArtifacts run_acquisition_benchmark(const BenchmarkOptions& options);

}  // namespace satcomfec::benchmark

#endif  // SATCOMFEC_TOOLS_ACQUISITION_BENCHMARK_H
