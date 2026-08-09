#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/acquisition_demo"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/run_acquisition_demo.sh [iq_path] [metadata_path] [reference|neon]

Runs the selected acquisition search. When metadata_path is omitted,
the tool reads a JSON sidecar with the same basename as the IQ fixture. The
implementation defaults to reference. A requested NEON path must be compiled;
there is no scalar fallback under the NEON label.
EOF
  exit 0
fi

IQ_PATH="${1:-${ROOT_DIR}/data/synthetic/acquisition/clean.iq}"
METADATA_PATH="${2:-${IQ_PATH%.iq}.json}"
IMPLEMENTATION="${3:-reference}"

if [[ ! -f "${IQ_PATH}" ]]; then
  echo "error: acquisition IQ file not found: ${IQ_PATH}" >&2
  exit 1
fi
if [[ ! -f "${METADATA_PATH}" ]]; then
  echo "error: acquisition metadata file not found: ${METADATA_PATH}" >&2
  exit 1
fi

"${ROOT_DIR}/scripts/build_host_tools.sh" acquisition_demo
"${BIN_PATH}" \
  --iq "${IQ_PATH}" \
  --metadata "${METADATA_PATH}" \
  --implementation "${IMPLEMENTATION}"
