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
    cmake --build "${build_dir}" --target \
      replay_demo \
      benchmark_decoders \
      benchmark_acquisition \
      check_branch_metrics \
      acquisition_demo \
      check_acquisition_kernels \
      check_sme2_acquisition
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

if command -v cmake >/dev/null 2>&1; then
  portable_benchmark="${BUILD_ROOT}/portable/benchmark_acquisition"
else
  portable_benchmark="${ROOT_DIR}/build/host_replay/benchmark_acquisition"
fi

echo
echo "Checking portable acquisition benchmark report:"
"${portable_benchmark}" \
  --workload small \
  --warmup-rounds 0 \
  --samples 3 \
  --min-sample-ms 1 \
  >/dev/null
echo "Portable acquisition benchmark report passed."

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
    sme2_acquisition_checker="${BUILD_ROOT}/sme2/check_sme2_acquisition"
    sme2_benchmark="${BUILD_ROOT}/sme2/benchmark_acquisition"
  else
    SATCOMFEC_ENABLE_SME2=ON bash "${ROOT_DIR}/scripts/build_host_tools.sh" all
    sme2_acquisition_checker="${ROOT_DIR}/build/host_replay/check_sme2_acquisition"
    sme2_benchmark="${ROOT_DIR}/build/host_replay/benchmark_acquisition"
  fi

  echo
  echo "Checking SME2 acquisition correctness and runtime availability:"
  "${sme2_acquisition_checker}"

  echo
  echo "Checking SME2-build acquisition benchmark report:"
  "${sme2_benchmark}" \
    --workload small \
    --warmup-rounds 0 \
    --samples 3 \
    --min-sample-ms 1 \
    >/dev/null
  echo "SME2-build acquisition benchmark report passed."

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
