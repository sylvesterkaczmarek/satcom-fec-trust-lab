cmake_minimum_required(VERSION 3.22)

project(satcom_fec_trust_lab LANGUAGES CXX)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(SATCOMFEC_ENABLE_NEON "Build with an explicit AArch64 NEON target flag" OFF)
option(SATCOMFEC_ENABLE_SME2 "Build with an explicit AArch64 SME2 target flag" OFF)

function(satcomfec_find_supported_flag out_var)
    foreach(candidate_flag IN LISTS ARGN)
        string(MAKE_C_IDENTIFIER "${candidate_flag}" flag_id)
        check_cxx_compiler_flag("${candidate_flag}" "SATCOMFEC_HAS_${flag_id}")
        if(SATCOMFEC_HAS_${flag_id})
            set("${out_var}" "${candidate_flag}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set("${out_var}" "" PARENT_SCOPE)
endfunction()

function(satcomfec_check_neon_flag flag out_var)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${flag}")
    check_cxx_source_compiles("
        #if !defined(__ARM_NEON) && !defined(__ARM_NEON__)
        #error NEON macro not defined
        #endif
        #include <arm_neon.h>
        int main() {
            int8x16_t value = vdupq_n_s8(1);
            return static_cast<int>(vgetq_lane_s8(value, 0)) == 1 ? 0 : 1;
        }
    " "${out_var}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")
    set("${out_var}" "${${out_var}}" PARENT_SCOPE)
endfunction()

function(satcomfec_check_sme2_flag flag out_var)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${flag}")
    check_cxx_source_compiles("
        #if !defined(__ARM_FEATURE_SME2)
        #error SME2 macro not defined
        #endif
        #include <arm_sve.h>
        #if defined(__has_include)
        #if __has_include(<arm_sme.h>)
        #include <arm_sme.h>
        #endif
        #else
        #include <arm_sme.h>
        #endif
        __arm_locally_streaming void satcomfec_sme2_probe() {}
        int main() { return 0; }
    " "${out_var}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")
    set("${out_var}" "${${out_var}}" PARENT_SCOPE)
endfunction()

set(SATCOMFEC_ARCH_FLAGS "")
set(SATCOMFEC_SME2_CANDIDATE_FLAGS "-march=armv9.4-a+sme2" "-march=armv9.2-a+sme2")

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    list(
        PREPEND
        SATCOMFEC_SME2_CANDIDATE_FLAGS
        "-mcpu=native+sme2"
        "-march=native+sme2"
    )
endif()

if(SATCOMFEC_ENABLE_SME2)
    satcomfec_find_supported_flag(
        SATCOMFEC_SME2_FLAG
        ${SATCOMFEC_SME2_CANDIDATE_FLAGS}
    )
    if(NOT SATCOMFEC_SME2_FLAG)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_SME2=ON requires a compiler that accepts "
            "an SME2 target flag. On Darwin arm64, native+sme2 is tried first; "
            "generic fallbacks include -march=armv9.4-a+sme2 and -march=armv9.2-a+sme2."
        )
    endif()
    satcomfec_check_sme2_flag("${SATCOMFEC_SME2_FLAG}" SATCOMFEC_SME2_ACLE_OK)
    if(NOT SATCOMFEC_SME2_ACLE_OK)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_SME2=ON requires ACLE SME2 support, including "
            "__ARM_FEATURE_SME2 and __arm_locally_streaming."
        )
    endif()
    list(APPEND SATCOMFEC_ARCH_FLAGS "${SATCOMFEC_SME2_FLAG}")
    message(STATUS "satcom-fec-trust-lab: SME2 build enabled with ${SATCOMFEC_SME2_FLAG}")
elseif(SATCOMFEC_ENABLE_NEON)
    satcomfec_find_supported_flag(
        SATCOMFEC_NEON_FLAG
        "-march=armv8-a+simd"
        "-march=armv8-a"
    )
    if(NOT SATCOMFEC_NEON_FLAG)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_NEON=ON requires a compiler that accepts "
            "-march=armv8-a+simd or -march=armv8-a."
        )
    endif()
    satcomfec_check_neon_flag("${SATCOMFEC_NEON_FLAG}" SATCOMFEC_NEON_ACLE_OK)
    if(NOT SATCOMFEC_NEON_ACLE_OK)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_NEON=ON requires NEON ACLE support and arm_neon.h."
        )
    endif()
    list(APPEND SATCOMFEC_ARCH_FLAGS "${SATCOMFEC_NEON_FLAG}")
    message(STATUS "satcom-fec-trust-lab: NEON build enabled with ${SATCOMFEC_NEON_FLAG}")
else()
    message(STATUS "satcom-fec-trust-lab: default portable build; no explicit Arm architecture flag requested")
endif()

add_library(
    satcom_replay_core
    STATIC
    src/demo/replay_pipeline.cpp
    src/dsp/front_end_dsp.cpp
    src/dsp/framing.cpp
    src/dsp/soft_demod.cpp
    src/fec/branch_metrics_sme2.cpp
    src/fec/convolutional_codec.cpp
    src/fec/ldpc_bitflip.cpp
    src/fec/viterbi_decoder_neon.cpp
    src/fec/viterbi_decoder_reference.cpp
    src/fec/viterbi_decoder_sme2.cpp
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

add_executable(check_branch_metrics tools/check_branch_metrics.cpp)
target_link_libraries(check_branch_metrics PRIVATE satcom_replay_core)

foreach(target_name
        satcom_replay_core
        replay_demo
        benchmark_decoders
        check_branch_metrics)
    target_compile_options("${target_name}" PRIVATE ${SATCOMFEC_ARCH_FLAGS})
endforeach()
