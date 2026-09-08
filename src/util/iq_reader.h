#ifndef SATCOMFEC_IQ_READER_H
#define SATCOMFEC_IQ_READER_H

#include <complex>
#include <string>
#include <vector>

namespace satcomfec {

using ComplexF = std::complex<float>;

/**
 * Load IQ samples from a binary file.
 *
 * Expected format: interleaved float32 I, float32 Q.
 * Empty files, incomplete samples, non-finite components and read errors
 * return false and leave out_samples empty. Finite sample values are preserved.
 */
bool load_iq_from_file(const std::string& path,
                       std::vector<ComplexF>& out_samples);

}  // namespace satcomfec

#endif  // SATCOMFEC_IQ_READER_H
