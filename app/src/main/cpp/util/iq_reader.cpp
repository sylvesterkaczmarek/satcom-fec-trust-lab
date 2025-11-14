#include "iq_reader.h"

#include <cstdio>

#include "logging.h"

namespace satcomfec {

bool load_iq_from_file(const std::string& path,
                       std::vector<ComplexF>& out_samples) {
    out_samples.clear();

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        log_error("Failed to open IQ file");
        return false;
    }

    // Simple, non optimised stub: read in small chunks.
    constexpr size_t kChunkSize = 1024;
    float buffer[2 * kChunkSize];

    while (true) {
        size_t read_count = std::fread(buffer, sizeof(float), 2 * kChunkSize, f);
        if (read_count == 0) {
            break;
        }
        size_t complex_count = read_count / 2;
        for (size_t i = 0; i < complex_count; ++i) {
            float i_val = buffer[2 * i];
            float q_val = buffer[2 * i + 1];
            out_samples.emplace_back(i_val, q_val);
        }
        if (read_count < 2 * kChunkSize) {
            break;
        }
    }

    std::fclose(f);
    log_info("Loaded IQ file (stub implementation)");
    return true;
}

}  // namespace satcomfec
