#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/host_replay}"
TARGET="${1:-all}"

usage() {
  cat <<'EOF'
Usage: scripts/build_host_tools.sh TARGET

Targets:
  replay_demo, benchmark_acquisition, check_branch_metrics, acquisition_demo,
  check_acquisition_kernels, check_sme2_acquisition,
  viterbi_branch_metric_experiment, all

Environment:
  CXX=clang++                         Select the C++ compiler.
  SATCOMFEC_BUILD_DIR=PATH            Override build/host_replay.
  SATCOMFEC_BUILD_TYPE=Release        Select the CMake build type.
  SATCOMFEC_ENABLE_NEON=ON            Request explicit AArch64 NEON sources.
  SATCOMFEC_ENABLE_SME2=ON            Request explicit SME2 sources.
  SATCOMFEC_ENABLE_WARNINGS=ON        Enable -Wall -Wextra -Wpedantic.
  SATCOMFEC_WARNINGS_AS_ERRORS=OFF    Promote project warnings to errors.
  SATCOMFEC_ENABLE_SANITIZERS=OFF     Enable host ASan and UBSan.
EOF
}

if [[ "${TARGET}" == "--help" || "${TARGET}" == "-h" ]]; then
  usage
  exit 0
fi

case "${TARGET}" in
  replay_demo|benchmark_acquisition|check_branch_metrics|acquisition_demo|\
  check_acquisition_kernels|check_sme2_acquisition|\
  viterbi_branch_metric_experiment|all)
    ;;
  *)
    echo "error: unsupported build target '${TARGET}'" >&2
    usage >&2
    exit 2
    ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake 3.22 or newer is required for the supported host build" >&2
  exit 1
fi

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

cmake_args=(
  -S "${ROOT_DIR}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${SATCOMFEC_BUILD_TYPE:-Release}"
  -DSATCOMFEC_ENABLE_NEON="${SATCOMFEC_ENABLE_NEON:-OFF}"
  -DSATCOMFEC_ENABLE_SME2="${SATCOMFEC_ENABLE_SME2:-OFF}"
  -DSATCOMFEC_ENABLE_WARNINGS="${SATCOMFEC_ENABLE_WARNINGS:-ON}"
  -DSATCOMFEC_WARNINGS_AS_ERRORS="${SATCOMFEC_WARNINGS_AS_ERRORS:-OFF}"
  -DSATCOMFEC_ENABLE_SANITIZERS="${SATCOMFEC_ENABLE_SANITIZERS:-OFF}"
)
if [[ -n "${CXX:-}" ]]; then
  cmake_args+=("-DCMAKE_CXX_COMPILER=${CXX}")
fi

echo "Configuring host tools in ${BUILD_DIR}" >&2
cmake "${cmake_args[@]}" >&2

build_args=(--build "${BUILD_DIR}" --parallel)
if [[ "${TARGET}" != "all" ]]; then
  build_args+=(--target "${TARGET}")
fi

echo "Building ${TARGET}" >&2
cmake "${build_args[@]}" >&2
