#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${ROOT_DIR}/build/verify_arm_paths"
CXX_BIN="${CXX:-c++}"
ARCHITECTURE="$(uname -m)"

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake 3.22 or newer is required" >&2
  exit 1
fi
if ! command -v "${CXX_BIN}" >/dev/null 2>&1; then
  echo "error: C++ compiler not found: ${CXX_BIN}" >&2
  exit 1
fi

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
    '__arm_locally_streaming __arm_new("za") void probe() {' \
    '  const svfloat32_t zero = svdup_f32(0.0F);' \
    '  const svfloat32x4_t group = svcreate4_f32(zero, zero, zero, zero);' \
    '  svwrite_za32_f32_vg1x4(0, group);' \
    '  svmla_single_za32_f32_vg1x4(0, group, zero);' \
    '  svmls_single_za32_f32_vg1x4(0, group, zero);' \
    '  (void)svread_za32_f32_vg1x4(0);' \
    '}' \
    'int main() { return 0; }' |
    "${CXX_BIN}" -std=c++17 "${flag}" -x c++ -fsyntax-only - >/dev/null 2>&1
}

select_sme2_flag() {
  local flag
  local flags=(-march=armv9.4-a+sme2 -march=armv9.2-a+sme2)
  if [[ "$(uname -s)" == "Darwin" && "${ARCHITECTURE}" == "arm64" ]]; then
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

configure_and_build() {
  local build_dir="$1"
  shift
  cmake -S "${ROOT_DIR}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="${CXX_BIN}" \
    -DSATCOMFEC_ENABLE_WARNINGS=ON \
    -DSATCOMFEC_WARNINGS_AS_ERRORS=ON \
    "$@"
  cmake --build "${build_dir}" --parallel
}

run_build_checks() {
  local build_dir="$1"
  local mode="$2"
  python3 "${ROOT_DIR}/scripts/check_compile_commands.py" \
    --build-dir "${build_dir}" \
    --expect "${mode}"
  ctest --test-dir "${build_dir}" --output-on-failure
  "${build_dir}/benchmark_acquisition" \
    --workload small \
    --warmup-rounds 0 \
    --samples 3 \
    --min-sample-ms 1 \
    >/dev/null
  echo "${mode} build and correctness checks passed."
}

echo "Compiler:"
"${CXX_BIN}" --version
echo
echo "Architecture: ${ARCHITECTURE}"

cmake -E remove_directory "${BUILD_ROOT}"

echo
echo "Portable build:"
configure_and_build "${BUILD_ROOT}/portable" \
  -DSATCOMFEC_ENABLE_NEON=OFF \
  -DSATCOMFEC_ENABLE_SME2=OFF
run_build_checks "${BUILD_ROOT}/portable" portable

case "${ARCHITECTURE}" in
  arm64|aarch64)
    echo
    echo "Explicit NEON build:"
    configure_and_build "${BUILD_ROOT}/neon" \
      -DSATCOMFEC_ENABLE_NEON=ON \
      -DSATCOMFEC_ENABLE_SME2=OFF
    run_build_checks "${BUILD_ROOT}/neon" neon
    ;;
  *)
    echo
    echo "Explicit NEON execution skipped: ${ARCHITECTURE} is not AArch64."
    ;;
esac

echo
echo "SME2 build probe:"
if sme2_flag="$(select_sme2_flag)"; then
  echo "SME2 ACLE compiler support detected with ${sme2_flag}."
  configure_and_build "${BUILD_ROOT}/sme2" \
    -DSATCOMFEC_ENABLE_NEON=OFF \
    -DSATCOMFEC_ENABLE_SME2=ON
  run_build_checks "${BUILD_ROOT}/sme2" sme2
  "${BUILD_ROOT}/sme2/check_sme2_acquisition"
else
  echo "SME2 ACLE compiler support was not detected; SME2 build skipped."
  echo "This is expected on x86 and on Arm toolchains without SME2 ACLE support."
fi

echo
echo "Arm path verification passed."
