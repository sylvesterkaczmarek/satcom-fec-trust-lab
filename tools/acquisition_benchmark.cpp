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
#include <cerrno>
#include <sys/auxv.h>
#if defined(__has_include)
#if __has_include(<asm/hwcap.h>)
#include <asm/hwcap.h>
#endif
#endif
#endif

#if defined(__ANDROID__)
#include <sys/system_properties.h>
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

#ifndef SATCOMFEC_BENCHMARK_TARGET
#define SATCOMFEC_BENCHMARK_TARGET "host-native"
#endif

#ifndef SATCOMFEC_BENCHMARK_BUILD_SYSTEM
#define SATCOMFEC_BENCHMARK_BUILD_SYSTEM "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_CMAKE_VERSION
#define SATCOMFEC_BENCHMARK_CMAKE_VERSION "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_CMAKE_GENERATOR
#define SATCOMFEC_BENCHMARK_CMAKE_GENERATOR "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_COMPILER_PATH
#define SATCOMFEC_BENCHMARK_COMPILER_PATH "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_COMPILER_ID
#define SATCOMFEC_BENCHMARK_COMPILER_ID "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_COMPILER_TARGET
#define SATCOMFEC_BENCHMARK_COMPILER_TARGET "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_SYSTEM_NAME
#define SATCOMFEC_BENCHMARK_SYSTEM_NAME "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_SYSTEM_PROCESSOR
#define SATCOMFEC_BENCHMARK_SYSTEM_PROCESSOR "unavailable"
#endif

#ifndef SATCOMFEC_BENCHMARK_WARNINGS_ENABLED
#define SATCOMFEC_BENCHMARK_WARNINGS_ENABLED "OFF"
#endif

#ifndef SATCOMFEC_BENCHMARK_WARNINGS_AS_ERRORS
#define SATCOMFEC_BENCHMARK_WARNINGS_AS_ERRORS "OFF"
#endif

#ifndef SATCOMFEC_BENCHMARK_SANITIZERS_ENABLED
#define SATCOMFEC_BENCHMARK_SANITIZERS_ENABLED "OFF"
#endif

#ifndef SATCOMFEC_BENCHMARK_NEON_REQUESTED
#define SATCOMFEC_BENCHMARK_NEON_REQUESTED "OFF"
#endif

#ifndef SATCOMFEC_BENCHMARK_SME2_REQUESTED
#define SATCOMFEC_BENCHMARK_SME2_REQUESTED "OFF"
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
constexpr std::size_t kPerCaptureWindowCount = 2;

volatile std::uint64_t g_result_sink = 0;

bool build_option_enabled(const char* value) {
    return std::strcmp(value, "ON") == 0 || std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "TRUE") == 0;
}

enum class ImplementationId {
    kReference,
    kNeon,
    kSme2,
};

enum class TimingMode {
    kSteadyState,
    kPerCapture,
    kSetupInclusive,
};

struct ImplementationMemory {
    std::uint64_t reusable_plan_payload_bytes = 0;
    std::uint64_t per_capture_workspace_payload_bytes = 0;
    std::uint64_t correlation_output_payload_bytes = 0;
    std::uint64_t total_temporary_workspace_payload_bytes = 0;
    std::uint64_t total_temporary_workspace_capacity_bytes = 0;
    bool workspace_capacity_measured = false;
};

struct WorkloadMemory {
    std::uint64_t common_input_capture_payload_bytes = 0;
    std::uint64_t benchmark_capture_bank_payload_bytes = 0;
    std::uint64_t shared_plan_allocated_capacity_bytes = 0;
};

struct PreparedWorkload {
    WorkloadDefinition definition;
    std::vector<ComplexF> received_iq;
    std::vector<std::vector<ComplexF>> per_capture_iq;
    std::vector<ComplexF> preamble;
    acquisition::AcquisitionConfig config;
    AcquisitionPlan plan;
    Sme2AcquisitionWorkspace sme2_workspace;
    std::size_t true_timing_offset = 0;
    double true_cfo_hz = 0.0;
    std::uint64_t fixture_seed = 0;
    WorkloadMemory memory;
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
    std::size_t per_capture_case_count = 0;
    std::size_t per_capture_cases_passed = 0;
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
    ImplementationMemory memory;
    std::vector<ModeResult> modes;
    std::size_t next_per_capture_index = 0;
};

struct WorkloadResult {
    PreparedWorkload workload;
    AcquisitionResult reference_result;
    std::vector<AcquisitionResult> per_capture_reference_results;
    bool reference_valid = false;
    bool accelerated_weight_tables_match = false;
    std::vector<ImplementationResult> implementations;
    std::vector<std::vector<std::string>> execution_order_by_sample;
};

struct HostMetadata {
    std::string os_name = "unavailable";
    std::string os_release = "unavailable";
    std::string architecture = "unavailable";
    std::string cpu_model = "unavailable";
    std::string cpu_model_source = "unavailable";
    std::string device_model = "unavailable";
    std::string android_version = "unavailable";
    std::string android_api_level = "unavailable";
    std::string android_abi = "unavailable";
    std::uint64_t auxiliary_vector_hwcap = 0;
    std::uint64_t auxiliary_vector_hwcap2 = 0;
    bool auxiliary_vector_available = false;
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
        case TimingMode::kPerCapture:
            return "per-capture";
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

std::uint64_t storage_bytes(std::size_t element_count, std::size_t element_size) {
    if (element_size != 0 &&
        element_count >
            std::numeric_limits<std::uint64_t>::max() / element_size) {
        return 0;
    }
    return static_cast<std::uint64_t>(element_count) * element_size;
}

bool equal_float_bits(float left, float right) {
    return std::memcmp(&left, &right, sizeof(float)) == 0;
}

bool accelerated_weight_tables_match(const AcquisitionPlan& plan) {
    const std::size_t frequency_count = plan.frequency_hypotheses.size();
    if (frequency_count != 0 &&
        plan.preamble_length >
            std::numeric_limits<std::size_t>::max() / frequency_count) {
        return false;
    }
    const std::size_t expected_count = frequency_count * plan.preamble_length;
    if (plan.matched_filter_weights_real_f32.size() != expected_count ||
        plan.matched_filter_weights_imag_f32.size() != expected_count) {
        return false;
    }

    for (std::size_t frequency_index = 0;
         frequency_index < frequency_count;
         ++frequency_index) {
        const auto& interleaved =
            plan.frequency_hypotheses[frequency_index].matched_filter_weights_f32;
        if (interleaved.size() != plan.preamble_length) {
            return false;
        }
        for (std::size_t sample_index = 0;
             sample_index < plan.preamble_length;
             ++sample_index) {
            const std::size_t flattened_index =
                frequency_index * plan.preamble_length + sample_index;
            if (!equal_float_bits(
                    interleaved[sample_index].real(),
                    plan.matched_filter_weights_real_f32[flattened_index]) ||
                !equal_float_bits(
                    interleaved[sample_index].imag(),
                    plan.matched_filter_weights_imag_f32[flattened_index])) {
                return false;
            }
        }
    }
    return true;
}

std::uint64_t shared_plan_allocated_capacity_bytes(const AcquisitionPlan& plan) {
    std::uint64_t bytes =
        storage_bytes(plan.timing_offsets.capacity(), sizeof(std::size_t)) +
        storage_bytes(
            plan.frequency_hypotheses.capacity(),
            sizeof(acquisition::PreparedFrequencyHypothesis)) +
        storage_bytes(
            plan.matched_filter_weights_real_f32.capacity(), sizeof(float)) +
        storage_bytes(
            plan.matched_filter_weights_imag_f32.capacity(), sizeof(float));
    for (const auto& frequency : plan.frequency_hypotheses) {
        bytes += storage_bytes(
            frequency.matched_filter_weights.capacity(),
            sizeof(std::complex<double>));
        bytes += storage_bytes(
            frequency.matched_filter_weights_f32.capacity(), sizeof(ComplexF));
    }
    return bytes;
}

ImplementationMemory implementation_memory(
    ImplementationId id,
    const PreparedWorkload& workload) {
    ImplementationMemory memory;
    const std::size_t timing_count = workload.plan.timing_offsets.size();
    const std::size_t frequency_count =
        workload.plan.frequency_hypotheses.size();
    const std::size_t weight_count =
        frequency_count * workload.plan.preamble_length;
    const std::uint64_t common_plan_bytes =
        storage_bytes(timing_count, sizeof(std::size_t)) +
        storage_bytes(frequency_count, sizeof(double));

    switch (id) {
        case ImplementationId::kReference:
            memory.reusable_plan_payload_bytes =
                common_plan_bytes +
                storage_bytes(weight_count, sizeof(std::complex<double>));
            break;
        case ImplementationId::kNeon:
            memory.reusable_plan_payload_bytes =
                common_plan_bytes + storage_bytes(weight_count, sizeof(ComplexF));
            break;
        case ImplementationId::kSme2: {
            const std::size_t packed_sample_count =
                timing_count * workload.plan.preamble_length;
            const std::size_t correlation_count =
                timing_count * frequency_count;
            memory.reusable_plan_payload_bytes =
                common_plan_bytes + 2 * storage_bytes(weight_count, sizeof(float));
            memory.per_capture_workspace_payload_bytes =
                2 * storage_bytes(packed_sample_count, sizeof(float));
            memory.correlation_output_payload_bytes =
                2 * storage_bytes(correlation_count, sizeof(float));
            memory.total_temporary_workspace_payload_bytes =
                memory.per_capture_workspace_payload_bytes +
                memory.correlation_output_payload_bytes;
            memory.total_temporary_workspace_capacity_bytes =
                storage_bytes(
                    workload.sme2_workspace
                        .received_real_by_sample_and_timing.capacity(),
                    sizeof(float)) +
                storage_bytes(
                    workload.sme2_workspace
                        .received_imag_by_sample_and_timing.capacity(),
                    sizeof(float)) +
                storage_bytes(
                    workload.sme2_workspace
                        .correlation_real_by_frequency_and_timing.capacity(),
                    sizeof(float)) +
                storage_bytes(
                    workload.sme2_workspace
                        .correlation_imag_by_frequency_and_timing.capacity(),
                    sizeof(float));
            memory.workspace_capacity_measured =
                !workload.sme2_workspace.received_real_by_sample_and_timing.empty();
            break;
        }
    }
    return memory;
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

#if defined(__ANDROID__)
std::string android_system_property(const char* name) {
    char value[PROP_VALUE_MAX] {};
    const int length = __system_property_get(name, value);
    return length > 0 ? std::string(value, static_cast<std::size_t>(length))
                      : "unavailable";
}
#endif

std::string hexadecimal_capability(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

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
    host.cpu_model_source = "sysctl machdep.cpu.brand_string";
    host.device_model = query_sysctl_string("hw.model");
    host.neon_hardware_supported = query_sysctl_int("hw.optional.neon");
    host.sme_hardware_supported = query_sysctl_int("hw.optional.arm.FEAT_SME");
    host.sme2_hardware_supported = query_sysctl_int("hw.optional.arm.FEAT_SME2");
#elif defined(__linux__)
    host.cpu_model = linux_cpu_model();
    host.cpu_model_source = "/proc/cpuinfo";
#if defined(__ANDROID__)
    host.os_name = "Android";
    host.android_version = android_system_property("ro.build.version.release");
    host.android_api_level = android_system_property("ro.build.version.sdk");
    host.android_abi = android_system_property("ro.product.cpu.abi");
    host.device_model = android_system_property("ro.product.model");
#endif
    errno = 0;
    host.auxiliary_vector_hwcap = getauxval(AT_HWCAP);
    const bool hwcap_available = errno == 0;
    errno = 0;
    host.auxiliary_vector_hwcap2 = getauxval(AT_HWCAP2);
    const bool hwcap2_available = errno == 0;
    host.auxiliary_vector_available = hwcap_available && hwcap2_available;
#if defined(__aarch64__)
    host.neon_hardware_supported = true;
#endif
#if defined(HWCAP_ASIMD)
    host.neon_hardware_supported =
        (host.auxiliary_vector_hwcap & HWCAP_ASIMD) != 0;
#endif
#if defined(HWCAP_SVE)
    host.sve_hardware_supported =
        (host.auxiliary_vector_hwcap & HWCAP_SVE) != 0;
#endif
#if defined(HWCAP2_SME)
    host.sme_hardware_supported =
        (host.auxiliary_vector_hwcap2 & HWCAP2_SME) != 0;
#endif
#if defined(HWCAP2_SME2)
    host.sme2_hardware_supported =
        (host.auxiliary_vector_hwcap2 & HWCAP2_SME2) != 0;
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

    workload.per_capture_iq.reserve(kPerCaptureWindowCount);
    workload.per_capture_iq.push_back(workload.received_iq);
    for (std::size_t capture_index = 1;
         capture_index < kPerCaptureWindowCount;
         ++capture_index) {
        std::vector<ComplexF> capture = workload.received_iq;
        for (ComplexF& sample : capture) {
            sample += ComplexF(
                0.003F * generator.signed_unit(),
                0.003F * generator.signed_unit());
        }
        workload.per_capture_iq.push_back(std::move(capture));
    }

    if (!acquisition::prepare_acquisition_plan(
            workload.config,
            workload.preamble,
            workload.plan,
            error_message)) {
        return false;
    }

    workload.memory.common_input_capture_payload_bytes =
        storage_bytes(workload.received_iq.size(), sizeof(ComplexF));
    for (const auto& capture : workload.per_capture_iq) {
        workload.memory.benchmark_capture_bank_payload_bytes +=
            storage_bytes(capture.size(), sizeof(ComplexF));
    }
    workload.memory.shared_plan_allocated_capacity_bytes =
        shared_plan_allocated_capacity_bytes(workload.plan);
    return true;
}

AcquisitionResult execute_checked(
    ImplementationId id,
    const std::vector<ComplexF>& received_iq,
    PreparedWorkload& workload,
    std::string& error_message) {
    switch (id) {
        case ImplementationId::kReference:
            return acquisition::run_reference_acquisition(
                received_iq, workload.plan);
        case ImplementationId::kNeon:
            return acquisition::run_neon_acquisition(
                received_iq, workload.plan);
        case ImplementationId::kSme2:
            if (!acquisition::prepare_sme2_acquisition_workspace(
                    received_iq,
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

AcquisitionResult execute_per_capture(
    ImplementationId id,
    PreparedWorkload& workload,
    std::size_t capture_index) {
    const std::vector<ComplexF>& received_iq =
        workload.per_capture_iq[capture_index % workload.per_capture_iq.size()];
    switch (id) {
        case ImplementationId::kReference:
            return acquisition::run_reference_acquisition_steady_state(
                received_iq, workload.plan);
        case ImplementationId::kNeon:
            return acquisition::run_neon_acquisition_steady_state(
                received_iq, workload.plan);
        case ImplementationId::kSme2:
            acquisition::pack_sme2_acquisition_capture_steady_state(
                received_iq, workload.plan, workload.sme2_workspace);
            return acquisition::run_sme2_acquisition_steady_state(
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
    implementation.modes.resize(3);
    implementation.modes[0].mode = TimingMode::kSteadyState;
    implementation.modes[1].mode = TimingMode::kPerCapture;
    implementation.modes[2].mode = TimingMode::kSetupInclusive;
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

    result.accelerated_weight_tables_match =
        accelerated_weight_tables_match(result.workload.plan);
    if (!result.accelerated_weight_tables_match) {
        error_message =
            "NEON and SME2 float32 acquisition weights are not bitwise aligned";
        return false;
    }

    for (const auto& capture : result.workload.per_capture_iq) {
        AcquisitionResult reference = acquisition::run_reference_acquisition(
            capture, result.workload.plan);
        const bool reference_valid =
            reference.ok &&
            reference.best.hypothesis.timing_offset ==
                result.workload.true_timing_offset &&
            reference.best.hypothesis.frequency_offset_hz ==
                result.workload.true_cfo_hz &&
            reference.evaluated_candidate_count == candidate_count(definition);
        if (!reference_valid) {
            error_message = reference.error_message.empty()
                                ? "reference acquisition failed per-capture ground truth"
                                : reference.error_message;
            return false;
        }
        result.per_capture_reference_results.push_back(std::move(reference));
    }
    result.reference_result = result.per_capture_reference_results.front();
    result.reference_valid = true;

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
            implementation.id,
            result.workload.per_capture_iq.front(),
            result.workload,
            execution_error);
        implementation.executed = true;
        implementation.correctness = verify_correctness(
            implementation.id,
            candidate,
            result.reference_result,
            result.workload);
        implementation.correctness.per_capture_case_count = 1;
        implementation.correctness.per_capture_cases_passed =
            implementation.correctness.passed ? 1 : 0;
        if (!execution_error.empty() &&
            implementation.correctness.error_message.empty()) {
            implementation.correctness.error_message = execution_error;
        }

        for (std::size_t capture_index = 1;
             capture_index < result.workload.per_capture_iq.size();
             ++capture_index) {
            execution_error.clear();
            const AcquisitionResult per_capture_candidate = execute_checked(
                implementation.id,
                result.workload.per_capture_iq[capture_index],
                result.workload,
                execution_error);
            const CorrectnessResult per_capture_correctness = verify_correctness(
                implementation.id,
                per_capture_candidate,
                result.per_capture_reference_results[capture_index],
                result.workload);
            ++implementation.correctness.per_capture_case_count;
            if (per_capture_correctness.passed) {
                ++implementation.correctness.per_capture_cases_passed;
            } else {
                implementation.correctness.passed = false;
                if (implementation.correctness.error_message.empty()) {
                    implementation.correctness.error_message =
                        "per-capture correctness failed at capture index " +
                        std::to_string(capture_index) + ": " +
                        per_capture_correctness.error_message;
                }
            }
        }

        if (implementation.id == ImplementationId::kSme2 &&
            implementation.correctness.passed) {
            execution_error.clear();
            if (!acquisition::prepare_sme2_acquisition_workspace(
                    result.workload.per_capture_iq.front(),
                    result.workload.plan,
                    result.workload.sme2_workspace,
                    execution_error)) {
                implementation.correctness.passed = false;
                implementation.correctness.error_message = execution_error;
            }
        }
        for (ModeResult& mode : implementation.modes) {
            mode.valid = implementation.correctness.passed;
            if (!mode.valid) {
                mode.error_message = implementation.correctness.error_message;
            }
        }
    }
    for (ImplementationResult& implementation : result.implementations) {
        implementation.memory =
            implementation_memory(implementation.id, result.workload);
    }
    return true;
}

AcquisitionResult execute_task(
    const BenchmarkTask& task,
    WorkloadResult& workload) {
    ImplementationResult& implementation_result =
        workload.implementations[task.implementation_index];
    const ImplementationId implementation = implementation_result.id;
    const TimingMode mode =
        implementation_result.modes[task.mode_index].mode;
    switch (mode) {
        case TimingMode::kSteadyState:
            return execute_steady_state(implementation, workload.workload);
        case TimingMode::kPerCapture:
            return execute_per_capture(
                implementation,
                workload.workload,
                implementation_result.next_per_capture_index++);
        case TimingMode::kSetupInclusive:
            return execute_setup_inclusive(implementation, workload.workload);
    }
    AcquisitionResult failure;
    failure.implementation = "unavailable";
    failure.error_message = "unknown benchmark timing mode";
    return failure;
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

    const std::size_t mode_count = workload.implementations.front().modes.size();
    for (std::size_t mode_index = 0; mode_index < mode_count; ++mode_index) {
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
    output << "          \"per_capture_case_count\": "
           << correctness.per_capture_case_count << ",\n";
    output << "          \"per_capture_cases_passed\": "
           << correctness.per_capture_cases_passed << ",\n";
    output << "          \"error\": \""
           << tools::escape_json(correctness.error_message) << "\"\n";
    output << "        },\n";
}

void write_memory_json(
    std::ostream& output,
    const ImplementationMemory& memory) {
    output << "        \"memory_bytes\": {\n";
    output << "          \"reusable_plan_payload\": "
           << memory.reusable_plan_payload_bytes << ",\n";
    output << "          \"per_capture_workspace_payload\": "
           << memory.per_capture_workspace_payload_bytes << ",\n";
    output << "          \"correlation_output_payload\": "
           << memory.correlation_output_payload_bytes << ",\n";
    output << "          \"total_temporary_workspace_payload\": "
           << memory.total_temporary_workspace_payload_bytes << ",\n";
    output << "          \"total_temporary_workspace_allocated_capacity\": ";
    if (memory.workspace_capacity_measured) {
        output << memory.total_temporary_workspace_capacity_bytes << "\n";
    } else {
        output << "null\n";
    }
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
    output << "            \"new_capture_preparation_included\": "
           << (mode.mode == TimingMode::kSteadyState ? "false" : "true")
           << ",\n";
    output << "            \"reusable_plan_generation_included\": "
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
    output << "  \"schema_version\": 3,\n";
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
    output << "    \"per_capture_definition\": \"The acquisition plan and "
              "allocations are reused while prevalidated IQ windows are "
              "cycled. Reference and NEON read each interleaved window "
              "directly; SME2 sample-major packing for every supplied window "
              "is timed before correlation and top-two reduction.\",\n";
    output << "    \"setup_inclusive_definition\": \"Includes acquisition-plan "
              "allocation/CFO-table generation and checked execution; SME2 "
              "additionally includes workspace allocation, packing, and "
              "release.\",\n";
    output << "    \"fairness_contract\": {\n";
    output << "      \"candidate_set\": \"One shared timing/CFO hypothesis plan "
              "is used by every implementation.\",\n";
    output << "      \"accelerated_precision\": \"NEON and SME2 use bitwise-"
              "aligned float32 weights and float32 accumulation; the scalar "
              "oracle uses float64 weights and accumulation.\",\n";
    output << "      \"scoring_and_selection\": \"Magnitude-squared scoring and "
              "top-two candidate selection are included for every path.\",\n";
    output << "      \"fallback_policy\": \"An accelerated path is timed only "
              "when its reported implementation matches the requested path.\",\n";
    output << "      \"target_isolation\": \"Reference vectorization controls "
              "and accelerated target flags are source-specific; an SME2 "
              "build gives NEON a target with SVE and SME disabled.\",\n";
    output << "      \"dead_code_control\": \"Every timed block consumes a "
              "fingerprint of its final acquisition result through a volatile "
              "sink.\"\n";
    output << "    }\n";
    output << "  },\n";
    output << "  \"host\": {\n";
    output << "    \"os\": \"" << tools::escape_json(report.host.os_name)
           << "\",\n";
    output << "    \"os_release\": \""
           << tools::escape_json(report.host.os_release) << "\",\n";
    output << "    \"architecture\": \""
           << tools::escape_json(report.host.architecture) << "\",\n";
    output << "    \"cpu_model\": \""
           << tools::escape_json(report.host.cpu_model) << "\",\n";
    output << "    \"cpu_model_source\": \""
           << tools::escape_json(report.host.cpu_model_source) << "\",\n";
    output << "    \"device_model\": \""
           << tools::escape_json(report.host.device_model) << "\",\n";
    output << "    \"android\": {\n";
#if defined(__ANDROID__)
    output << "      \"build\": true,\n";
#else
    output << "      \"build\": false,\n";
#endif
    output << "      \"version\": \""
           << tools::escape_json(report.host.android_version) << "\",\n";
    output << "      \"api_level\": \""
           << tools::escape_json(report.host.android_api_level) << "\",\n";
    output << "      \"abi\": \""
           << tools::escape_json(report.host.android_abi) << "\"\n";
    output << "    }\n";
    output << "  },\n";
    output << "  \"build\": {\n";
    output << "    \"build_system\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_BUILD_SYSTEM) << "\",\n";
    output << "    \"target\": \"" << SATCOMFEC_BENCHMARK_TARGET << "\",\n";
    output << "    \"cmake_version\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_CMAKE_VERSION) << "\",\n";
    output << "    \"cmake_generator\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_CMAKE_GENERATOR) << "\",\n";
    output << "    \"system_name\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_SYSTEM_NAME) << "\",\n";
    output << "    \"system_processor\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_SYSTEM_PROCESSOR) << "\",\n";
    output << "    \"compiler\": \"" << compiler_name() << "\",\n";
    output << "    \"compiler_id\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_COMPILER_ID) << "\",\n";
    output << "    \"compiler_path\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_COMPILER_PATH) << "\",\n";
    output << "    \"compiler_target\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_COMPILER_TARGET) << "\",\n";
    output << "    \"compiler_version\": \""
           << tools::escape_json(compiler_version()) << "\",\n";
    output << "    \"cxx_standard\": \"C++17\",\n";
    output << "    \"build_type\": \"" << SATCOMFEC_BENCHMARK_BUILD_TYPE
           << "\",\n";
    output << "    \"common_compile_flags\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_COMMON_FLAGS) << "\",\n";
    output << "    \"options\": {\n";
    output << "      \"warnings_enabled\": "
           << (build_option_enabled(SATCOMFEC_BENCHMARK_WARNINGS_ENABLED)
                   ? "true"
                   : "false")
           << ",\n";
    output << "      \"warnings_as_errors\": "
           << (build_option_enabled(SATCOMFEC_BENCHMARK_WARNINGS_AS_ERRORS)
                   ? "true"
                   : "false")
           << ",\n";
    output << "      \"asan_ubsan_enabled\": "
           << (build_option_enabled(SATCOMFEC_BENCHMARK_SANITIZERS_ENABLED)
                   ? "true"
                   : "false")
           << ",\n";
    output << "      \"explicit_neon_requested\": "
           << (build_option_enabled(SATCOMFEC_BENCHMARK_NEON_REQUESTED)
                   ? "true"
                   : "false")
           << ",\n";
    output << "      \"explicit_sme2_requested\": "
           << (build_option_enabled(SATCOMFEC_BENCHMARK_SME2_REQUESTED)
                   ? "true"
                   : "false")
           << "\n";
    output << "    },\n";
    output << "    \"source_compile_flags\": {\n";
    output << "      \"reference\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_REFERENCE_FLAGS) << "\",\n";
    output << "      \"neon\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_NEON_FLAGS) << "\",\n";
    output << "      \"sme2\": \""
           << tools::escape_json(SATCOMFEC_BENCHMARK_SME2_FLAGS) << "\"\n";
    output << "    },\n";
    output << "    \"source_files\": {\n";
    output << "      \"reference\": \"src/acquisition/acquisition_reference.cpp\",\n";
    output << "      \"neon\": \"src/acquisition/acquisition_neon.cpp\",\n";
    output << "      \"sme2\": \"src/acquisition/acquisition_sme2.cpp\"\n";
    output << "    }\n";
    output << "  },\n";
    output << "  \"runtime_cpu_features\": {\n";
    output << "    \"auxiliary_vector_available\": "
           << (report.host.auxiliary_vector_available ? "true" : "false")
           << ",\n";
    output << "    \"auxv_hwcap_hex\": \""
           << hexadecimal_capability(report.host.auxiliary_vector_hwcap)
           << "\",\n";
    output << "    \"auxv_hwcap2_hex\": \""
           << hexadecimal_capability(report.host.auxiliary_vector_hwcap2)
           << "\",\n";
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
        output << "      \"memory_accounting\": {\n";
        output << "        \"scope\": \"Logical payload bytes unless the field "
                  "explicitly names allocated capacity; allocator bookkeeping "
                  "and stack objects are excluded.\",\n";
        output << "        \"common_input_capture_payload_bytes\": "
               << workload.memory.common_input_capture_payload_bytes << ",\n";
        output << "        \"benchmark_capture_bank_payload_bytes\": "
               << workload.memory.benchmark_capture_bank_payload_bytes << ",\n";
        output << "        \"shared_plan_allocated_capacity_bytes\": "
               << workload.memory.shared_plan_allocated_capacity_bytes << "\n";
        output << "      },\n";
        output << "      \"fixture\": {\n";
        output << "        \"synthetic\": true,\n";
        output << "        \"seed\": " << workload.fixture_seed << ",\n";
        output << "        \"per_capture_window_count\": "
               << workload.per_capture_iq.size() << ",\n";
        output << "        \"execution_order_seed\": "
               << (workload.fixture_seed ^ 0x9E3779B97F4A7C15ULL) << ",\n";
        output << "        \"true_timing_offset\": "
               << workload.true_timing_offset << ",\n";
        output << "        \"true_cfo_hz\": "
               << tools::format_float(workload.true_cfo_hz, 1) << "\n";
        output << "      },\n";
        output << "      \"fairness_checks\": {\n";
        output << "        \"shared_candidate_plan\": true,\n";
        output << "        \"accelerated_weight_tables_bitwise_equal\": "
               << (workload_result.accelerated_weight_tables_match
                       ? "true"
                       : "false")
               << ",\n";
        output << "        \"per_capture_reference_cases_valid\": "
               << (workload_result.per_capture_reference_results.size() ==
                           workload.per_capture_iq.size()
                       ? "true"
                       : "false")
               << ",\n";
        output << "        \"timed_execution_order_randomized\": true,\n";
        output << "        \"sme2_per_capture_packing_timed\": true\n";
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
            write_memory_json(output, implementation.memory);
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
    output << "    \"Per-capture timing reuses the plan and allocations but "
              "includes SME2 sample-major packing for every supplied IQ "
              "window.\",\n";
    output << "    \"The setup-inclusive mode includes implementation-specific "
              "setup costs and is not expected to rank paths identically to "
              "steady state.\",\n";
    output << "    \"The scalar oracle accumulates in float64; NEON and SME2 use "
              "the same float32 weights and float32 accumulation and are "
              "correctness-gated against the oracle.\",\n";
    output << "    \"Reported memory is payload/capacity accounting, not process "
              "resident-set size or allocator overhead.\",\n";
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
              "implementation,implementation_class,reusable_plan_payload_bytes,"
              "per_capture_workspace_payload_bytes,correlation_output_payload_bytes,"
              "total_temporary_workspace_payload_bytes,available,correctness_passed,mode,"
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
                       << implementation.memory.reusable_plan_payload_bytes << ','
                       << implementation.memory.per_capture_workspace_payload_bytes
                       << ','
                       << implementation.memory.correlation_output_payload_bytes
                       << ','
                       << implementation.memory
                              .total_temporary_workspace_payload_bytes
                       << ','
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
