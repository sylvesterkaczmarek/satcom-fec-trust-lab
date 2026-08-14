#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRE_NEON=0

if [[ "${1:-}" == "--require-neon" ]]; then
  REQUIRE_NEON=1
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check_acquisition_neon.sh [--require-neon]

Runs deterministic direct-kernel equivalence cases. On a portable non-Arm
build, the default command reports NEON as unavailable and exits successfully.
--require-neon fails unless the real NEON kernel was compiled and executed.
EOF
  exit 0
elif [[ $# -gt 0 ]]; then
  echo "error: unsupported argument '$1'" >&2
  exit 1
fi

if [[ "${REQUIRE_NEON}" == "1" ]]; then
  BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/acquisition_neon}"
else
  BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/host_replay}"
fi
if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

if [[ "${REQUIRE_NEON}" == "1" ]]; then
  SATCOMFEC_BUILD_DIR="${BUILD_DIR}" SATCOMFEC_ENABLE_NEON=ON \
    "${ROOT_DIR}/scripts/build_host_tools.sh" check_acquisition_kernels
else
  SATCOMFEC_BUILD_DIR="${BUILD_DIR}" \
    "${ROOT_DIR}/scripts/build_host_tools.sh" check_acquisition_kernels
fi

arguments=()
if [[ "${REQUIRE_NEON}" == "1" ]]; then
  arguments+=(--require-neon)
fi
"${BUILD_DIR}/check_acquisition_kernels" "${arguments[@]}"
