#include "iq_reader.h"

#include <cmath>
#include <cstdio>
#include <memory>

#include "logging.h"

namespace satcomfec {

bool load_iq_from_file(const std::string& path,
                       std::vector<ComplexF>& out_samples) {
    out_samples.clear();

    if (path.find('\0') != std::string::npos) {
        log_error("load_iq_from_file: file path contains a null byte");
        return false;
    }

    const auto close_file = [](FILE* file) { std::fclose(file); };
    const std::unique_ptr<FILE, decltype(close_file)> f(
        std::fopen(path.c_str(), "rb"), close_file);
    if (!f) {
        log_error("Failed to open IQ file");
        return false;
    }

    constexpr size_t kChunkSize = 1024;
    float buffer[2 * kChunkSize];

    while (true) {
        // Count bytes so that a trailing partial float cannot be discarded by
        // fread's complete-element count.
        const size_t read_bytes = std::fread(buffer, 1, sizeof(buffer), f.get());
        if (std::ferror(f.get())) {
            out_samples.clear();
            log_error("load_iq_from_file: failed while reading IQ samples");
            return false;
        }
        if ((read_bytes % (2 * sizeof(float))) != 0) {
            out_samples.clear();
            log_error("load_iq_from_file: file ended with a partial IQ sample");
            return false;
        }
        const size_t complex_count = read_bytes / (2 * sizeof(float));
        for (size_t i = 0; i < complex_count; ++i) {
            float i_val = buffer[2 * i];
            float q_val = buffer[2 * i + 1];
            if (!std::isfinite(i_val) || !std::isfinite(q_val)) {
                out_samples.clear();
                log_error("load_iq_from_file: IQ samples must be finite");
                return false;
            }
            out_samples.emplace_back(i_val, q_val);
        }
        if (read_bytes < sizeof(buffer)) {
            break;
        }
    }

    if (out_samples.empty()) {
        log_error("load_iq_from_file: file contained no IQ samples");
        return false;
    }
    log_info("load_iq_from_file: IQ samples loaded");
    return true;
}

}  // namespace satcomfec
