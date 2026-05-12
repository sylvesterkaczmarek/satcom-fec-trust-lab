#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/verify_arm_paths"
CXX_BIN="${CXX:-c++}"

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

build_with_cmake_or_script() {
  local build_dir="$1"
  shift
  local -a cmake_options=("$@")

  if command -v cmake >/dev/null 2>&1; then
    cmake -S "${ROOT_DIR}" -B "${build_dir}" \
      -DCMAKE_BUILD_TYPE=Release \
      "${cmake_options[@]}"
    cmake --build "${build_dir}" --target replay_demo benchmark_decoders check_branch_metrics
    return
  fi

  bash "${ROOT_DIR}/scripts/build_host_tools.sh" all
}

echo "Compiler:"
"${CXX_BIN}" --version

echo
echo "Detected architecture:"
uname -m

echo
echo "Default portable build:"
build_with_cmake_or_script "${BUILD_ROOT}/portable" \
  -DSATCOMFEC_ENABLE_NEON=OFF \
  -DSATCOMFEC_ENABLE_SME2=OFF

echo
echo "Running unit tests on default build:"
python3 -m unittest discover -s "${ROOT_DIR}/tests" -v

echo
echo "Checking branch-metric path selection on default build:"
bash "${ROOT_DIR}/scripts/check_branch_metrics.sh"

echo
echo "SME2 build probe:"
if sme2_flag="$(select_sme2_flag)"; then
  echo "SME2 compiler support detected with ${sme2_flag}"
  if command -v cmake >/dev/null 2>&1; then
    build_with_cmake_or_script "${BUILD_ROOT}/sme2" \
      -DSATCOMFEC_ENABLE_SME2=ON
  else
    SATCOMFEC_ENABLE_SME2=ON bash "${ROOT_DIR}/scripts/build_host_tools.sh" all
  fi

  sme2_obj="${BUILD_ROOT}/branch_metrics_sme2.o"
  mkdir -p "${BUILD_ROOT}"
  "${CXX_BIN}" -std=c++17 -O2 "${sme2_flag}" \
    -I"${ROOT_DIR}/src" \
    -c "${ROOT_DIR}/src/fec/branch_metrics_sme2.cpp" \
    -o "${sme2_obj}"

  if command -v nm >/dev/null 2>&1; then
    echo
    echo "SME2 object symbols:"
    nm "${sme2_obj}" | grep -E 'prepare_branch_metrics_sme2|branch_metrics_sme2' || true
  elif command -v objdump >/dev/null 2>&1; then
    echo
    echo "SME2 object symbols:"
    objdump -t "${sme2_obj}" | grep -E 'prepare_branch_metrics_sme2|branch_metrics_sme2' || true
  else
    echo "nm/objdump not available; skipped SME2 object symbol listing"
  fi
else
  echo "SME2 compiler support not detected; skipped SATCOMFEC_ENABLE_SME2=ON build."
  echo "This is expected on x86 CI and on Arm toolchains without SME2 ACLE support."
fi
