#include "util/iq_reader.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("satcomfec-iq-test-" + std::to_string(nonce) + "-" +
                 std::to_string(attempt));
            if (std::filesystem::create_directory(candidate)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("could not create a temporary test directory");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    require(!file.fail(), "could not write test IQ file");
}

std::string encode_components(const std::vector<std::uint32_t>& bits) {
    static_assert(sizeof(float) == sizeof(std::uint32_t), "IQ requires float32");
    return std::string(reinterpret_cast<const char*>(bits.data()),
                       bits.size() * sizeof(std::uint32_t));
}

void expect_rejection(const std::string& path, const std::string& description) {
    std::vector<satcomfec::ComplexF> samples{{7.0F, -3.0F}};
    require(!satcomfec::load_iq_from_file(path, samples), description + " was accepted");
    require(samples.empty(), description + " retained output samples");
}

void expect_exact_components(const std::filesystem::path& path,
                             const std::vector<std::uint32_t>& expected) {
    std::vector<satcomfec::ComplexF> samples{{7.0F, -3.0F}};
    require(satcomfec::load_iq_from_file(path.string(), samples),
            "valid IQ file was rejected");
    require(samples.size() * 2 == expected.size(), "valid sample count changed");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const float value = index % 2 == 0 ? samples[index / 2].real()
                                          : samples[index / 2].imag();
        std::uint32_t actual = 0;
        std::memcpy(&actual, &value, sizeof(actual));
        require(actual == expected[index], "a finite sample's float bits changed");
    }
}

}  // namespace

int main() {
    try {
        TemporaryDirectory directory;
        const auto path = directory.path / "capture.iq";
        const std::vector<std::uint32_t> finite_values{
            0x00000000U, 0x80000000U, 0x00000001U, 0x80000001U,
            0x7f7fffffU, 0xff7fffffU, 0x3f800000U, 0xbf800000U};

        // Exercise both sides of the reader's 1,024-sample chunk boundary.
        for (const std::size_t sample_count : {1U, 1023U, 1024U, 1025U, 2050U}) {
            std::vector<std::uint32_t> components;
            for (std::size_t index = 0; index < 2 * sample_count; ++index) {
                components.push_back(finite_values[index % finite_values.size()]);
            }
            const auto bytes = encode_components(components);
            write_bytes(path, bytes);
            expect_exact_components(path, components);
            for (std::size_t trailing = 1; trailing < 8; ++trailing) {
                write_bytes(path, bytes + std::string(trailing, '\0'));
                expect_rejection(path.string(), "partial sample");
            }
        }

        // Reject each non-finite component even after a whole valid chunk.
        for (const auto non_finite : {0x7f800000U, 0xff800000U, 0x7fc00001U}) {
            for (const std::size_t component : {0U, 1U, 2048U, 2049U}) {
                std::vector<std::uint32_t> bits(2050, 0x3f800000U);
                bits[component] = non_finite;
                write_bytes(path, encode_components(bits));
                expect_rejection(path.string(), "non-finite component");
            }
        }

        write_bytes(path, "");
        expect_rejection(path.string(), "empty file");
        expect_rejection((directory.path / "missing.iq").string(), "missing file");
        expect_rejection(directory.path.string(), "directory");
        write_bytes(path, encode_components(finite_values));
        expect_rejection(path.string() + '\0' + "missing", "null-byte path");
        std::cout << "IQ reader integrity passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "IQ reader integrity failed: " << error.what() << '\n';
        return 1;
    }
}
