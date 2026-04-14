cmake_minimum_required(VERSION 3.22)

project(satcom_fec_trust_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_library(
    satcom_replay_core
    STATIC
    src/demo/replay_pipeline.cpp
    src/dsp/front_end_dsp.cpp
    src/dsp/framing.cpp
    src/dsp/soft_demod.cpp
    src/fec/convolutional_codec.cpp
    src/fec/ldpc_bitflip.cpp
    src/fec/ldpc_decoder_sme2.cpp
    src/fec/ldpc_decoder_neon.cpp
    src/fec/viterbi_decoder_sme2.cpp
    src/fec/viterbi_decoder_neon.cpp
    src/trust/trust_features.cpp
    src/trust/trust_score.cpp
    src/util/iq_reader.cpp
    src/util/logging.cpp
)

target_include_directories(
    satcom_replay_core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(replay_demo tools/replay_demo.cpp)
target_link_libraries(replay_demo PRIVATE satcom_replay_core)

add_executable(benchmark_decoders tools/benchmark_decoders.cpp)
target_link_libraries(benchmark_decoders PRIVATE satcom_replay_core)
