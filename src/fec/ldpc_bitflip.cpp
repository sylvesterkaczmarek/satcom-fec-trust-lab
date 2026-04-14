#include "ldpc_bitflip.h"

#include <array>
#include <cstddef>

#include "../util/logging.h"

namespace satcomfec {

namespace {

constexpr size_t kLdpcBlockLength = 12;
constexpr size_t kLdpcCheckCount = 6;
constexpr int kLdpcMaxIterations = 8;

constexpr std::array<std::array<uint8_t, kLdpcBlockLength>, kLdpcCheckCount> kParityCheck {{
    {{1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0}},
    {{0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0}},
    {{0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0}},
    {{0, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0}},
    {{0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1}},
}};

bool parity_satisfied(const std::array<uint8_t, kLdpcBlockLength>& bits,
                      size_t check_index) {
    uint8_t parity = 0U;
    for (size_t bit_index = 0; bit_index < kLdpcBlockLength; ++bit_index) {
        if (kParityCheck[check_index][bit_index] != 0U) {
            parity ^= bits[bit_index];
        }
    }
    return parity == 0U;
}

}  // namespace

bool ldpc_decode_reference(const SoftBitBuffer& soft_in,
                           std::vector<uint8_t>& hard_out) {
    if (soft_in.empty() || (soft_in.size() % kLdpcBlockLength) != 0) {
        log_error("ldpc_decode_reference: expected blocks of 12 soft bits");
        return false;
    }

    hard_out.clear();
    hard_out.reserve(soft_in.size());

    for (size_t block_start = 0; block_start < soft_in.size();
         block_start += kLdpcBlockLength) {
        std::array<uint8_t, kLdpcBlockLength> bits {};
        for (size_t i = 0; i < kLdpcBlockLength; ++i) {
            bits[i] = soft_in[block_start + i] >= 0 ? 1U : 0U;
        }

        for (int iteration = 0; iteration < kLdpcMaxIterations; ++iteration) {
            std::array<uint8_t, kLdpcCheckCount> check_failures {};
            int failure_count = 0;
            for (size_t check_index = 0; check_index < kLdpcCheckCount; ++check_index) {
                check_failures[check_index] =
                    parity_satisfied(bits, check_index) ? 0U : 1U;
                failure_count += check_failures[check_index];
            }

            if (failure_count == 0) {
                break;
            }

            std::array<uint8_t, kLdpcBlockLength> votes {};
            for (size_t bit_index = 0; bit_index < kLdpcBlockLength; ++bit_index) {
                for (size_t check_index = 0; check_index < kLdpcCheckCount; ++check_index) {
                    if (check_failures[check_index] != 0U &&
                        kParityCheck[check_index][bit_index] != 0U) {
                        votes[bit_index] += 1U;
                    }
                }
            }

            uint8_t max_vote = 0U;
            for (uint8_t vote : votes) {
                if (vote > max_vote) {
                    max_vote = vote;
                }
            }

            if (max_vote == 0U) {
                break;
            }

            for (size_t bit_index = 0; bit_index < kLdpcBlockLength; ++bit_index) {
                if (votes[bit_index] == max_vote) {
                    bits[bit_index] ^= 0x1U;
                }
            }
        }

        for (uint8_t bit : bits) {
            hard_out.push_back(bit);
        }
    }

    log_info("ldpc_decode_reference: iterative bit-flip decode complete");
    return true;
}

}  // namespace satcomfec
