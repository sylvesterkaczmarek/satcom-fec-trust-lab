#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/host_replay}"
PREFIX="${SATCOMFEC_INSTALL_PREFIX:-${1:-${ROOT_DIR}/build/install}}"

usage() {
  cat <<'EOF'
Usage: scripts/install_host_tools.sh [PREFIX]

Builds the supported host command-line tools and installs them under PREFIX/bin.

Environment:
  SATCOMFEC_BUILD_DIR=PATH          Override build/host_replay.
  SATCOMFEC_INSTALL_PREFIX=PATH     Override the install prefix.
  SATCOMFEC_BUILD_TYPE=Release      Select the CMake build type.
  SATCOMFEC_ENABLE_WARNINGS=ON      Enable project compiler warnings.
  SATCOMFEC_WARNINGS_AS_ERRORS=OFF  Promote project warnings to errors.
EOF
}

if [[ "${PREFIX}" == "--help" || "${PREFIX}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi
if [[ "${PREFIX}" != /* ]]; then
  PREFIX="${ROOT_DIR}/${PREFIX}"
fi

SATCOMFEC_BUILD_DIR="${BUILD_DIR}" \
  bash "${ROOT_DIR}/scripts/build_host_tools.sh" all

BIN_DIR="${PREFIX}/bin"
mkdir -p "${BIN_DIR}"

for tool in replay_demo acquisition_demo benchmark_acquisition; do
  source_path="${BUILD_DIR}/${tool}"
  if [[ ! -x "${source_path}" ]]; then
    echo "error: expected built executable was not found: ${source_path}" >&2
    exit 1
  fi
  cp "${source_path}" "${BIN_DIR}/${tool}"
  chmod 0755 "${BIN_DIR}/${tool}"
done

printf 'Installed host tools to %s\n' "${BIN_DIR}"
