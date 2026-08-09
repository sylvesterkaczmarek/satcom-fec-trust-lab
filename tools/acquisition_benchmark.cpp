#include "acquisition_benchmark.h"

#include "acquisition/acquisition_neon.h"
#include "acquisition/acquisition_reference.h"
#include "acquisition/acquisition_sme2.h"
#include "json_output.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(__linux__)
#include <sys/auxv.h>
#if defined(__has_include)
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
#endif
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

#ifndef SATCOMFEC_BENCHMARK_GIT_SHA
#define SATCOMFEC_BENCHMARK_GIT_SHA "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_GIT_DIRTY
#define SATCOMFEC_BENCHMARK_GIT_DIRTY "unknown"
#endif

#ifndef SATCOMFEC_BENCHMARK_BUILD_TYPE
#define SATCOMFEC_BENCHMARK_BUILD_TYPE "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_COMMON_FLAGS
#define SATCOMFEC_BENCHMARK_COMMON_FLAGS "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_REFERENCE_FLAGS
#define SATCOMFEC_BENCHMARK_REFERENCE_FLAGS "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_NEON_FLAGS
#define SATCOMFEC_BENCHMARK_NEON_FLAGS "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_SME2_FLAGS
#define SATCOMFEC_BENCHMARK_SME2_FLAGS "not-compiled"
#endif

namespace satcomfec::benchmark {
namespace {

using Clock = std::chrono::steady_clock;
using satcomfec::ComplexF;
using satcomfec::acquisition::AcquisitionPlan;
using satcomfec::acquisition::AcquisitionResult;
using satcomfec::acquisition::Sme2AcquisitionWorkspace;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSampleRateHz = 48000.0;
constexpr double kCfoSpacingHz = 250.0;
constexpr double kScoreAbsoluteTolerance = 1.0e-3;
constexpr double kScoreRelativeTolerance = 2.0e-4;

volatile std::uint64_t g_result_sink = 0;

enum class ImplementationId {
    kReference,
    kNeon,
    kSme2,
};

enum class TimingMode {
    kSteadyState,
    kSetupInclusive,
};

struct PreparedWorkload {
    WorkloadDefinition definition;
    std::vector<ComplexF> received_iq;
    std::vector<ComplexF> preamble;
    acquisition::AcquisitionConfig config;
    AcquisitionPlan plan;
    Sme2AcquisitionWorkspace sme2_workspace;
    std::size_t true_timing_offset = 0;
    double true_cfo_hz = 0.0;
    std::uint64_t fixture_seed = 0;
};

struct CorrectnessResult {
    bool executed = false;
    bool passed = false;
    bool result_ok = false;
    bool ground_truth_match = false;
    bool candidate_count_match = false;
    bool best_candidate_match = false;
    bool second_best_candidate_match = false;
    bool best_score_within_tolerance = false;
    bool second_best_score_within_tolerance = false;
    std::string actual_implementation = "unavailable";
    double best_score_difference = 0.0;
    double best_score_tolerance = 0.0;
    double second_best_score_difference = 0.0;
    double second_best_score_tolerance = 0.0;
    std::string error_message;
};

struct TimedSample {
    bool ok = false;
    std::size_t operation_count = 0;
    double block_duration_ms = 0.0;
    double latency_per_operation_ms = 0.0;
    std::string error_message;
};

struct TimingStatistics {
    std::size_t sample_count = 0;
    std::size_t total_operation_count = 0;
    double total_timed_duration_ms = 0.0;
    double mean_latency_ms = 0.0;
    double standard_deviation_ms = 0.0;
    double minimum_latency_ms = 0.0;
    double maximum_latency_ms = 0.0;
    double median_latency_ms = 0.0;
    double p50_latency_ms = 0.0;
    double p95_latency_ms = 0.0;
    double candidate_correlations_per_second = 0.0;
    double complex_macs_per_second = 0.0;
    std::vector<std::size_t> operations_per_sample;
    std::vector<double> latency_samples_ms;
    std::vector<double> block_duration_samples_ms;
};

struct ModeResult {
    TimingMode mode = TimingMode::kSteadyState;
    bool valid = false;
    std::string error_message;
    std::vector<TimedSample> samples;
    TimingStatistics statistics;
    std::optional<double> speedup_vs_reference;
    std::optional<double> speedup_vs_neon;
};

struct ImplementationResult {
    ImplementationId id = ImplementationId::kReference;
    std::string name;
    std::string implementation_class;
    std::string mechanism;
    bool available = false;
    bool executed = false;
    std::string unavailable_reason;
    CorrectnessResult correctness;
    std::vector<ModeResult> modes;
};

struct WorkloadResult {
    PreparedWorkload workload;
    AcquisitionResult reference_result;
    bool reference_valid = false;
    std::vector<ImplementationResult> implementations;
    std::vector<std::vector<std::string>> execution_order_by_sample;
};

struct HostMetadata {
    std::string os_name = "unavailable";
    std::string os_release = "unavailable";
    std::string architecture = "unavailable";
    std::string cpu_model = "unavailable";
    bool neon_hardware_supported = false;
    bool sve_hardware_supported = false;
    bool sme_hardware_supported = false;
    bool sme2_hardware_supported = false;
};

struct BenchmarkReport {
    bool ok = true;
    BenchmarkOptions options;
    std::string timestamp_utc;
    HostMetadata host;
    std::vector<WorkloadResult> workloads;
};

struct BenchmarkTask {
    std::size_t implementation_index = 0;
    std::size_t mode_index = 0;
};

class DeterministicGenerator {
public:
    explicit DeterministicGenerator(std::uint64_t seed)
        : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}

    std::uint64_t next_u64() {
        std::uint64_t value = state_;
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        state_ = value;
        return value * 2685821657736338717ULL;
    }

    float signed_unit() {
        const double unit = static_cast<double>(next_u64() >> 11) /
                            static_cast<double>(1ULL << 53);
        return static_cast<float>(2.0 * unit - 1.0);
    }

private:
    std::uint64_t state_;
};

const char* implementation_name(ImplementationId id) {
    switch (id) {
        case ImplementationId::kReference:
            return "reference";
        case ImplementationId::kNeon:
            return "neon";
        case ImplementationId::kSme2:
            return "sme2";
    }
    return "unknown";
}

const char* timing_mode_name(TimingMode mode) {
    switch (mode) {
        case TimingMode::kSteadyState:
            return "steady-state";
        case TimingMode::kSetupInclusive:
            return "setup-inclusive";
    }
    return "unknown";
}

std::string task_name(const BenchmarkTask& task) {
    return std::string(implementation_name(
               static_cast<ImplementationId>(task.implementation_index))) +
           "/" + timing_mode_name(static_cast<TimingMode>(task.mode_index));
}

bool same_candidate(
    const acquisition::AcquisitionCandidate& left,
    const acquisition::AcquisitionCandidate& right) {
    return left.valid == right.valid &&
           (!left.valid ||
            (left.hypothesis.timing_offset == right.hypothesis.timing_offset &&
             left.hypothesis.frequency_offset_hz ==
                 right.hypothesis.frequency_offset_hz));
}

double score_tolerance(double reference_score) {
    return kScoreAbsoluteTolerance +
           kScoreRelativeTolerance * std::abs(reference_score);
}

std::uint64_t checked_complex_mac_count(const WorkloadDefinition& definition) {
    const std::uint64_t timing = definition.timing_hypothesis_count;
    const std::uint64_t cfo = definition.cfo_hypothesis_count;
    const std::uint64_t preamble = definition.preamble_length;
    if (timing > std::numeric_limits<std::uint64_t>::max() / cfo ||
        timing * cfo > std::numeric_limits<std::uint64_t>::max() / preamble) {
        return 0;
    }
    return timing * cfo * preamble;
}

std::uint64_t candidate_count(const WorkloadDefinition& definition) {
    return static_cast<std::uint64_t>(definition.timing_hypothesis_count) *
           static_cast<std::uint64_t>(definition.cfo_hypothesis_count);
}

std::uint64_t stable_name_hash(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::uint64_t result_fingerprint(const AcquisitionResult& result) {
    std::uint64_t score_bits = 0;
    static_assert(sizeof(score_bits) == sizeof(result.best.score));
    std::memcpy(&score_bits, &result.best.score, sizeof(score_bits));
    score_bits ^= static_cast<std::uint64_t>(
        result.best.hypothesis.timing_offset + 0x9E3779B9U);
    score_bits ^= static_cast<std::uint64_t>(result.evaluated_candidate_count) << 32;
    return score_bits;
}

void consume_result(const AcquisitionResult& result) {
    g_result_sink =
        g_result_sink * 1099511628211ULL ^ result_fingerprint(result);
}

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string compiler_name() {
#if defined(__clang__)
    return "Clang";
#elif defined(__GNUC__)
    return "GCC";
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "unknown";
#endif
}

std::string compiler_version() {
#if defined(__clang__)
    return __clang_version__;
#elif defined(__GNUC__)
    return __VERSION__;
#elif defined(_MSC_FULL_VER)
    return std::to_string(_MSC_FULL_VER);
#else
    return "unknown";
#endif
}

#if defined(__APPLE__)
bool query_sysctl_int(const char* name) {
    int value = 0;
    std::size_t size = sizeof(value);
    return sysctlbyname(name, &value, &size, nullptr, 0) == 0 && value != 0;
}

std::string query_sysctl_string(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return "unavailable";
    }
    std::vector<char> value(size);
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
        return "unavailable";
    }
    return std::string(value.data());
}
#endif

#if defined(__linux__)
std::string linux_cpu_model() {
    std::ifstream input("/proc/cpuinfo");
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        if (key.find("model name") == std::string::npos &&
            key.find("Hardware") == std::string::npos) {
            continue;
        }
        const std::size_t value_start = line.find_first_not_of(" \t", separator + 1);
        return value_start == std::string::npos
                   ? "unavailable"
                   : line.substr(value_start);
    }
    return "unavailable";
}
#endif

HostMetadata collect_host_metadata() {
    HostMetadata host;
#if defined(__unix__) || defined(__APPLE__)
    struct utsname system_info {};
    if (uname(&system_info) == 0) {
        host.os_name = system_info.sysname;
        host.os_release = system_info.release;
        host.architecture = system_info.machine;
    }
#endif

#if defined(__APPLE__)
    host.cpu_model = query_sysctl_string("machdep.cpu.brand_string");
    if (host.cpu_model == "unavailable") {
        host.cpu_model = query_sysctl_string("hw.model");
    }
    host.neon_hardware_supported = query_sysctl_int("hw.optional.neon");
    host.sme_hardware_supported = query_sysctl_int("hw.optional.arm.FEAT_SME");
    host.sme2_hardware_supported = query_sysctl_int("hw.optional.arm.FEAT_SME2");
#elif defined(__linux__)
    host.cpu_model = linux_cpu_model();
#if defined(__aarch64__)
    host.neon_hardware_supported = true;
#endif
#if defined(HWCAP_ASIMD)
    host.neon_hardware_supported =
        (getauxval(AT_HWCAP) & HWCAP_ASIMD) != 0;
#endif
#if defined(HWCAP_SVE)
    host.sve_hardware_supported = (getauxval(AT_HWCAP) & HWCAP_SVE) != 0;
#endif
#if defined(HWCAP2_SME)
    host.sme_hardware_supported = (getauxval(AT_HWCAP2) & HWCAP2_SME) != 0;
#endif
#if defined(HWCAP2_SME2)
    host.sme2_hardware_supported = (getauxval(AT_HWCAP2) & HWCAP2_SME2) != 0;
#endif
#endif

    host.neon_hardware_supported =
        host.neon_hardware_supported || acquisition::acquisition_neon_kernel_compiled();
    host.sme2_hardware_supported =
        host.sme2_hardware_supported ||
        acquisition::acquisition_sme2_runtime_supported();
    host.sme_hardware_supported =
        host.sme_hardware_supported || host.sme2_hardware_supported;
    return host;
}

bool prepare_workload(
    const WorkloadDefinition& definition,
    std::uint64_t seed,
    PreparedWorkload& workload,
    std::string& error_message) {
    if (definition.preamble_length == 0 ||
        definition.timing_hypothesis_count == 0 ||
        definition.cfo_hypothesis_count == 0 ||
        definition.timing_hypothesis_count + definition.preamble_length - 1 >
            definition.iq_sample_count ||
        definition.cfo_hypothesis_count % 2 == 0 ||
        checked_complex_mac_count(definition) == 0) {
        error_message = "benchmark workload definition is invalid";
        return false;
    }

    workload = {};
    workload.definition = definition;
    workload.fixture_seed = seed;
    DeterministicGenerator generator(seed);
    constexpr float qpsk_amplitude = 0.7071067811865475F;

    workload.preamble.reserve(definition.preamble_length);
    for (std::size_t index = 0; index < definition.preamble_length; ++index) {
        const float real = (generator.next_u64() & 1U) != 0
                               ? qpsk_amplitude
                               : -qpsk_amplitude;
        const float imag = (generator.next_u64() & 1U) != 0
                               ? qpsk_amplitude
                               : -qpsk_amplitude;
        workload.preamble.emplace_back(real, imag);
    }

    workload.received_iq.resize(definition.iq_sample_count);
    for (ComplexF& sample : workload.received_iq) {
        sample = {
            0.02F * generator.signed_unit(),
            0.02F * generator.signed_unit(),
        };
    }

    workload.config.sample_rate_hz = kSampleRateHz;
    workload.config.timing_offsets.resize(definition.timing_hypothesis_count);
    std::iota(
        workload.config.timing_offsets.begin(),
        workload.config.timing_offsets.end(),
        std::size_t{0});

    const std::int64_t cfo_center =
        static_cast<std::int64_t>(definition.cfo_hypothesis_count / 2);
    workload.config.frequency_offsets_hz.reserve(definition.cfo_hypothesis_count);
    for (std::size_t index = 0; index < definition.cfo_hypothesis_count; ++index) {
        workload.config.frequency_offsets_hz.push_back(
            kCfoSpacingHz *
            static_cast<double>(static_cast<std::int64_t>(index) - cfo_center));
    }

    workload.true_timing_offset =
        3 * definition.timing_hypothesis_count / 5;
    const std::size_t true_cfo_index = definition.cfo_hypothesis_count / 2 + 1;
    workload.true_cfo_hz =
        workload.config.frequency_offsets_hz[true_cfo_index];
    for (std::size_t index = 0; index < workload.preamble.size(); ++index) {
        const double phase =
            0.37 + 2.0 * kPi * workload.true_cfo_hz *
                       static_cast<double>(index) / kSampleRateHz;
        const std::complex<double> carrier = std::polar(0.9, phase);
        const std::complex<double> signal =
            std::complex<double>(
                workload.preamble[index].real(), workload.preamble[index].imag()) *
            carrier;
        workload.received_iq[workload.true_timing_offset + index] += ComplexF(
            static_cast<float>(signal.real()),
            static_cast<float>(signal.imag()));
    }

    return acquisition::prepare_acquisition_plan(
        workload.config,
        workload.preamble,
        workload.plan,
        error_message);
}

AcquisitionResult execute_checked(
    ImplementationId id,
    PreparedWorkload& workload,
    std::string& error_message) {
    switch (id) {
        case ImplementationId::kReference:
            return acquisition::run_reference_acquisition(
                workload.received_iq, workload.plan);
        case ImplementationId::kNeon:
            return acquisition::run_neon_acquisition(
                workload.received_iq, workload.plan);
        case ImplementationId::kSme2:
            if (!acquisition::prepare_sme2_acquisition_workspace(
                    workload.received_iq,
                    workload.plan,
                    workload.sme2_workspace,
                    error_message)) {
                AcquisitionResult failure;
                failure.implementation = "sme2";
                failure.error_message = error_message;
                return failure;
            }
            return acquisition::run_sme2_acquisition_prepared(
                workload.plan, workload.sme2_workspace);
    }
    AcquisitionResult failure;
    failure.implementation = "unavailable";
    failure.error_message = "unknown acquisition implementation";
    return failure;
}

AcquisitionResult execute_steady_state(
    ImplementationId id,
    PreparedWorkload& workload) {
    switch (id) {
        case ImplementationId::kReference:
            return acquisition::run_reference_acquisition_steady_state(
                workload.received_iq, workload.plan);
        case ImplementationId::kNeon:
            return acquisition::run_neon_acquisition_steady_state(
                workload.received_iq, workload.plan);
        case ImplementationId::kSme2:
            return acquisition::run_sme2_acquisition_steady_state(
                workload.plan, workload.sme2_workspace);
    }
    AcquisitionResult failure;
    failure.implementation = "unavailable";
    failure.error_message = "unknown acquisition implementation";
    return failure;
}

AcquisitionResult execute_setup_inclusive(
    ImplementationId id,
    const PreparedWorkload& workload) {
    AcquisitionPlan plan;
    std::string error_message;
    if (!acquisition::prepare_acquisition_plan(
            workload.config, workload.preamble, plan, error_message)) {
        AcquisitionResult failure;
        failure.implementation = implementation_name(id);
        failure.error_message = error_message;
        return failure;
    }

    switch (id) {
        case ImplementationId::kReference:
            return acquisition::run_reference_acquisition(
                workload.received_iq, plan);
        case ImplementationId::kNeon:
            return acquisition::run_neon_acquisition(
                workload.received_iq, plan);
        case ImplementationId::kSme2:
            return acquisition::run_sme2_acquisition(
                workload.received_iq, plan);
    }
    AcquisitionResult failure;
    failure.implementation = "unavailable";
    failure.error_message = "unknown acquisition implementation";
    return failure;
}

CorrectnessResult verify_correctness(
    ImplementationId id,
    const AcquisitionResult& result,
    const AcquisitionResult& reference,
    const PreparedWorkload& workload) {
    CorrectnessResult correctness;
    correctness.executed = true;
    correctness.result_ok = result.ok;
    correctness.actual_implementation = result.implementation;
    correctness.ground_truth_match =
        result.ok &&
        result.best.hypothesis.timing_offset == workload.true_timing_offset &&
        result.best.hypothesis.frequency_offset_hz == workload.true_cfo_hz;
    correctness.candidate_count_match =
        result.evaluated_candidate_count == candidate_count(workload.definition);
    correctness.best_candidate_match = same_candidate(result.best, reference.best);
    correctness.second_best_candidate_match =
        same_candidate(result.second_best, reference.second_best);
    correctness.best_score_difference =
        std::abs(result.best.score - reference.best.score);
    correctness.best_score_tolerance = score_tolerance(reference.best.score);
    correctness.best_score_within_tolerance =
        correctness.best_score_difference <= correctness.best_score_tolerance;
    correctness.second_best_score_difference =
        std::abs(result.second_best.score - reference.second_best.score);
    correctness.second_best_score_tolerance =
        score_tolerance(reference.second_best.score);
    correctness.second_best_score_within_tolerance =
        correctness.second_best_score_difference <=
        correctness.second_best_score_tolerance;
    correctness.passed =
        correctness.result_ok && correctness.ground_truth_match &&
        correctness.candidate_count_match && correctness.best_candidate_match &&
        correctness.second_best_candidate_match &&
        correctness.best_score_within_tolerance &&
        correctness.second_best_score_within_tolerance &&
        result.implementation == implementation_name(id);
    if (!correctness.passed) {
        correctness.error_message = result.error_message.empty()
                                        ? "result did not match the reference contract"
                                        : result.error_message;
    }
    return correctness;
}

TimedSample measure_sample(
    double minimum_duration_ms,
    const std::function<AcquisitionResult()>& operation) {
    TimedSample sample;
    AcquisitionResult last_result;
    const auto start = Clock::now();
    auto stop = start;
    do {
        last_result = operation();
        if (!last_result.ok) {
            sample.error_message = last_result.error_message;
            return sample;
        }
        ++sample.operation_count;
        stop = Clock::now();
    } while (std::chrono::duration<double, std::milli>(stop - start).count() <
             minimum_duration_ms);

    sample.block_duration_ms =
        std::chrono::duration<double, std::milli>(stop - start).count();
    sample.latency_per_operation_ms =
        sample.block_duration_ms / static_cast<double>(sample.operation_count);
    sample.ok = true;
    consume_result(last_result);
    return sample;
}

double median_of_sorted(const std::vector<double>& sorted) {
    const std::size_t middle = sorted.size() / 2;
    if (sorted.size() % 2 != 0) {
        return sorted[middle];
    }
    return 0.5 * (sorted[middle - 1] + sorted[middle]);
}

double nearest_rank_percentile(
    const std::vector<double>& sorted,
    double percentile) {
    const std::size_t rank = static_cast<std::size_t>(
        std::ceil(percentile * static_cast<double>(sorted.size())));
    return sorted[std::max<std::size_t>(1, rank) - 1];
}

TimingStatistics calculate_statistics(
    const std::vector<TimedSample>& samples,
    const WorkloadDefinition& workload) {
    TimingStatistics statistics;
    statistics.sample_count = samples.size();
    statistics.latency_samples_ms.reserve(samples.size());
    statistics.operations_per_sample.reserve(samples.size());
    statistics.block_duration_samples_ms.reserve(samples.size());
    for (const TimedSample& sample : samples) {
        statistics.latency_samples_ms.push_back(sample.latency_per_operation_ms);
        statistics.operations_per_sample.push_back(sample.operation_count);
        statistics.block_duration_samples_ms.push_back(sample.block_duration_ms);
        statistics.total_operation_count += sample.operation_count;
        statistics.total_timed_duration_ms += sample.block_duration_ms;
    }

    std::vector<double> sorted = statistics.latency_samples_ms;
    std::sort(sorted.begin(), sorted.end());
    statistics.minimum_latency_ms = sorted.front();
    statistics.maximum_latency_ms = sorted.back();
    statistics.median_latency_ms = median_of_sorted(sorted);
    statistics.p50_latency_ms = nearest_rank_percentile(sorted, 0.50);
    statistics.p95_latency_ms = nearest_rank_percentile(sorted, 0.95);
    statistics.mean_latency_ms =
        std::accumulate(sorted.begin(), sorted.end(), 0.0) /
        static_cast<double>(sorted.size());
    if (sorted.size() > 1) {
        double squared_deviation_sum = 0.0;
        for (double value : sorted) {
            const double difference = value - statistics.mean_latency_ms;
            squared_deviation_sum += difference * difference;
        }
        statistics.standard_deviation_ms = std::sqrt(
            squared_deviation_sum / static_cast<double>(sorted.size() - 1));
    }

    const double median_seconds = statistics.median_latency_ms / 1000.0;
    statistics.candidate_correlations_per_second =
        static_cast<double>(candidate_count(workload)) / median_seconds;
    statistics.complex_macs_per_second =
        static_cast<double>(checked_complex_mac_count(workload)) / median_seconds;
    return statistics;
}

ImplementationResult make_implementation_result(ImplementationId id) {
    ImplementationResult implementation;
    implementation.id = id;
    implementation.name = implementation_name(id);
    implementation.modes.resize(2);
    implementation.modes[0].mode = TimingMode::kSteadyState;
    implementation.modes[1].mode = TimingMode::kSetupInclusive;
    switch (id) {
        case ImplementationId::kReference:
            implementation.available = true;
            implementation.implementation_class = "scalar-reference";
            implementation.mechanism = "scalar-complex-inner-product";
            break;
        case ImplementationId::kNeon:
            implementation.available = acquisition::acquisition_neon_kernel_compiled();
            implementation.implementation_class = "neon-intrinsics";
            implementation.mechanism = "neon-float32-complex-multiply-accumulate";
            if (!implementation.available) {
                implementation.unavailable_reason =
                    "NEON acquisition kernel is not compiled for this target";
            }
            break;
        case ImplementationId::kSme2:
            implementation.available =
                acquisition::acquisition_sme2_kernel_compiled() &&
                acquisition::acquisition_sme2_runtime_supported();
            implementation.implementation_class = "sme2-za-vgx4";
            implementation.mechanism = acquisition::acquisition_sme2_mechanism();
            if (!acquisition::acquisition_sme2_kernel_compiled()) {
                implementation.unavailable_reason =
                    "SME2 acquisition kernel is not compiled for this target";
            } else if (!acquisition::acquisition_sme2_runtime_supported()) {
                implementation.unavailable_reason =
                    "SME2 acquisition kernel is compiled but runtime execution is unavailable";
            }
            break;
    }
    return implementation;
}

bool prepare_and_verify_workload(
    const WorkloadDefinition& definition,
    std::uint64_t seed,
    WorkloadResult& result,
    std::string& error_message) {
    if (!prepare_workload(definition, seed, result.workload, error_message)) {
        return false;
    }

    result.reference_result = acquisition::run_reference_acquisition(
        result.workload.received_iq, result.workload.plan);
    result.reference_valid =
        result.reference_result.ok &&
        result.reference_result.best.hypothesis.timing_offset ==
            result.workload.true_timing_offset &&
        result.reference_result.best.hypothesis.frequency_offset_hz ==
            result.workload.true_cfo_hz &&
        result.reference_result.evaluated_candidate_count ==
            candidate_count(definition);
    if (!result.reference_valid) {
        error_message = result.reference_result.error_message.empty()
                            ? "reference acquisition failed benchmark ground truth"
                            : result.reference_result.error_message;
        return false;
    }

    result.implementations = {
        make_implementation_result(ImplementationId::kReference),
        make_implementation_result(ImplementationId::kNeon),
        make_implementation_result(ImplementationId::kSme2),
    };
    for (ImplementationResult& implementation : result.implementations) {
        if (!implementation.available) {
            implementation.correctness.error_message =
                implementation.unavailable_reason;
            for (ModeResult& mode : implementation.modes) {
                mode.error_message = implementation.unavailable_reason;
            }
            continue;
        }
        std::string execution_error;
        const AcquisitionResult candidate = execute_checked(
            implementation.id, result.workload, execution_error);
        implementation.executed = true;
        implementation.correctness = verify_correctness(
            implementation.id,
            candidate,
            result.reference_result,
            result.workload);
        if (!execution_error.empty() &&
            implementation.correctness.error_message.empty()) {
            implementation.correctness.error_message = execution_error;
        }
        for (ModeResult& mode : implementation.modes) {
            mode.valid = implementation.correctness.passed;
            if (!mode.valid) {
                mode.error_message = implementation.correctness.error_message;
            }
        }
    }
    return true;
}

AcquisitionResult execute_task(
    const BenchmarkTask& task,
    WorkloadResult& workload) {
    const ImplementationId implementation =
        workload.implementations[task.implementation_index].id;
    const TimingMode mode =
        workload.implementations[task.implementation_index]
            .modes[task.mode_index]
            .mode;
    return mode == TimingMode::kSteadyState
               ? execute_steady_state(implementation, workload.workload)
               : execute_setup_inclusive(implementation, workload.workload);
}

std::vector<BenchmarkTask> active_tasks(const WorkloadResult& workload) {
    std::vector<BenchmarkTask> tasks;
    for (std::size_t implementation_index = 0;
         implementation_index < workload.implementations.size();
         ++implementation_index) {
        const auto& implementation = workload.implementations[implementation_index];
        for (std::size_t mode_index = 0;
             mode_index < implementation.modes.size();
             ++mode_index) {
            if (implementation.modes[mode_index].valid) {
                tasks.push_back({implementation_index, mode_index});
            }
        }
    }
    return tasks;
}

void run_timing(
    WorkloadResult& workload,
    const BenchmarkOptions& options,
    std::mt19937_64& order_generator,
    bool& report_ok) {
    for (std::size_t round = 0; round < options.warmup_rounds; ++round) {
        std::vector<BenchmarkTask> tasks = active_tasks(workload);
        std::shuffle(tasks.begin(), tasks.end(), order_generator);
        for (const BenchmarkTask& task : tasks) {
            const AcquisitionResult result = execute_task(task, workload);
            if (!result.ok) {
                auto& mode = workload.implementations[task.implementation_index]
                                 .modes[task.mode_index];
                mode.valid = false;
                mode.error_message = "warm-up failed: " + result.error_message;
                report_ok = false;
                continue;
            }
            consume_result(result);
        }
    }

    for (std::size_t sample_index = 0;
         sample_index < options.timed_sample_count;
         ++sample_index) {
        std::vector<BenchmarkTask> tasks = active_tasks(workload);
        std::shuffle(tasks.begin(), tasks.end(), order_generator);
        std::vector<std::string> order;
        order.reserve(tasks.size());
        for (const BenchmarkTask& task : tasks) {
            order.push_back(task_name(task));
            ModeResult& mode = workload.implementations[task.implementation_index]
                                   .modes[task.mode_index];
            const TimedSample sample = measure_sample(
                options.minimum_sample_duration_ms,
                [&workload, &task]() { return execute_task(task, workload); });
            if (!sample.ok) {
                mode.valid = false;
                mode.error_message = "timed sample failed: " + sample.error_message;
                report_ok = false;
                continue;
            }
            mode.samples.push_back(sample);
        }
        workload.execution_order_by_sample.push_back(std::move(order));
    }

    for (ImplementationResult& implementation : workload.implementations) {
        for (ModeResult& mode : implementation.modes) {
            if (!mode.valid) {
                continue;
            }
            if (mode.samples.size() != options.timed_sample_count) {
                mode.valid = false;
                mode.error_message = "timing sample count is incomplete";
                report_ok = false;
                continue;
            }
            mode.statistics = calculate_statistics(
                mode.samples, workload.workload.definition);
        }
    }

    for (std::size_t mode_index = 0; mode_index < 2; ++mode_index) {
        const ModeResult& reference =
            workload.implementations[0].modes[mode_index];
        const ModeResult& neon = workload.implementations[1].modes[mode_index];
        for (ImplementationResult& implementation : workload.implementations) {
            ModeResult& mode = implementation.modes[mode_index];
            if (!mode.valid) {
                continue;
            }
            if (reference.valid) {
                mode.speedup_vs_reference =
                    reference.statistics.median_latency_ms /
                    mode.statistics.median_latency_ms;
            }
            if (neon.valid) {
                mode.speedup_vs_neon =
                    neon.statistics.median_latency_ms /
                    mode.statistics.median_latency_ms;
            }
        }
    }
}

void write_optional_number(
    std::ostream& output,
    const std::optional<double>& value,
    int precision = 6) {
    if (!value.has_value()) {
        output << "null";
        return;
    }
    output << tools::format_float(*value, precision);
}

void write_double_array(
    std::ostream& output,
    const std::vector<double>& values,
    int precision) {
    output << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        output << tools::format_float(values[index], precision);
        if (index + 1 < values.size()) {
            output << ", ";
        }
    }
    output << "]";
}

void write_size_array(
    std::ostream& output,
    const std::vector<std::size_t>& values) {
    output << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        output << values[index];
        if (index + 1 < values.size()) {
            output << ", ";
        }
    }
    output << "]";
}

void write_correctness_json(
    std::ostream& output,
    const CorrectnessResult& correctness) {
    output << "        \"correctness\": {\n";
    output << "          \"executed\": "
           << (correctness.executed ? "true" : "false") << ",\n";
    output << "          \"passed\": "
           << (correctness.passed ? "true" : "false") << ",\n";
    output << "          \"result_ok\": "
           << (correctness.result_ok ? "true" : "false") << ",\n";
    output << "          \"actual_implementation\": \""
           << tools::escape_json(correctness.actual_implementation) << "\",\n";
    output << "          \"ground_truth_match\": "
           << (correctness.ground_truth_match ? "true" : "false") << ",\n";
    output << "          \"candidate_count_match\": "
           << (correctness.candidate_count_match ? "true" : "false") << ",\n";
    output << "          \"best_candidate_match\": "
           << (correctness.best_candidate_match ? "true" : "false") << ",\n";
    output << "          \"second_best_candidate_match\": "
           << (correctness.second_best_candidate_match ? "true" : "false")
           << ",\n";
    output << "          \"best_score_difference\": "
           << tools::format_float(correctness.best_score_difference, 9) << ",\n";
    output << "          \"best_score_tolerance\": "
           << tools::format_float(correctness.best_score_tolerance, 9) << ",\n";
    output << "          \"best_score_within_tolerance\": "
           << (correctness.best_score_within_tolerance ? "true" : "false")
           << ",\n";
    output << "          \"second_best_score_difference\": "
           << tools::format_float(correctness.second_best_score_difference, 9)
           << ",\n";
    output << "          \"second_best_score_tolerance\": "
           << tools::format_float(correctness.second_best_score_tolerance, 9)
           << ",\n";
    output << "          \"second_best_score_within_tolerance\": "
           << (correctness.second_best_score_within_tolerance ? "true" : "false")
           << ",\n";
    output << "          \"error\": \""
           << tools::escape_json(correctness.error_message) << "\"\n";
    output << "        },\n";
}

void write_mode_json(std::ostream& output, const ModeResult& mode, bool trailing) {
    output << "          {\n";
    output << "            \"name\": \"" << timing_mode_name(mode.mode)
           << "\",\n";
    output << "            \"valid\": " << (mode.valid ? "true" : "false")
           << ",\n";
    output << "            \"allocations_included\": "
           << (mode.mode == TimingMode::kSetupInclusive ? "true" : "false")
           << ",\n";
    output << "            \"speedup_vs_reference\": ";
    write_optional_number(output, mode.speedup_vs_reference);
    output << ",\n";
    output << "            \"speedup_vs_neon\": ";
    write_optional_number(output, mode.speedup_vs_neon);
    output << ",\n";
    if (mode.valid) {
        const auto& stats = mode.statistics;
        output << "            \"timing\": {\n";
        output << "              \"sample_count\": " << stats.sample_count
               << ",\n";
        output << "              \"total_operation_count\": "
               << stats.total_operation_count << ",\n";
        output << "              \"total_timed_duration_ms\": "
               << tools::format_float(stats.total_timed_duration_ms, 6) << ",\n";
        output << "              \"latency_ms\": {\n";
        output << "                \"median\": "
               << tools::format_float(stats.median_latency_ms, 9) << ",\n";
        output << "                \"mean\": "
               << tools::format_float(stats.mean_latency_ms, 9) << ",\n";
        output << "                \"standard_deviation\": "
               << tools::format_float(stats.standard_deviation_ms, 9) << ",\n";
        output << "                \"minimum\": "
               << tools::format_float(stats.minimum_latency_ms, 9) << ",\n";
        output << "                \"maximum\": "
               << tools::format_float(stats.maximum_latency_ms, 9) << ",\n";
        output << "                \"p50\": "
               << tools::format_float(stats.p50_latency_ms, 9) << ",\n";
        output << "                \"p95\": "
               << tools::format_float(stats.p95_latency_ms, 9) << "\n";
        output << "              },\n";
        output << "              \"candidate_correlations_per_second\": "
               << tools::format_float(
                      stats.candidate_correlations_per_second, 3)
               << ",\n";
        output << "              \"complex_macs_per_second\": "
               << tools::format_float(stats.complex_macs_per_second, 3) << ",\n";
        output << "              \"operations_per_sample\": ";
        write_size_array(output, stats.operations_per_sample);
        output << ",\n";
        output << "              \"sample_block_duration_ms\": ";
        write_double_array(output, stats.block_duration_samples_ms, 6);
        output << ",\n";
        output << "              \"per_sample_latency_ms\": ";
        write_double_array(output, stats.latency_samples_ms, 9);
        output << "\n";
        output << "            },\n";
    } else {
        output << "            \"timing\": null,\n";
    }
    output << "            \"error\": \""
           << tools::escape_json(mode.error_message) << "\"\n";
    output << "          }" << (trailing ? "," : "") << "\n";
}

std::string serialize_json(const BenchmarkReport& report) {
    std::ostringstream output;
    output << "{\n";
    output << "  \"schema_version\": 1,\n";
    output << "  \"ok\": " << (report.ok ? "true" : "false") << ",\n";
    output << "  \"benchmark\": {\n";
    output << "    \"name\": \"acquisition-workload-sweep\",\n";
    output << "    \"scope\": \"Local timing of the checked scalar, NEON, and "
              "available SME2 acquisition implementations; no performance "
              "conclusion is embedded in the report.\",\n";
    output << "    \"timestamp_utc\": \"" << report.timestamp_utc << "\",\n";
    output << "    \"git_commit_sha\": \"" << SATCOMFEC_BENCHMARK_GIT_SHA
           << "\",\n";
    output << "    \"git_working_tree_dirty_at_build\": \""
           << SATCOMFEC_BENCHMARK_GIT_DIRTY << "\",\n";
    output << "    \"timer\": \"std::chrono::steady_clock\",\n";
    output << "    \"warmup_rounds\": " << report.options.warmup_rounds
           << ",\n";
    output << "    \"timed_sample_count\": "
           << report.options.timed_sample_count << ",\n";
    output << "    \"minimum_sample_duration_ms\": "
           << tools::format_float(
                  report.options.minimum_sample_duration_ms, 3)
           << ",\n";
    output << "    \"deterministic_seed\": "
           << report.options.deterministic_seed << ",\n";
    output << "    \"execution_order\": \"Fisher-Yates shuffle of every valid "
              "implementation/mode pair before each warm-up and timed sample "
              "block\",\n";
    output << "    \"percentile_method\": \"nearest-rank\",\n";
    output << "    \"standard_deviation\": \"sample standard deviation (N-1)\",\n";
    output << "    \"steady_state_definition\": \"Prevalidated input, precomputed "
              "CFO weights, and preallocated SME2 workspace; complete "
              "correlation, magnitude-squared scoring, and top-two reduction "
              "are timed.\",\n";
    output << "    \"setup_inclusive_definition\": \"Includes acquisition-plan "
              "allocation/CFO-table generation and checked execution; SME2 "
              "additionally includes workspace allocation, packing, and "
              "release.\"\n";
    output << "  },\n";
    output << "  \"host\": {\n";
    output << "    \"os\": \"" << tools::escape_json(report.host.os_name)
           << "\",\n";
    output << "    \"os_release\": \""
           << tools::escape_json(report.host.os_release) << "\",\n";
    output << "    \"architecture\": \""
           << tools::escape_json(report.host.architecture) << "\",\n";
    output << "    \"cpu_model\": \""
           << tools::escape_json(report.host.cpu_model) << "\"\n";
    output << "  },\n";
    output << "  \"build\": {\n";
    output << "    \"compiler\": \"" << compiler_name() << "\",\n";
    output << "    \"compiler_version\": \""
           << tools::escape_json(compiler_version()) << "\",\n";
    output << "    \"cxx_standard\": \"C++17\",\n";
    output << "    \"build_type\": \"" << SATCOMFEC_BENCHMARK_BUILD_TYPE
           << "\",\n";
    output << "    \"common_compile_flags\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_COMMON_FLAGS) << "\",\n";
    output << "    \"source_compile_flags\": {\n";
    output << "      \"reference\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_REFERENCE_FLAGS) << "\",\n";
    output << "      \"neon\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_NEON_FLAGS) << "\",\n";
    output << "      \"sme2\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_SME2_FLAGS) << "\"\n";
    output << "    }\n";
    output << "  },\n";
    output << "  \"runtime_cpu_features\": {\n";
    output << "    \"neon_hardware_supported\": "
           << (report.host.neon_hardware_supported ? "true" : "false") << ",\n";
    output << "    \"neon_kernel_compiled\": "
           << (acquisition::acquisition_neon_kernel_compiled() ? "true" : "false")
           << ",\n";
    output << "    \"neon_vector_bits\": "
           << (acquisition::acquisition_neon_kernel_compiled() ? "128" : "null")
           << ",\n";
    output << "    \"sve_hardware_supported\": "
           << (report.host.sve_hardware_supported ? "true" : "false") << ",\n";
    output << "    \"sve_acquisition_implementation_included\": false,\n";
    output << "    \"sme_hardware_supported\": "
           << (report.host.sme_hardware_supported ? "true" : "false") << ",\n";
    output << "    \"sme2_hardware_supported\": "
           << (report.host.sme2_hardware_supported ? "true" : "false") << ",\n";
    output << "    \"sme2_kernel_compiled\": "
           << (acquisition::acquisition_sme2_kernel_compiled() ? "true" : "false")
           << ",\n";
    output << "    \"sme2_runtime_supported\": "
           << (acquisition::acquisition_sme2_runtime_supported() ? "true" : "false")
           << ",\n";
    const std::size_t streaming_lanes =
        acquisition::acquisition_sme2_streaming_lanes_f32();
    output << "    \"sme_streaming_vector_bits\": ";
    if (streaming_lanes == 0) {
        output << "null\n";
    } else {
        output << streaming_lanes * 32 << "\n";
    }
    output << "  },\n";
    output << "  \"workloads\": [\n";

    for (std::size_t workload_index = 0;
         workload_index < report.workloads.size();
         ++workload_index) {
        const WorkloadResult& workload_result = report.workloads[workload_index];
        const auto& workload = workload_result.workload;
        const auto& definition = workload.definition;
        output << "    {\n";
        output << "      \"name\": \"" << definition.name << "\",\n";
        output << "      \"definition\": {\n";
        output << "        \"iq_sample_count\": " << definition.iq_sample_count
               << ",\n";
        output << "        \"preamble_length\": " << definition.preamble_length
               << ",\n";
        output << "        \"timing_hypothesis_count\": "
               << definition.timing_hypothesis_count << ",\n";
        output << "        \"cfo_hypothesis_count\": "
               << definition.cfo_hypothesis_count << ",\n";
        output << "        \"candidate_correlation_count\": "
               << candidate_count(definition) << ",\n";
        output << "        \"complex_mac_count\": "
               << checked_complex_mac_count(definition) << ",\n";
        output << "        \"sample_rate_hz\": "
               << tools::format_float(kSampleRateHz, 1) << ",\n";
        output << "        \"cfo_spacing_hz\": "
               << tools::format_float(kCfoSpacingHz, 1) << "\n";
        output << "      },\n";
        output << "      \"fixture\": {\n";
        output << "        \"synthetic\": true,\n";
        output << "        \"seed\": " << workload.fixture_seed << ",\n";
        output << "        \"execution_order_seed\": "
               << (workload.fixture_seed ^ 0x9E3779B97F4A7C15ULL) << ",\n";
        output << "        \"true_timing_offset\": "
               << workload.true_timing_offset << ",\n";
        output << "        \"true_cfo_hz\": "
               << tools::format_float(workload.true_cfo_hz, 1) << "\n";
        output << "      },\n";
        output << "      \"reference_result\": {\n";
        output << "        \"valid\": "
               << (workload_result.reference_valid ? "true" : "false") << ",\n";
        output << "        \"detected_timing_offset\": "
               << workload_result.reference_result.best.hypothesis.timing_offset
               << ",\n";
        output << "        \"detected_cfo_hz\": "
               << tools::format_float(
                      workload_result.reference_result.best.hypothesis
                          .frequency_offset_hz,
                      1)
               << ",\n";
        output << "        \"best_score\": "
               << tools::format_float(
                      workload_result.reference_result.best.score, 9)
               << ",\n";
        output << "        \"second_best_score\": "
               << tools::format_float(
                      workload_result.reference_result.second_best.score, 9)
               << "\n";
        output << "      },\n";
        output << "      \"execution_order_by_sample\": [\n";
        for (std::size_t sample_index = 0;
             sample_index < workload_result.execution_order_by_sample.size();
             ++sample_index) {
            const auto& order =
                workload_result.execution_order_by_sample[sample_index];
            output << "        [";
            for (std::size_t order_index = 0; order_index < order.size(); ++order_index) {
                output << "\"" << tools::escape_json(order[order_index]) << "\"";
                if (order_index + 1 < order.size()) {
                    output << ", ";
                }
            }
            output << "]"
                   << (sample_index + 1 <
                               workload_result.execution_order_by_sample.size()
                           ? ","
                           : "")
                   << "\n";
        }
        output << "      ],\n";
        output << "      \"implementations\": [\n";
        for (std::size_t implementation_index = 0;
             implementation_index < workload_result.implementations.size();
             ++implementation_index) {
            const ImplementationResult& implementation =
                workload_result.implementations[implementation_index];
            output << "      {\n";
            output << "        \"requested_implementation\": \""
                   << implementation.name << "\",\n";
            output << "        \"implementation_class\": \""
                   << implementation.implementation_class << "\",\n";
            output << "        \"mechanism\": \""
                   << tools::escape_json(implementation.mechanism) << "\",\n";
            output << "        \"available\": "
                   << (implementation.available ? "true" : "false") << ",\n";
            output << "        \"executed\": "
                   << (implementation.executed ? "true" : "false") << ",\n";
            output << "        \"unavailable_reason\": \""
                   << tools::escape_json(implementation.unavailable_reason)
                   << "\",\n";
            write_correctness_json(output, implementation.correctness);
            output << "        \"modes\": [\n";
            for (std::size_t mode_index = 0;
                 mode_index < implementation.modes.size();
                 ++mode_index) {
                write_mode_json(
                    output,
                    implementation.modes[mode_index],
                    mode_index + 1 < implementation.modes.size());
            }
            output << "        ]\n";
            output << "      }"
                   << (implementation_index + 1 <
                               workload_result.implementations.size()
                           ? ","
                           : "")
                   << "\n";
        }
        output << "      ]\n";
        output << "    }"
               << (workload_index + 1 < report.workloads.size() ? "," : "")
               << "\n";
    }

    output << "  ],\n";
    output << "  \"result_sink\": " << g_result_sink << ",\n";
    output << "  \"limitations\": [\n";
    output << "    \"Results describe one local process run and are not "
              "device-independent performance claims.\",\n";
    output << "    \"No CPU affinity, frequency locking, thermal stabilization, "
              "or energy measurement is performed.\",\n";
    output << "    \"Synthetic workload classes are engineering sweeps, not "
              "mission-derived waveform standards.\",\n";
    output << "    \"Steady-state timing includes score calculation and top-two "
              "selection; it is not an instruction-only microbenchmark.\",\n";
    output << "    \"The setup-inclusive mode includes implementation-specific "
              "setup costs and is not expected to rank paths identically to "
              "steady state.\",\n";
    output << "    \"The legacy Viterbi decoder benchmark is independent and is "
              "not evidence for acquisition performance.\"\n";
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (char character : value) {
        escaped += character == '\"' ? "\"\"" : std::string(1, character);
    }
    escaped += "\"";
    return escaped;
}

std::string serialize_csv(const BenchmarkReport& report) {
    std::ostringstream output;
    output << "workload,iq_samples,preamble_length,timing_hypotheses,cfo_hypotheses,"
              "implementation,implementation_class,available,correctness_passed,mode,"
              "timing_valid,sample_count,median_latency_ms,mean_latency_ms,"
              "standard_deviation_ms,p95_latency_ms,correlations_per_second,"
              "complex_macs_per_second,speedup_vs_reference,speedup_vs_neon,error\n";
    for (const WorkloadResult& workload : report.workloads) {
        for (const ImplementationResult& implementation : workload.implementations) {
            for (const ModeResult& mode : implementation.modes) {
                const auto& definition = workload.workload.definition;
                output << definition.name << ',' << definition.iq_sample_count << ','
                       << definition.preamble_length << ','
                       << definition.timing_hypothesis_count << ','
                       << definition.cfo_hypothesis_count << ','
                       << implementation.name << ','
                       << implementation.implementation_class << ','
                       << (implementation.available ? "true" : "false") << ','
                       << (implementation.correctness.passed ? "true" : "false")
                       << ',' << timing_mode_name(mode.mode) << ','
                       << (mode.valid ? "true" : "false") << ',';
                if (mode.valid) {
                    output << mode.statistics.sample_count << ','
                           << tools::format_float(
                                  mode.statistics.median_latency_ms, 9)
                           << ','
                           << tools::format_float(
                                  mode.statistics.mean_latency_ms, 9)
                           << ','
                           << tools::format_float(
                                  mode.statistics.standard_deviation_ms, 9)
                           << ','
                           << tools::format_float(
                                  mode.statistics.p95_latency_ms, 9)
                           << ','
                           << tools::format_float(
                                  mode.statistics.candidate_correlations_per_second,
                                  3)
                           << ','
                           << tools::format_float(
                                  mode.statistics.complex_macs_per_second, 3)
                           << ',';
                    if (mode.speedup_vs_reference.has_value()) {
                        output << tools::format_float(
                            *mode.speedup_vs_reference, 6);
                    }
                    output << ',';
                    if (mode.speedup_vs_neon.has_value()) {
                        output << tools::format_float(*mode.speedup_vs_neon, 6);
                    }
                } else {
                    output << ",,,,,,,,";
                }
                const std::string error = mode.error_message.empty()
                                              ? implementation.unavailable_reason
                                              : mode.error_message;
                output << ',' << csv_escape(error) << '\n';
            }
        }
    }
    return output.str();
}

std::vector<WorkloadDefinition> select_workloads(
    const BenchmarkOptions& options,
    std::string& error_message) {
    if (options.workload_names.empty()) {
        return acquisition_benchmark_workloads();
    }

    std::vector<WorkloadDefinition> selected;
    std::set<std::string> seen;
    for (const std::string& requested : options.workload_names) {
        if (!seen.insert(requested).second) {
            error_message = "duplicate workload requested: " + requested;
            return {};
        }
        const auto& definitions = acquisition_benchmark_workloads();
        const auto match = std::find_if(
            definitions.begin(),
            definitions.end(),
            [&requested](const WorkloadDefinition& definition) {
                return definition.name == requested;
            });
        if (match == definitions.end()) {
            error_message = "unknown workload: " + requested;
            return {};
        }
        selected.push_back(*match);
    }
    return selected;
}

}  // namespace

const std::vector<WorkloadDefinition>& acquisition_benchmark_workloads() {
    static const std::vector<WorkloadDefinition> workloads = {
        {"small", 2048, 64, 1024, 5},
        {"medium", 8192, 128, 4096, 9},
        {"large", 32768, 256, 16384, 17},
        {"very-large", 65536, 512, 32768, 25},
    };
    return workloads;
}

BenchmarkArtifacts run_acquisition_benchmark(const BenchmarkOptions& options) {
    BenchmarkArtifacts artifacts;
    if (options.timed_sample_count < 3 ||
        !std::isfinite(options.minimum_sample_duration_ms) ||
        options.minimum_sample_duration_ms <= 0.0) {
        artifacts.error_message = "benchmark options are invalid";
        return artifacts;
    }

    std::string selection_error;
    const std::vector<WorkloadDefinition> selected =
        select_workloads(options, selection_error);
    if (!selection_error.empty()) {
        artifacts.error_message = selection_error;
        return artifacts;
    }

    BenchmarkReport report;
    report.options = options;
    report.timestamp_utc = utc_timestamp();
    report.host = collect_host_metadata();
    try {
        for (std::size_t index = 0; index < selected.size(); ++index) {
            WorkloadResult workload;
            std::string workload_error;
            const std::uint64_t fixture_seed =
                options.deterministic_seed ^ stable_name_hash(selected[index].name);
            if (!prepare_and_verify_workload(
                    selected[index], fixture_seed, workload, workload_error)) {
                artifacts.error_message = selected[index].name + ": " + workload_error;
                return artifacts;
            }
            for (const ImplementationResult& implementation :
                 workload.implementations) {
                if (implementation.available &&
                    !implementation.correctness.passed) {
                    report.ok = false;
                }
            }
            std::mt19937_64 order_generator(
                fixture_seed ^ 0x9E3779B97F4A7C15ULL);
            run_timing(workload, options, order_generator, report.ok);
            report.workloads.push_back(std::move(workload));
        }
    } catch (const std::bad_alloc&) {
        artifacts.error_message = "benchmark allocation failed for the selected workload";
        return artifacts;
    }

    artifacts.ok = report.ok;
    artifacts.json = serialize_json(report);
    artifacts.csv = serialize_csv(report);
    return artifacts;
}

}  // namespace satcomfec::benchmark
