#include "acquisition/acquisition_runner.h"
#include "json_output.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSampleRateHz = 48000.0;
constexpr double kCfoHz = 750.0;
// Four rounded operations per complex component per sample, plus margin for
// float32 weight conversion and different legal FMA contraction orders.
constexpr double kToleranceSafetyFactor = 8.0;

struct KernelCase {
    const char* name;
    std::size_t preamble_length;
    std::size_t timing_count;
    std::size_t timing_step;
    float signal_amplitude;
    float noise_amplitude;
};

struct CaseResult {
    std::string name;
    std::size_t preamble_length = 0;
    double correlation_component_tolerance = 0.0;
    double correlation_real_difference = 0.0;
    double correlation_imag_difference = 0.0;
    double score_tolerance = 0.0;
    double score_difference = 0.0;
    bool candidate_identity_match = false;
    bool second_best_candidate_identity_match = false;
    bool within_tolerance = false;
};

class DeterministicGenerator {
public:
    explicit DeterministicGenerator(std::uint32_t seed) : state_(seed) {}

    float signed_unit() {
        state_ = state_ * 1664525U + 1013904223U;
        const double unit = static_cast<double>(state_) /
                            static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        return static_cast<float>(2.0 * unit - 1.0);
    }

private:
    std::uint32_t state_;
};

std::vector<satcomfec::ComplexF> make_preamble(
    std::size_t length,
    DeterministicGenerator& generator) {
    std::vector<satcomfec::ComplexF> preamble;
    preamble.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        preamble.emplace_back(generator.signed_unit(), generator.signed_unit());
    }
    return preamble;
}

std::vector<satcomfec::ComplexF> make_received(
    const std::vector<satcomfec::ComplexF>& preamble,
    const std::vector<std::size_t>& timings,
    float signal_amplitude,
    float noise_amplitude,
    DeterministicGenerator& generator) {
    const std::size_t timing_offset = timings[2 * timings.size() / 3];
    std::vector<satcomfec::ComplexF> received(
        timings.back() + preamble.size() + 5);
    for (satcomfec::ComplexF& sample : received) {
        sample = {
            noise_amplitude * generator.signed_unit(),
            noise_amplitude * generator.signed_unit(),
        };
    }

    for (std::size_t index = 0; index < preamble.size(); ++index) {
        const double phase = 0.37 + 2.0 * kPi * kCfoHz *
                                        static_cast<double>(index) / kSampleRateHz;
        const std::complex<double> carrier = std::polar(1.0, phase);
        const std::complex<double> signal =
            static_cast<double>(signal_amplitude) *
            std::complex<double>(preamble[index].real(), preamble[index].imag()) *
            carrier;
        received[timing_offset + index] += satcomfec::ComplexF(
            static_cast<float>(signal.real()), static_cast<float>(signal.imag()));
    }
    return received;
}

std::vector<std::size_t> make_timings(std::size_t count, std::size_t step) {
    std::vector<std::size_t> timings;
    timings.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        timings.push_back(3 + index * step);
    }
    return timings;
}

double correlation_component_tolerance(
    const std::vector<satcomfec::ComplexF>& received,
    std::size_t timing_offset,
    const satcomfec::acquisition::PreparedFrequencyHypothesis& frequency) {
    double sum_of_product_magnitudes = 0.0;
    for (std::size_t index = 0; index < frequency.matched_filter_weights_f32.size();
         ++index) {
        sum_of_product_magnitudes +=
            std::abs(received[timing_offset + index]) *
            std::abs(frequency.matched_filter_weights_f32[index]);
    }
    return kToleranceSafetyFactor * std::numeric_limits<float>::epsilon() *
           static_cast<double>(frequency.matched_filter_weights_f32.size()) *
           sum_of_product_magnitudes;
}

CaseResult run_case(const KernelCase& definition, std::uint32_t seed) {
    DeterministicGenerator generator(seed);
    const std::vector<satcomfec::ComplexF> preamble =
        make_preamble(definition.preamble_length, generator);
    const std::vector<std::size_t> timings =
        make_timings(definition.timing_count, definition.timing_step);
    const std::vector<satcomfec::ComplexF> received = make_received(
        preamble,
        timings,
        definition.signal_amplitude,
        definition.noise_amplitude,
        generator);

    satcomfec::acquisition::AcquisitionConfig config;
    config.sample_rate_hz = kSampleRateHz;
    config.timing_offsets = timings;
    config.frequency_offsets_hz = {-kCfoHz, 0.0, kCfoHz};

    satcomfec::acquisition::AcquisitionPlan plan;
    std::string error_message;
    CaseResult case_result;
    case_result.name = definition.name;
    case_result.preamble_length = definition.preamble_length;
    if (!satcomfec::acquisition::prepare_acquisition_plan(
            config, preamble, plan, error_message)) {
        return case_result;
    }

    const satcomfec::acquisition::AcquisitionResult reference =
        satcomfec::acquisition::run_reference_acquisition(received, plan);
    const satcomfec::acquisition::AcquisitionResult neon =
        satcomfec::acquisition::run_neon_acquisition(received, plan);
    if (!reference.ok || !neon.ok || reference.implementation != "reference" ||
        neon.implementation != "neon") {
        return case_result;
    }

    case_result.candidate_identity_match =
        reference.best.hypothesis.timing_offset == neon.best.hypothesis.timing_offset &&
        reference.best.hypothesis.frequency_offset_hz ==
            neon.best.hypothesis.frequency_offset_hz;
    case_result.second_best_candidate_identity_match =
        reference.second_best.valid == neon.second_best.valid &&
        (!reference.second_best.valid ||
         (reference.second_best.hypothesis.timing_offset ==
              neon.second_best.hypothesis.timing_offset &&
          reference.second_best.hypothesis.frequency_offset_hz ==
              neon.second_best.hypothesis.frequency_offset_hz));
    case_result.correlation_real_difference = std::abs(
        reference.best.correlation.real() - neon.best.correlation.real());
    case_result.correlation_imag_difference = std::abs(
        reference.best.correlation.imag() - neon.best.correlation.imag());
    const auto best_frequency = std::find_if(
        plan.frequency_hypotheses.begin(),
        plan.frequency_hypotheses.end(),
        [&reference](
            const satcomfec::acquisition::PreparedFrequencyHypothesis& frequency) {
            return frequency.frequency_offset_hz ==
                   reference.best.hypothesis.frequency_offset_hz;
        });
    if (best_frequency == plan.frequency_hypotheses.end()) {
        return case_result;
    }
    case_result.correlation_component_tolerance = correlation_component_tolerance(
        received,
        reference.best.hypothesis.timing_offset,
        *best_frequency);

    const double correlation_vector_tolerance =
        std::sqrt(2.0) * case_result.correlation_component_tolerance;
    case_result.score_tolerance =
        2.0 * std::abs(reference.best.correlation) * correlation_vector_tolerance +
        correlation_vector_tolerance * correlation_vector_tolerance;
    case_result.score_difference = std::abs(reference.best.score - neon.best.score);
    case_result.within_tolerance =
        case_result.candidate_identity_match &&
        case_result.second_best_candidate_identity_match &&
        case_result.correlation_real_difference <=
            case_result.correlation_component_tolerance &&
        case_result.correlation_imag_difference <=
            case_result.correlation_component_tolerance &&
        case_result.score_difference <= case_result.score_tolerance;
    return case_result;
}

}  // namespace

int main(int argc, char** argv) {
    bool require_neon = false;
    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string argument = argv[argument_index];
        if (argument == "--require-neon") {
            require_neon = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "Usage: check_acquisition_kernels [--require-neon]\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "Unknown argument: " << argument << "\n";
        return EXIT_FAILURE;
    }

    const bool neon_compiled =
        satcomfec::acquisition::acquisition_neon_kernel_compiled();
    if (!neon_compiled) {
        std::cout << "{\n"
                  << "  \"ok\": " << (require_neon ? "false" : "true") << ",\n"
                  << "  \"neon_kernel_compiled\": false,\n"
                  << "  \"neon_executed\": false,\n"
                  << "  \"implementation\": \"unavailable\",\n"
                  << "  \"cases\": [],\n"
                  << "  \"error\": \"NEON acquisition kernel is not compiled for this target\"\n"
                  << "}\n";
        return require_neon ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    const std::vector<KernelCase> definitions = {
        {"single_sample", 1, 1, 1, 1.0F, 0.01F},
        {"short_odd", 3, 3, 1, 0.8F, 0.02F},
        {"one_timing_vector", 4, 4, 1, 1.2F, 0.01F},
        {"timing_vector_tail", 5, 5, 1, 0.9F, 0.03F},
        {"two_timing_vectors", 17, 8, 1, 1.0F, 0.02F},
        {"four_timing_vectors", 33, 16, 1, 0.8F, 0.03F},
        {"four_vector_tail", 65, 19, 1, 0.7F, 0.04F},
        {"eight_timing_vectors", 65, 32, 1, 0.8F, 0.03F},
        {"eight_vector_tail", 129, 35, 1, 0.7F, 0.04F},
        {"irregular_timing_grid", 129, 17, 2, 0.7F, 0.05F},
        {"long_even", 256, 16, 1, 0.7F, 0.05F},
        {"long_odd_tail", 257, 17, 1, 0.7F, 0.05F},
        {"very_long_odd", 1023, 19, 1, 0.5F, 0.08F},
        {"very_weak", 33, 16, 1, 1.0e-7F, 1.0e-8F},
        {"high_amplitude", 129, 17, 1, 1.0e4F, 10.0F},
    };

    std::vector<CaseResult> results;
    results.reserve(definitions.size());
    bool all_ok = true;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        results.push_back(run_case(definitions[index], 0xC001D00DU + index));
        all_ok = all_ok && results.back().within_tolerance;
    }

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (all_ok ? "true" : "false") << ",\n";
    std::cout << "  \"neon_kernel_compiled\": true,\n";
    std::cout << "  \"neon_executed\": true,\n";
    std::cout << "  \"implementation\": \"neon\",\n";
    std::cout << "  \"tolerance_model\": \"component tolerance = 8 * "
                 "float_epsilon * preamble_length * sum(abs(x)*abs(w)); "
                 "score tolerance is propagated from the complex-correlation bound\",\n";
    std::cout << "  \"cases\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const CaseResult& result = results[index];
        std::cout << "    {\n";
        std::cout << "      \"name\": \""
                  << satcomfec::tools::escape_json(result.name) << "\",\n";
        std::cout << "      \"preamble_length\": " << result.preamble_length << ",\n";
        std::cout << "      \"candidate_identity_match\": "
                  << (result.candidate_identity_match ? "true" : "false") << ",\n";
        std::cout << "      \"second_best_candidate_identity_match\": "
                  << (result.second_best_candidate_identity_match ? "true" : "false")
                  << ",\n";
        std::cout << "      \"correlation_component_tolerance\": "
                  << satcomfec::tools::format_float(
                         result.correlation_component_tolerance, 12)
                  << ",\n";
        std::cout << "      \"correlation_real_difference\": "
                  << satcomfec::tools::format_float(
                         result.correlation_real_difference, 12)
                  << ",\n";
        std::cout << "      \"correlation_imag_difference\": "
                  << satcomfec::tools::format_float(
                         result.correlation_imag_difference, 12)
                  << ",\n";
        std::cout << "      \"score_tolerance\": "
                  << satcomfec::tools::format_float(result.score_tolerance, 9)
                  << ",\n";
        std::cout << "      \"score_difference\": "
                  << satcomfec::tools::format_float(result.score_difference, 9)
                  << ",\n";
        std::cout << "      \"within_tolerance\": "
                  << (result.within_tolerance ? "true" : "false") << "\n";
        std::cout << "    }" << (index + 1 < results.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"error\": \"\"\n";
    std::cout << "}\n";
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
