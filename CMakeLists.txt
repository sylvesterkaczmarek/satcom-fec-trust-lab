cmake_minimum_required(VERSION 3.22)

project(satcom_fec_trust_lab LANGUAGES CXX)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CTest)

find_package(Git QUIET)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

option(SATCOMFEC_ENABLE_NEON "Build with an explicit AArch64 NEON target flag" OFF)
option(SATCOMFEC_ENABLE_SME2 "Build with an explicit AArch64 SME2 target flag" OFF)
option(SATCOMFEC_ENABLE_WARNINGS "Enable project compiler warnings" ON)
option(SATCOMFEC_WARNINGS_AS_ERRORS "Treat project compiler warnings as errors" OFF)
option(
    SATCOMFEC_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF
)
option(
    SATCOMFEC_ANDROID_BENCHMARK_ONLY
    "Build only the acquisition benchmark for Android/ADB execution"
    OFF
)

if(SATCOMFEC_ANDROID_BENCHMARK_ONLY)
    if(NOT ANDROID)
        message(FATAL_ERROR
            "SATCOMFEC_ANDROID_BENCHMARK_ONLY=ON requires the Android NDK "
            "CMake toolchain file. Use scripts/build_android_benchmark.sh."
        )
    endif()
    if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR
            "The Android acquisition benchmark supports only arm64-v8a; got "
            "'${CMAKE_ANDROID_ARCH_ABI}'."
        )
    endif()
    message(STATUS
        "satcom-fec-trust-lab: Android ADB benchmark-only build for "
        "${CMAKE_ANDROID_ARCH_ABI} (${ANDROID_PLATFORM})"
    )
endif()

add_library(satcomfec_project_options INTERFACE)
set(SATCOMFEC_PROJECT_WARNING_FLAGS "")
set(SATCOMFEC_PROJECT_SANITIZER_FLAGS "")

if(SATCOMFEC_ENABLE_WARNINGS)
    if(MSVC)
        set(SATCOMFEC_PROJECT_WARNING_FLAGS "/W4")
        if(SATCOMFEC_WARNINGS_AS_ERRORS)
            list(APPEND SATCOMFEC_PROJECT_WARNING_FLAGS "/WX")
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(SATCOMFEC_PROJECT_WARNING_FLAGS "-Wall" "-Wextra" "-Wpedantic")
        if(SATCOMFEC_WARNINGS_AS_ERRORS)
            list(APPEND SATCOMFEC_PROJECT_WARNING_FLAGS "-Werror")
        endif()
    else()
        message(WARNING
            "Compiler warning flags are not defined for ${CMAKE_CXX_COMPILER_ID}."
        )
    endif()
    target_compile_options(
        satcomfec_project_options
        INTERFACE
        ${SATCOMFEC_PROJECT_WARNING_FLAGS}
    )
endif()

if(SATCOMFEC_ENABLE_SANITIZERS)
    if(ANDROID)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_SANITIZERS=ON is supported only by the portable host build."
        )
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_SANITIZERS=ON requires GCC or Clang."
        )
    endif()
    set(
        SATCOMFEC_PROJECT_SANITIZER_FLAGS
        "-fsanitize=address,undefined"
        "-fno-omit-frame-pointer"
    )
    target_compile_options(
        satcomfec_project_options
        INTERFACE
        ${SATCOMFEC_PROJECT_SANITIZER_FLAGS}
    )
    target_link_options(
        satcomfec_project_options
        INTERFACE
        "-fsanitize=address,undefined"
    )
endif()

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

function(satcomfec_check_neon_only_flag flag out_var)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${flag}")
    check_cxx_source_compiles("
        #if !defined(__ARM_NEON) && !defined(__ARM_NEON__)
        #error NEON macro not defined
        #endif
        #if defined(__ARM_FEATURE_SVE) || defined(__ARM_FEATURE_SME)
        #error scalable or matrix extensions enabled in NEON-only source
        #endif
        #include <arm_neon.h>
        int main() {
            float32x4_t value = vdupq_n_f32(1.0F);
            return static_cast<int>(vgetq_lane_f32(value, 0)) == 1 ? 0 : 1;
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
        __arm_locally_streaming __arm_new(\"za\") void satcomfec_sme2_probe() {
            const svfloat32_t zero = svdup_f32(0.0F);
            const svfloat32x4_t group = svcreate4_f32(zero, zero, zero, zero);
            svwrite_za32_f32_vg1x4(0, group);
            svmla_single_za32_f32_vg1x4(0, group, zero);
            svmls_single_za32_f32_vg1x4(0, group, zero);
            (void)svread_za32_f32_vg1x4(0);
        }
        int main() { return 0; }
    " "${out_var}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")
    set("${out_var}" "${${out_var}}" PARENT_SCOPE)
endfunction()

set(SATCOMFEC_NEON_SOURCE_FLAG "")
set(SATCOMFEC_SME2_SOURCE_FLAG "")
set(SATCOMFEC_ACQUISITION_NEON_AVAILABLE OFF)
satcomfec_find_supported_flag(
    SATCOMFEC_NO_LOOP_VECTORIZE_FLAG
    "-fno-vectorize"
    "-fno-tree-vectorize"
)
satcomfec_find_supported_flag(
    SATCOMFEC_NO_SLP_VECTORIZE_FLAG
    "-fno-slp-vectorize"
    "-fno-tree-slp-vectorize"
)
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
            "__ARM_FEATURE_SME2, ZA state attributes, and VGx4 ZA FMLA/FMLS intrinsics."
        )
    endif()
    set(SATCOMFEC_SME2_SOURCE_FLAG "${SATCOMFEC_SME2_FLAG}")
    message(STATUS "satcom-fec-trust-lab: SME2 build enabled with ${SATCOMFEC_SME2_FLAG}")

    set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE "")
    if(ANDROID)
        # Keep all code outside the gated SME2 translation unit runnable on
        # baseline arm64-v8a devices.
        set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE "-march=armv8-a+simd")
    elseif(SATCOMFEC_SME2_FLAG STREQUAL "-mcpu=native+sme2")
        set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE
            "-mcpu=native+nosve+nosve2+nosme+nosme2")
    elseif(SATCOMFEC_SME2_FLAG STREQUAL "-march=native+sme2")
        set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE
            "-march=native+nosve+nosve2+nosme+nosme2")
    elseif(SATCOMFEC_SME2_FLAG STREQUAL "-march=armv9.4-a+sme2")
        set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE
            "-march=armv9.4-a+nosve+nosve2+nosme+nosme2")
    elseif(SATCOMFEC_SME2_FLAG STREQUAL "-march=armv9.2-a+sme2")
        set(SATCOMFEC_SME2_BASE_NEON_CANDIDATE
            "-march=armv9.2-a+nosve+nosve2+nosme+nosme2")
    endif()
    set(SATCOMFEC_SME2_NEON_CANDIDATE_FLAGS
        "${SATCOMFEC_SME2_BASE_NEON_CANDIDATE}"
        "-march=armv8-a+simd"
        "-march=armv8-a"
    )
    foreach(candidate_flag IN LISTS SATCOMFEC_SME2_NEON_CANDIDATE_FLAGS)
        if(NOT candidate_flag)
            continue()
        endif()
        string(MAKE_C_IDENTIFIER
            "SATCOMFEC_SME2_NEON_ONLY_OK_${candidate_flag}"
            candidate_result)
        satcomfec_check_neon_only_flag(
            "${candidate_flag}"
            "${candidate_result}"
        )
        if(${candidate_result})
            set(SATCOMFEC_NEON_SOURCE_FLAG "${candidate_flag}")
            set(SATCOMFEC_ACQUISITION_NEON_AVAILABLE ON)
            message(STATUS
                "satcom-fec-trust-lab: NEON comparison target disables SVE/SME: "
                "${candidate_flag}"
            )
            break()
        endif()
    endforeach()
    if(NOT SATCOMFEC_ACQUISITION_NEON_AVAILABLE)
        message(FATAL_ERROR
            "SATCOMFEC_ENABLE_SME2=ON also requires a NEON-only comparison "
            "target with SVE and SME disabled."
        )
    endif()
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
    set(SATCOMFEC_NEON_SOURCE_FLAG "${SATCOMFEC_NEON_FLAG}")
    set(SATCOMFEC_ACQUISITION_NEON_AVAILABLE ON)
    message(STATUS "satcom-fec-trust-lab: NEON build enabled with ${SATCOMFEC_NEON_FLAG}")
else()
    message(STATUS "satcom-fec-trust-lab: default portable build; no explicit Arm architecture flag requested")
endif()

if(NOT SATCOMFEC_ACQUISITION_NEON_AVAILABLE)
    satcomfec_check_neon_flag("" SATCOMFEC_NATIVE_NEON_ACLE_OK)
    if(SATCOMFEC_NATIVE_NEON_ACLE_OK)
        set(SATCOMFEC_ACQUISITION_NEON_AVAILABLE ON)
        message(STATUS "satcom-fec-trust-lab: native AArch64 NEON acquisition kernel enabled")
    else()
        message(STATUS "satcom-fec-trust-lab: NEON acquisition kernel unavailable for this target")
    endif()
endif()

set(
    SATCOMFEC_ACQUISITION_SOURCES
    src/acquisition/acquisition_neon.cpp
    src/acquisition/acquisition_plan.cpp
    src/acquisition/acquisition_reference.cpp
    src/acquisition/acquisition_runner.cpp
    src/acquisition/acquisition_sme2.cpp
)

set(SATCOMFEC_CORE_SOURCES ${SATCOMFEC_ACQUISITION_SOURCES})
if(NOT SATCOMFEC_ANDROID_BENCHMARK_ONLY)
    list(APPEND SATCOMFEC_CORE_SOURCES
        src/demo/replay_acquisition.cpp
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
endif()

add_library(
    satcom_replay_core
    STATIC
    ${SATCOMFEC_CORE_SOURCES}
)
target_link_libraries(satcom_replay_core PRIVATE satcomfec_project_options)

set(SATCOMFEC_REFERENCE_COMPILE_OPTIONS "")
if(SATCOMFEC_NO_LOOP_VECTORIZE_FLAG)
    list(APPEND SATCOMFEC_REFERENCE_COMPILE_OPTIONS "${SATCOMFEC_NO_LOOP_VECTORIZE_FLAG}")
endif()
if(SATCOMFEC_NO_SLP_VECTORIZE_FLAG)
    list(APPEND SATCOMFEC_REFERENCE_COMPILE_OPTIONS "${SATCOMFEC_NO_SLP_VECTORIZE_FLAG}")
endif()
if(SATCOMFEC_REFERENCE_COMPILE_OPTIONS)
    set_property(
        SOURCE src/acquisition/acquisition_reference.cpp
        APPEND PROPERTY COMPILE_OPTIONS ${SATCOMFEC_REFERENCE_COMPILE_OPTIONS}
    )
    message(STATUS
        "satcom-fec-trust-lab: scalar acquisition reference vectorization disabled with "
        "${SATCOMFEC_REFERENCE_COMPILE_OPTIONS}"
    )
endif()

if(SATCOMFEC_ACQUISITION_NEON_AVAILABLE)
    set_property(
        SOURCE src/acquisition/acquisition_neon.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS SATCOMFEC_ACQUISITION_NEON_COMPILED=1
    )
    set_property(
        SOURCE src/fec/viterbi_decoder_neon.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS SATCOMFEC_FEC_NEON_COMPILED=1
    )
    if(SATCOMFEC_NEON_SOURCE_FLAG)
        set_property(
            SOURCE
                src/acquisition/acquisition_neon.cpp
                src/fec/viterbi_decoder_neon.cpp
            APPEND PROPERTY COMPILE_OPTIONS "${SATCOMFEC_NEON_SOURCE_FLAG}"
        )
    endif()
endif()

if(SATCOMFEC_SME2_SOURCE_FLAG)
    set_property(
        SOURCE src/acquisition/acquisition_sme2.cpp
        APPEND PROPERTY COMPILE_OPTIONS "${SATCOMFEC_SME2_SOURCE_FLAG}"
    )
    if(NOT SATCOMFEC_ANDROID_BENCHMARK_ONLY)
        set_property(
            SOURCE src/fec/branch_metrics_sme2.cpp
            APPEND PROPERTY COMPILE_OPTIONS "${SATCOMFEC_SME2_SOURCE_FLAG}"
        )
    endif()
    set_property(
        SOURCE src/acquisition/acquisition_sme2.cpp
        APPEND PROPERTY COMPILE_DEFINITIONS SATCOMFEC_ACQUISITION_SME2_COMPILED=1
    )
endif()

if(ANDROID)
    set(SATCOMFEC_BENCHMARK_TARGET "android-adb-native")
else()
    set(SATCOMFEC_BENCHMARK_TARGET "host-native")
endif()

target_include_directories(
    satcom_replay_core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(NOT SATCOMFEC_ANDROID_BENCHMARK_ONLY)
    add_executable(replay_demo tools/replay_demo.cpp)
    target_link_libraries(
        replay_demo
        PRIVATE satcom_replay_core satcomfec_project_options
    )

    add_executable(benchmark_decoders tools/benchmark_decoders.cpp)
    target_link_libraries(
        benchmark_decoders
        PRIVATE satcom_replay_core satcomfec_project_options
    )
endif()

add_executable(
    benchmark_acquisition
    tools/acquisition_benchmark.cpp
    tools/benchmark_acquisition.cpp
)
target_link_libraries(
    benchmark_acquisition
    PRIVATE satcom_replay_core satcomfec_project_options
)

set(SATCOMFEC_BENCHMARK_GIT_SHA "unavailable")
set(SATCOMFEC_BENCHMARK_GIT_DIRTY "unknown")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE SATCOMFEC_GIT_SHA_RESULT
        OUTPUT_VARIABLE SATCOMFEC_BENCHMARK_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT SATCOMFEC_GIT_SHA_RESULT EQUAL 0)
        set(SATCOMFEC_BENCHMARK_GIT_SHA "unavailable")
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE SATCOMFEC_GIT_DIRTY_RESULT
        OUTPUT_VARIABLE SATCOMFEC_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(SATCOMFEC_GIT_DIRTY_RESULT EQUAL 0)
        if(SATCOMFEC_GIT_STATUS)
            set(SATCOMFEC_BENCHMARK_GIT_DIRTY "true")
        else()
            set(SATCOMFEC_BENCHMARK_GIT_DIRTY "false")
        endif()
    endif()
endif()

if(CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" SATCOMFEC_BUILD_TYPE_UPPER)
    set(SATCOMFEC_BENCHMARK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
    set(SATCOMFEC_BENCHMARK_COMMON_FLAGS
        "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${SATCOMFEC_BUILD_TYPE_UPPER}} -std=c++17"
    )
else()
    set(SATCOMFEC_BENCHMARK_BUILD_TYPE "unspecified")
    set(SATCOMFEC_BENCHMARK_COMMON_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
endif()
string(JOIN " " SATCOMFEC_BENCHMARK_WARNING_FLAGS ${SATCOMFEC_PROJECT_WARNING_FLAGS})
string(JOIN " " SATCOMFEC_BENCHMARK_SANITIZER_FLAGS ${SATCOMFEC_PROJECT_SANITIZER_FLAGS})
string(APPEND
    SATCOMFEC_BENCHMARK_COMMON_FLAGS
    " ${SATCOMFEC_BENCHMARK_WARNING_FLAGS} ${SATCOMFEC_BENCHMARK_SANITIZER_FLAGS}"
)
string(STRIP "${SATCOMFEC_BENCHMARK_COMMON_FLAGS}" SATCOMFEC_BENCHMARK_COMMON_FLAGS)
string(JOIN " " SATCOMFEC_BENCHMARK_REFERENCE_FLAGS ${SATCOMFEC_REFERENCE_COMPILE_OPTIONS})
if(NOT SATCOMFEC_BENCHMARK_REFERENCE_FLAGS)
    set(SATCOMFEC_BENCHMARK_REFERENCE_FLAGS "none")
endif()
if(SATCOMFEC_NEON_SOURCE_FLAG)
    set(SATCOMFEC_BENCHMARK_NEON_FLAGS "${SATCOMFEC_NEON_SOURCE_FLAG}")
elseif(SATCOMFEC_ACQUISITION_NEON_AVAILABLE)
    set(SATCOMFEC_BENCHMARK_NEON_FLAGS "compiler target default with NEON ACLE enabled")
else()
    set(SATCOMFEC_BENCHMARK_NEON_FLAGS "not-compiled")
endif()
if(SATCOMFEC_SME2_SOURCE_FLAG)
    set(SATCOMFEC_BENCHMARK_SME2_FLAGS "${SATCOMFEC_SME2_SOURCE_FLAG}")
else()
    set(SATCOMFEC_BENCHMARK_SME2_FLAGS "not-compiled")
endif()

function(satcomfec_add_string_definition target name value)
    string(REPLACE "\\" "\\\\" escaped_value "${value}")
    string(REPLACE "\"" "\\\"" escaped_value "${escaped_value}")
    target_compile_definitions("${target}" PRIVATE "${name}=\"${escaped_value}\"")
endfunction()

satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_GIT_SHA
    "${SATCOMFEC_BENCHMARK_GIT_SHA}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_GIT_DIRTY
    "${SATCOMFEC_BENCHMARK_GIT_DIRTY}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_BUILD_TYPE
    "${SATCOMFEC_BENCHMARK_BUILD_TYPE}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_COMMON_FLAGS
    "${SATCOMFEC_BENCHMARK_COMMON_FLAGS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_REFERENCE_FLAGS
    "${SATCOMFEC_BENCHMARK_REFERENCE_FLAGS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_NEON_FLAGS
    "${SATCOMFEC_BENCHMARK_NEON_FLAGS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_SME2_FLAGS
    "${SATCOMFEC_BENCHMARK_SME2_FLAGS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_TARGET
    "${SATCOMFEC_BENCHMARK_TARGET}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_BUILD_SYSTEM
    "cmake"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_CMAKE_VERSION
    "${CMAKE_VERSION}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_CMAKE_GENERATOR
    "${CMAKE_GENERATOR}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_COMPILER_PATH
    "${CMAKE_CXX_COMPILER}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_COMPILER_ID
    "${CMAKE_CXX_COMPILER_ID}"
)
if(CMAKE_CXX_COMPILER_TARGET)
    set(SATCOMFEC_BENCHMARK_COMPILER_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
else()
    set(SATCOMFEC_BENCHMARK_COMPILER_TARGET "compiler-default")
endif()
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_COMPILER_TARGET
    "${SATCOMFEC_BENCHMARK_COMPILER_TARGET}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_SYSTEM_NAME
    "${CMAKE_SYSTEM_NAME}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_SYSTEM_PROCESSOR
    "${CMAKE_SYSTEM_PROCESSOR}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_WARNINGS_ENABLED
    "${SATCOMFEC_ENABLE_WARNINGS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_WARNINGS_AS_ERRORS
    "${SATCOMFEC_WARNINGS_AS_ERRORS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_SANITIZERS_ENABLED
    "${SATCOMFEC_ENABLE_SANITIZERS}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_NEON_REQUESTED
    "${SATCOMFEC_ENABLE_NEON}"
)
satcomfec_add_string_definition(
    benchmark_acquisition SATCOMFEC_BENCHMARK_SME2_REQUESTED
    "${SATCOMFEC_ENABLE_SME2}"
)

if(NOT SATCOMFEC_ANDROID_BENCHMARK_ONLY)
    add_executable(check_branch_metrics tools/check_branch_metrics.cpp)
    target_link_libraries(
        check_branch_metrics
        PRIVATE satcom_replay_core satcomfec_project_options
    )

    add_executable(acquisition_demo tools/acquisition_demo.cpp)
    target_link_libraries(
        acquisition_demo
        PRIVATE satcom_replay_core satcomfec_project_options
    )

    add_executable(check_acquisition_kernels tools/check_acquisition_kernels.cpp)
    target_link_libraries(
        check_acquisition_kernels
        PRIVATE satcom_replay_core satcomfec_project_options
    )

    add_executable(check_sme2_acquisition tools/check_sme2_acquisition.cpp)
    target_link_libraries(
        check_sme2_acquisition
        PRIVATE satcom_replay_core satcomfec_project_options
    )
endif()

if(BUILD_TESTING AND NOT SATCOMFEC_ANDROID_BENCHMARK_ONLY)
    find_package(Python3 3 REQUIRED COMPONENTS Interpreter)
    if(SATCOMFEC_ENABLE_SME2)
        set(SATCOMFEC_COMPILE_CHECK_MODE "sme2")
    elseif(SATCOMFEC_ENABLE_NEON)
        set(SATCOMFEC_COMPILE_CHECK_MODE "neon")
    else()
        set(SATCOMFEC_COMPILE_CHECK_MODE "portable")
    endif()
    add_test(
        NAME fixture_manifest_integrity
        COMMAND "${Python3_EXECUTABLE}"
            scripts/update_fixture_checksums.py
            --check
    )
    set_tests_properties(
        fixture_manifest_integrity
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME compile_flag_separation
        COMMAND "${Python3_EXECUTABLE}"
            scripts/check_compile_commands.py
            --build-dir "${CMAKE_BINARY_DIR}"
            --expect "${SATCOMFEC_COMPILE_CHECK_MODE}"
    )
    set_tests_properties(
        compile_flag_separation
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME replay_acquisition_end_to_end
        COMMAND replay_demo
            --iq data/synthetic/canned_replay/demo_conv_bpsk.iq
            --metadata data/synthetic/canned_replay/demo_conv_bpsk.json
            --preamble data/synthetic/canned_replay/preamble_qpsk_256.iq
            --acquisition reference
            --decoder viterbi-reference
    )
    set_tests_properties(
        replay_acquisition_end_to_end
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME acquisition_reference_clean
        COMMAND acquisition_demo
            --iq data/synthetic/acquisition/clean.iq
            --metadata data/synthetic/acquisition/clean.json
    )
    set_tests_properties(
        acquisition_reference_clean
        PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME acquisition_kernel_equivalence COMMAND check_acquisition_kernels)
    add_test(NAME acquisition_sme2_equivalence COMMAND check_sme2_acquisition)
    add_test(
        NAME acquisition_benchmark_smoke
        COMMAND benchmark_acquisition
            --workload small
            --warmup-rounds 0
            --samples 3
            --min-sample-ms 1
    )
    if(SATCOMFEC_ACQUISITION_NEON_AVAILABLE)
        add_test(
            NAME acquisition_neon_clean
            COMMAND acquisition_demo
                --iq data/synthetic/acquisition/clean.iq
                --metadata data/synthetic/acquisition/clean.json
                --implementation neon
        )
        set_tests_properties(
            acquisition_neon_clean
            PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
    endif()
endif()
