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

    constexpr size_t kChunkSize = 1024;
    float buffer[2 * kChunkSize];

    while (true) {
        size_t read_count = std::fread(buffer, sizeof(float), 2 * kChunkSize, f);
        if (read_count == 0) {
            break;
        }
        if ((read_count % 2) != 0) {
            std::fclose(f);
            out_samples.clear();
            log_error("load_iq_from_file: file ended with a partial IQ sample");
            return false;
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
    if (out_samples.empty()) {
        log_error("load_iq_from_file: file contained no IQ samples");
        return false;
    }
    log_info("load_iq_from_file: IQ samples loaded");
    return true;
}

}  // namespace satcomfec
