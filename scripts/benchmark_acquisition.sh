#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -z "${SATCOMFEC_BUILD_DIR:-}" ]]; then
  if [[ "${SATCOMFEC_ENABLE_SME2:-OFF}" == "ON" ]]; then
    SATCOMFEC_BUILD_DIR="${ROOT_DIR}/build/benchmark/sme2"
  elif [[ "${SATCOMFEC_ENABLE_NEON:-OFF}" == "ON" ]]; then
    SATCOMFEC_BUILD_DIR="${ROOT_DIR}/build/benchmark/neon"
  else
    SATCOMFEC_BUILD_DIR="${ROOT_DIR}/build/benchmark/portable"
  fi
fi
if [[ "${SATCOMFEC_BUILD_DIR}" != /* ]]; then
  SATCOMFEC_BUILD_DIR="${ROOT_DIR}/${SATCOMFEC_BUILD_DIR}"
fi
export SATCOMFEC_BUILD_DIR

bash "${ROOT_DIR}/scripts/build_host_tools.sh" benchmark_acquisition
exec "${SATCOMFEC_BUILD_DIR}/benchmark_acquisition" "$@"
