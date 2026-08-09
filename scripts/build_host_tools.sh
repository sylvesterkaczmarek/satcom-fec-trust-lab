#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
TARGET="${1:-all}"
CXX_BIN="${CXX:-c++}"

if [[ "${TARGET}" == "--help" || "${TARGET}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/build_host_tools.sh [replay_demo|benchmark_decoders|check_branch_metrics|acquisition_demo|check_acquisition_kernels|all]

Configures and builds the supported host-side executables into build/host_replay/.

Environment:
  CXX=clang++                  Select the C++ compiler for direct fallback builds.
  SATCOMFEC_ENABLE_NEON=ON   Request an explicit AArch64 NEON build.
  SATCOMFEC_ENABLE_SME2=ON   Request an explicit AArch64 SME2 build.
EOF
  exit 0
fi

mkdir -p "${BUILD_DIR}"

flag_supported() {
  local flag="$1"
  printf '%s\n' 'int main() { return 0; }' |
    "${CXX_BIN}" -std=c++17 "${flag}" -x c++ -fsyntax-only - >/dev/null 2>&1
}

sme2_acle_supported() {
  local flag="$1"
  printf '%s\n' \
    '#if !defined(__ARM_FEATURE_SME2)' \
    '#error SME2 macro not defined' \
    '#endif' \
    '#include <arm_sve.h>' \
    '#if defined(__has_include)' \
    '#if __has_include(<arm_sme.h>)' \
    '#include <arm_sme.h>' \
    '#endif' \
    '#else' \
    '#include <arm_sme.h>' \
    '#endif' \
    '__arm_locally_streaming void probe() {}' \
    'int main() { return 0; }' |
    "${CXX_BIN}" -std=c++17 "${flag}" -x c++ -fsyntax-only - >/dev/null 2>&1
}

neon_acle_supported() {
  local flag="$1"
  if [[ -n "${flag}" ]]; then
    printf '%s\n' \
      '#if !defined(__ARM_NEON) && !defined(__ARM_NEON__)' \
      '#error NEON macro not defined' \
      '#endif' \
      '#include <arm_neon.h>' \
      'int main() { int8x16_t v = vdupq_n_s8(1); return vgetq_lane_s8(v, 0) == 1 ? 0 : 1; }' |
      "${CXX_BIN}" -std=c++17 "${flag}" -x c++ -fsyntax-only - >/dev/null 2>&1
    return
  fi
  printf '%s\n' \
    '#if !defined(__ARM_NEON) && !defined(__ARM_NEON__)' \
    '#error NEON macro not defined' \
    '#endif' \
    '#include <arm_neon.h>' \
    'int main() { int8x16_t v = vdupq_n_s8(1); return vgetq_lane_s8(v, 0) == 1 ? 0 : 1; }' |
    "${CXX_BIN}" -std=c++17 -x c++ -fsyntax-only - >/dev/null 2>&1
}

select_sme2_flag() {
  local flag
  local flags=(-march=armv9.4-a+sme2 -march=armv9.2-a+sme2)
  if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
    flags=(-mcpu=native+sme2 -march=native+sme2 "${flags[@]}")
  fi

  for flag in "${flags[@]}"; do
    if flag_supported "${flag}" && sme2_acle_supported "${flag}"; then
      printf '%s\n' "${flag}"
      return 0
    fi
  done
  return 1
}

select_neon_flag() {
  local flag
  for flag in -march=armv8-a+simd -march=armv8-a; do
    if flag_supported "${flag}" && neon_acle_supported "${flag}"; then
      printf '%s\n' "${flag}"
      return 0
    fi
  done
  return 1
}

build_with_compiler() {
  if ! command -v "${CXX_BIN}" >/dev/null 2>&1; then
    echo "error: neither cmake nor ${CXX_BIN} is available in PATH" >&2
    exit 1
  fi

  local neon_source_flag=""
  local sme2_source_flag=""
  local acquisition_neon_flag=""
  local acquisition_neon_definition=""
  local reference_no_loop_vectorize_flag=""
  local reference_no_slp_vectorize_flag=""
  if flag_supported "-fno-vectorize"; then
    reference_no_loop_vectorize_flag="-fno-vectorize"
  elif flag_supported "-fno-tree-vectorize"; then
    reference_no_loop_vectorize_flag="-fno-tree-vectorize"
  fi
  if flag_supported "-fno-slp-vectorize"; then
    reference_no_slp_vectorize_flag="-fno-slp-vectorize"
  elif flag_supported "-fno-tree-slp-vectorize"; then
    reference_no_slp_vectorize_flag="-fno-tree-slp-vectorize"
  fi
  if [[ "${SATCOMFEC_ENABLE_SME2:-OFF}" == "ON" ]]; then
    local sme2_flag
    if ! sme2_flag="$(select_sme2_flag)"; then
      echo "error: SATCOMFEC_ENABLE_SME2=ON requires a supported SME2 target flag plus ACLE SME2 attributes" >&2
      echo "       tried Darwin arm64 native+sme2 when applicable, then -march=armv9.4-a+sme2 and -march=armv9.2-a+sme2" >&2
      exit 1
    fi
    sme2_source_flag="${sme2_flag}"
    echo "info: SME2 build enabled with ${sme2_flag}" >&2
  elif [[ "${SATCOMFEC_ENABLE_NEON:-OFF}" == "ON" ]]; then
    local neon_flag
    if ! neon_flag="$(select_neon_flag)"; then
      echo "error: SATCOMFEC_ENABLE_NEON=ON requires -march=armv8-a+simd or -march=armv8-a plus NEON ACLE support" >&2
      exit 1
    fi
    neon_source_flag="${neon_flag}"
    acquisition_neon_flag="${neon_flag}"
    acquisition_neon_definition="-DSATCOMFEC_ACQUISITION_NEON_COMPILED=1"
    echo "info: NEON build enabled with ${neon_flag}" >&2
  fi
  if [[ -z "${acquisition_neon_definition}" ]] && neon_acle_supported ""; then
    acquisition_neon_definition="-DSATCOMFEC_ACQUISITION_NEON_COMPILED=1"
    echo "info: native AArch64 NEON acquisition kernel enabled" >&2
  fi

  local common_sources=(
    "${ROOT_DIR}/src/acquisition/acquisition_neon.cpp"
    "${ROOT_DIR}/src/acquisition/acquisition_plan.cpp"
    "${ROOT_DIR}/src/acquisition/acquisition_reference.cpp"
    "${ROOT_DIR}/src/acquisition/acquisition_runner.cpp"
    "${ROOT_DIR}/src/demo/replay_pipeline.cpp"
    "${ROOT_DIR}/src/dsp/front_end_dsp.cpp"
    "${ROOT_DIR}/src/dsp/framing.cpp"
    "${ROOT_DIR}/src/dsp/soft_demod.cpp"
    "${ROOT_DIR}/src/fec/branch_metrics_sme2.cpp"
    "${ROOT_DIR}/src/fec/convolutional_codec.cpp"
    "${ROOT_DIR}/src/fec/ldpc_bitflip.cpp"
    "${ROOT_DIR}/src/fec/viterbi_decoder_neon.cpp"
    "${ROOT_DIR}/src/fec/viterbi_decoder_reference.cpp"
    "${ROOT_DIR}/src/fec/viterbi_decoder_sme2.cpp"
    "${ROOT_DIR}/src/trust/trust_features.cpp"
    "${ROOT_DIR}/src/trust/trust_score.cpp"
    "${ROOT_DIR}/src/util/iq_reader.cpp"
    "${ROOT_DIR}/src/util/logging.cpp"
  )

  local object_dir="${BUILD_DIR}/direct_objects"
  mkdir -p "${object_dir}"
  local common_objects=()
  local source
  for source in "${common_sources[@]}"; do
    local object_name
    object_name="$(basename "${source}" .cpp).o"
    local source_flags=()
    if [[ "${source}" == "${ROOT_DIR}/src/acquisition/acquisition_neon.cpp" ]]; then
      if [[ -n "${acquisition_neon_flag}" ]]; then
        source_flags+=("${acquisition_neon_flag}")
      fi
      if [[ -n "${acquisition_neon_definition}" ]]; then
        source_flags+=("${acquisition_neon_definition}")
      fi
    elif [[ "${source}" == "${ROOT_DIR}/src/acquisition/acquisition_reference.cpp" ]]; then
      if [[ -n "${reference_no_loop_vectorize_flag}" ]]; then
        source_flags+=("${reference_no_loop_vectorize_flag}")
      fi
      if [[ -n "${reference_no_slp_vectorize_flag}" ]]; then
        source_flags+=("${reference_no_slp_vectorize_flag}")
      fi
    elif [[ "${source}" == "${ROOT_DIR}/src/fec/branch_metrics_sme2.cpp" ]]; then
      if [[ -n "${sme2_source_flag}" ]]; then
        source_flags+=("${sme2_source_flag}")
      fi
    elif [[ "${source}" == "${ROOT_DIR}/src/fec/convolutional_codec.cpp" ]]; then
      if [[ -n "${neon_source_flag}" ]]; then
        source_flags+=("${neon_source_flag}")
      fi
      if [[ -n "${sme2_source_flag}" ]]; then
        source_flags+=("${sme2_source_flag}")
      fi
    fi

    "${CXX_BIN}" -std=c++17 -O2 \
      ${source_flags[@]+"${source_flags[@]}"} \
      -I"${ROOT_DIR}/src" \
      -c "${source}" \
      -o "${object_dir}/${object_name}"
    common_objects+=("${object_dir}/${object_name}")
  done

  build_binary() {
    local output_name="$1"
    local entry_source="$2"
    "${CXX_BIN}" -std=c++17 -O2 \
      -I"${ROOT_DIR}/src" \
      "${entry_source}" \
      "${common_objects[@]}" \
      -o "${BUILD_DIR}/${output_name}"
  }

  case "${TARGET}" in
    replay_demo)
      build_binary "replay_demo" "${ROOT_DIR}/tools/replay_demo.cpp"
      ;;
    benchmark_decoders)
      build_binary "benchmark_decoders" "${ROOT_DIR}/tools/benchmark_decoders.cpp"
      ;;
    check_branch_metrics)
      build_binary "check_branch_metrics" "${ROOT_DIR}/tools/check_branch_metrics.cpp"
      ;;
    acquisition_demo)
      build_binary "acquisition_demo" "${ROOT_DIR}/tools/acquisition_demo.cpp"
      ;;
    check_acquisition_kernels)
      build_binary "check_acquisition_kernels" "${ROOT_DIR}/tools/check_acquisition_kernels.cpp"
      ;;
    all)
      build_binary "replay_demo" "${ROOT_DIR}/tools/replay_demo.cpp"
      build_binary "benchmark_decoders" "${ROOT_DIR}/tools/benchmark_decoders.cpp"
      build_binary "check_branch_metrics" "${ROOT_DIR}/tools/check_branch_metrics.cpp"
      build_binary "acquisition_demo" "${ROOT_DIR}/tools/acquisition_demo.cpp"
      build_binary "check_acquisition_kernels" "${ROOT_DIR}/tools/check_acquisition_kernels.cpp"
      ;;
    *)
      echo "error: unsupported build target '${TARGET}'" >&2
      exit 1
      ;;
  esac
}

if ! command -v cmake >/dev/null 2>&1; then
  build_with_compiler
  exit 0
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSATCOMFEC_ENABLE_NEON="${SATCOMFEC_ENABLE_NEON:-OFF}" \
  -DSATCOMFEC_ENABLE_SME2="${SATCOMFEC_ENABLE_SME2:-OFF}" \
  >/dev/null

case "${TARGET}" in
  replay_demo)
    cmake --build "${BUILD_DIR}" --target replay_demo >/dev/null
    ;;
  benchmark_decoders)
    cmake --build "${BUILD_DIR}" --target benchmark_decoders >/dev/null
    ;;
  check_branch_metrics)
    cmake --build "${BUILD_DIR}" --target check_branch_metrics >/dev/null
    ;;
  acquisition_demo)
    cmake --build "${BUILD_DIR}" --target acquisition_demo >/dev/null
    ;;
  check_acquisition_kernels)
    cmake --build "${BUILD_DIR}" --target check_acquisition_kernels >/dev/null
    ;;
  all)
    cmake --build "${BUILD_DIR}" --target replay_demo benchmark_decoders check_branch_metrics acquisition_demo check_acquisition_kernels >/dev/null
    ;;
  *)
    echo "error: unsupported build target '${TARGET}'" >&2
    exit 1
    ;;
esac
