cmake_minimum_required(VERSION 3.22.1)

project("satcom_fec_trust_lab")

add_library(
    satcomfec
    SHARED
    dsp/front_end_dsp.cpp
    dsp/framing.cpp
    dsp/soft_demod.cpp
    fec/ldpc_decoder_sme2.cpp
    fec/ldpc_decoder_neon.cpp
    fec/viterbi_decoder_sme2.cpp
    fec/viterbi_decoder_neon.cpp
    trust/trust_features.cpp
    trust/trust_score.cpp
    util/iq_reader.cpp
    util/logging.cpp
)

find_library(
    log-lib
    log
)

target_include_directories(
    satcomfec
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
    satcomfec
    ${log-lib}
)
