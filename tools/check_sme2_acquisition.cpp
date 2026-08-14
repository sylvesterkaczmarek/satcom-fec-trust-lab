#include "acquisition/acquisition_runner.h"
#include "json_output.h"

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
// Four rounded operations per complex component per sample, plus margin for
// float32 weight conversion and different legal FMA contraction orders.
constexpr double kToleranceSafetyFactor = 8.0;

struct KernelCase {
    std::string name;
    std::size_t preamble_length;
    std::size_t timing_count;
    std::size_t frequency_count;
    std::size_t timing_step;
};

struct CaseResult {
    std::string name;
    std::size_t preamble_length = 0;
    std::size_t timing_count = 0;
    std::size_t frequency_count = 0;
    std::size_t candidate_count = 0;
    std::string input_layout;
    double maximum_component_difference = 0.0;
    double maximum_component_tolerance = 0.0;
    double maximum_score_difference = 0.0;
    double maximum_score_tolerance = 0.0;
    bool all_correlations_within_tolerance = false;
    bool best_candidate_match = false;
    bool second_best_candidate_match = false;
    bool neon_compared = false;
    bool three_path_candidate_identity_match = false;
    bool passed = false;
};

class DeterministicGenerator {
public:
    explicit DeterministicGenerator(std::uint32_t seed) : state_(seed) {}

    float signed_unit() {
        state_ = state_ * 1664525U + 1013904223U;
        const double unit = static_cast<double>(state_) /
                            static_cast<double>(
                                std::numeric_limits<std::uint32_t>::max());
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

std::vector<double> make_frequency_hypotheses(std::size_t count) {
    std::vector<double> frequencies;
    frequencies.reserve(count);
    const std::int64_t center = static_cast<std::int64_t>(count / 2);
    for (std::size_t index = 0; index < count; ++index) {
        frequencies.push_back(
            375.0 * (static_cast<std::int64_t>(index) - center));
    }
    return frequencies;
}

std::vector<std::size_t> make_timing_hypotheses(
    std::size_t count,
    std::size_t step) {
    std::vector<std::size_t> timings;
    timings.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        timings.push_back(3 + step * index);
    }
    return timings;
}

std::vector<satcomfec::ComplexF> make_received(
    const std::vector<satcomfec::ComplexF>& preamble,
    const std::vector<std::size_t>& timings,
    double frequency_offset_hz,
    DeterministicGenerator& generator) {
    const std::size_t injected_timing = timings[timings.size() / 2];
    std::vector<satcomfec::ComplexF> received(
        timings.back() + preamble.size() + 5);
    for (satcomfec::ComplexF& sample : received) {
        sample = {
            0.015F * generator.signed_unit(),
            0.015F * generator.signed_unit(),
        };
    }

    for (std::size_t index = 0; index < preamble.size(); ++index) {
        const double phase = 0.41 + 2.0 * kPi * frequency_offset_hz *
                                        static_cast<double>(index) / kSampleRateHz;
        const std::complex<double> carrier = std::polar(1.0, phase);
        const std::complex<double> signal =
            std::complex<double>(preamble[index].real(), preamble[index].imag()) *
            carrier;
        received[injected_timing + index] += satcomfec::ComplexF(
            static_cast<float>(signal.real()),
            static_cast<float>(signal.imag()));
    }
    return received;
}

bool same_candidate(
    const satcomfec::acquisition::AcquisitionCandidate& left,
    const satcomfec::acquisition::AcquisitionCandidate& right) {
    return left.valid == right.valid &&
           (!left.valid ||
            (left.hypothesis.timing_offset == right.hypothesis.timing_offset &&
             left.hypothesis.frequency_offset_hz ==
                 right.hypothesis.frequency_offset_hz));
}

double component_tolerance(
    const std::vector<satcomfec::ComplexF>& received,
    std::size_t timing_offset,
    const satcomfec::acquisition::PreparedFrequencyHypothesis& frequency) {
    double product_magnitude_sum = 0.0;
    for (std::size_t index = 0;
         index < frequency.matched_filter_weights_f32.size();
         ++index) {
        product_magnitude_sum +=
            std::abs(received[timing_offset + index]) *
            std::abs(frequency.matched_filter_weights_f32[index]);
    }
    return kToleranceSafetyFactor * std::numeric_limits<float>::epsilon() *
           static_cast<double>(frequency.matched_filter_weights_f32.size()) *
           product_magnitude_sum;
}

std::complex<double> reference_correlation(
    const std::vector<satcomfec::ComplexF>& received,
    std::size_t timing_offset,
    const satcomfec::acquisition::PreparedFrequencyHypothesis& frequency) {
    std::complex<double> correlation(0.0, 0.0);
    for (std::size_t index = 0;
         index < frequency.matched_filter_weights.size();
         ++index) {
        correlation += std::complex<double>(
                           received[timing_offset + index].real(),
                           received[timing_offset + index].imag()) *
                       frequency.matched_filter_weights[index];
    }
    return correlation;
}

CaseResult run_case(const KernelCase& definition, std::uint32_t seed) {
    DeterministicGenerator generator(seed);
    const std::vector<satcomfec::ComplexF> preamble =
        make_preamble(definition.preamble_length, generator);
    const std::vector<std::size_t> timings =
        make_timing_hypotheses(definition.timing_count, definition.timing_step);
    const std::vector<double> frequencies =
        make_frequency_hypotheses(definition.frequency_count);
    const double injected_frequency = frequencies.back();
    const std::vector<satcomfec::ComplexF> received = make_received(
        preamble, timings, injected_frequency, generator);

    satcomfec::acquisition::AcquisitionConfig config;
    config.sample_rate_hz = kSampleRateHz;
    config.timing_offsets = timings;
    config.frequency_offsets_hz = frequencies;

    CaseResult case_result;
    case_result.name = definition.name;
    case_result.preamble_length = definition.preamble_length;
    case_result.timing_count = definition.timing_count;
    case_result.frequency_count = definition.frequency_count;
    case_result.candidate_count =
        definition.timing_count * definition.frequency_count;

    satcomfec::acquisition::AcquisitionPlan plan;
    std::string error_message;
    if (!satcomfec::acquisition::prepare_acquisition_plan(
            config, preamble, plan, error_message)) {
        return case_result;
    }
    satcomfec::acquisition::Sme2AcquisitionWorkspace workspace;
    if (!satcomfec::acquisition::prepare_sme2_acquisition_workspace(
            received, plan, workspace, error_message)) {
        return case_result;
    }
    case_result.input_layout = workspace.packed_input_required
                                   ? "sample-major-packed-noncontiguous-timing"
                                   : "direct-interleaved-contiguous-timing";

    const satcomfec::acquisition::AcquisitionResult reference =
        satcomfec::acquisition::run_reference_acquisition(received, plan);
    const satcomfec::acquisition::AcquisitionResult sme2 =
        satcomfec::acquisition::run_sme2_acquisition_prepared(
            received, plan, workspace);
    if (!reference.ok || !sme2.ok || reference.implementation != "reference" ||
        sme2.implementation != "sme2") {
        return case_result;
    }

    bool correlations_ok = true;
    for (std::size_t frequency_index = 0;
         frequency_index < definition.frequency_count;
         ++frequency_index) {
        const auto& frequency = plan.frequency_hypotheses[frequency_index];
        for (std::size_t timing_index = 0;
             timing_index < definition.timing_count;
             ++timing_index) {
            const std::size_t candidate_index =
                frequency_index * definition.timing_count + timing_index;
            const std::complex<double> expected = reference_correlation(
                received, timings[timing_index], frequency);
            const std::complex<double> actual(
                workspace.correlation_real_by_frequency_and_timing[candidate_index],
                workspace.correlation_imag_by_frequency_and_timing[candidate_index]);
            const double tolerance = component_tolerance(
                received, timings[timing_index], frequency);
            const double real_difference =
                std::abs(expected.real() - actual.real());
            const double imag_difference =
                std::abs(expected.imag() - actual.imag());
            const double correlation_difference = std::max(
                real_difference, imag_difference);
            const double vector_tolerance = std::sqrt(2.0) * tolerance;
            const double score_tolerance =
                2.0 * std::abs(expected) * vector_tolerance +
                vector_tolerance * vector_tolerance;
            const double score_difference =
                std::abs(std::norm(expected) - std::norm(actual));

            case_result.maximum_component_difference = std::max(
                case_result.maximum_component_difference,
                correlation_difference);
            case_result.maximum_component_tolerance = std::max(
                case_result.maximum_component_tolerance,
                tolerance);
            case_result.maximum_score_difference = std::max(
                case_result.maximum_score_difference,
                score_difference);
            case_result.maximum_score_tolerance = std::max(
                case_result.maximum_score_tolerance,
                score_tolerance);
            correlations_ok = correlations_ok &&
                              real_difference <= tolerance &&
                              imag_difference <= tolerance &&
                              score_difference <= score_tolerance;
        }
    }

    case_result.all_correlations_within_tolerance = correlations_ok;
    case_result.best_candidate_match = same_candidate(reference.best, sme2.best);
    case_result.second_best_candidate_match =
        same_candidate(reference.second_best, sme2.second_best);
    case_result.neon_compared =
        satcomfec::acquisition::acquisition_neon_kernel_compiled();
    case_result.three_path_candidate_identity_match = true;
    if (case_result.neon_compared) {
        const satcomfec::acquisition::AcquisitionResult neon =
            satcomfec::acquisition::run_neon_acquisition(received, plan);
        case_result.three_path_candidate_identity_match =
            neon.ok && neon.implementation == "neon" &&
            same_candidate(reference.best, neon.best) &&
            same_candidate(reference.second_best, neon.second_best) &&
            same_candidate(neon.best, sme2.best) &&
            same_candidate(neon.second_best, sme2.second_best);
    }
    case_result.passed =
        case_result.all_correlations_within_tolerance &&
        case_result.best_candidate_match &&
        case_result.second_best_candidate_match &&
        case_result.three_path_candidate_identity_match;
    return case_result;
}

}  // namespace

int main(int argc, char** argv) {
    bool require_sme2 = false;
    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string argument = argv[argument_index];
        if (argument == "--require-sme2") {
            require_sme2 = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "Usage: check_sme2_acquisition [--require-sme2]\n";
            return EXIT_SUCCESS;
        }
        std::cerr << "Unknown argument: " << argument << "\n";
        return EXIT_FAILURE;
    }

    const bool compiled =
        satcomfec::acquisition::acquisition_sme2_kernel_compiled();
    const bool runtime_supported =
        satcomfec::acquisition::acquisition_sme2_runtime_supported();
    if (!compiled || !runtime_supported) {
        const bool okay = !require_sme2;
        std::cout << "{\n"
                  << "  \"ok\": " << (okay ? "true" : "false") << ",\n"
                  << "  \"sme2_kernel_compiled\": "
                  << (compiled ? "true" : "false") << ",\n"
                  << "  \"sme2_runtime_supported\": "
                  << (runtime_supported ? "true" : "false") << ",\n"
                  << "  \"sme2_executed\": false,\n"
                  << "  \"implementation\": \"unavailable\",\n"
                  << "  \"mechanism\": \""
                  << satcomfec::acquisition::acquisition_sme2_mechanism()
                  << "\",\n"
                  << "  \"cases\": [],\n"
                  << "  \"error\": \"SME2 acquisition execution is unavailable on this host\"\n"
                  << "}\n";
        return okay ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    const std::size_t lanes =
        satcomfec::acquisition::acquisition_sme2_streaming_lanes_f32();
    const std::vector<KernelCase> definitions = {
        {"single_candidate", 1, 1, 1, 1},
        {"short_odd", 3, 3, 2, 1},
        {"vector_tail", 5, lanes + 1, 3, 1},
        {"four_vector_exact", 17, 4 * lanes, 4, 1},
        {"four_vector_tail", 33, 4 * lanes + 3, 5, 1},
        {"many_frequency_hypotheses", 65, 11, 7, 1},
        {"irregular_timing_grid", 129, 13, 5, 2},
        {"long_odd_preamble", 257, 7, 9, 3},
    };

    std::vector<CaseResult> results;
    results.reserve(definitions.size());
    bool all_ok = true;
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        results.push_back(run_case(definitions[index], 0x5A17C0DEU + index));
        all_ok = all_ok && results.back().passed;
    }

    std::cout << "{\n";
    std::cout << "  \"ok\": " << (all_ok ? "true" : "false") << ",\n";
    std::cout << "  \"sme2_kernel_compiled\": true,\n";
    std::cout << "  \"sme2_runtime_supported\": true,\n";
    std::cout << "  \"sme2_executed\": true,\n";
    std::cout << "  \"implementation\": \"sme2\",\n";
    std::cout << "  \"mechanism\": \""
              << satcomfec::acquisition::acquisition_sme2_mechanism()
              << "\",\n";
    std::cout << "  \"streaming_lanes_f32\": " << lanes << ",\n";
    std::cout << "  \"timing_batch_width\": " << 4 * lanes << ",\n";
    std::cout << "  \"tolerance_model\": \"component tolerance = 8 * "
                 "float_epsilon * preamble_length * sum(abs(x)*abs(w)); "
                 "score tolerance is propagated from the complex-correlation bound\",\n";
    std::cout << "  \"cases\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const CaseResult& result = results[index];
        std::cout << "    {\n";
        std::cout << "      \"name\": \""
                  << satcomfec::tools::escape_json(result.name) << "\",\n";
        std::cout << "      \"preamble_length\": "
                  << result.preamble_length << ",\n";
        std::cout << "      \"timing_count\": " << result.timing_count << ",\n";
        std::cout << "      \"frequency_count\": "
                  << result.frequency_count << ",\n";
        std::cout << "      \"candidate_count\": "
                  << result.candidate_count << ",\n";
        std::cout << "      \"input_layout\": \""
                  << satcomfec::tools::escape_json(result.input_layout)
                  << "\",\n";
        std::cout << "      \"maximum_component_difference\": "
                  << satcomfec::tools::format_float(
                         result.maximum_component_difference, 12)
                  << ",\n";
        std::cout << "      \"maximum_component_tolerance\": "
                  << satcomfec::tools::format_float(
                         result.maximum_component_tolerance, 12)
                  << ",\n";
        std::cout << "      \"maximum_score_difference\": "
                  << satcomfec::tools::format_float(
                         result.maximum_score_difference, 9)
                  << ",\n";
        std::cout << "      \"maximum_score_tolerance\": "
                  << satcomfec::tools::format_float(
                         result.maximum_score_tolerance, 9)
                  << ",\n";
        std::cout << "      \"all_correlations_within_tolerance\": "
                  << (result.all_correlations_within_tolerance ? "true" : "false")
                  << ",\n";
        std::cout << "      \"best_candidate_match\": "
                  << (result.best_candidate_match ? "true" : "false") << ",\n";
        std::cout << "      \"second_best_candidate_match\": "
                  << (result.second_best_candidate_match ? "true" : "false")
                  << ",\n";
        std::cout << "      \"neon_compared\": "
                  << (result.neon_compared ? "true" : "false") << ",\n";
        std::cout << "      \"three_path_candidate_identity_match\": "
                  << (result.three_path_candidate_identity_match ? "true" : "false")
                  << ",\n";
        std::cout << "      \"passed\": "
                  << (result.passed ? "true" : "false") << "\n";
        std::cout << "    }" << (index + 1 < results.size() ? "," : "") << "\n";
    }
    std::cout << "  ],\n";
    std::cout << "  \"error\": \"\"\n";
    std::cout << "}\n";
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
