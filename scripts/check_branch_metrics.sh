#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/check_branch_metrics"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check_branch_metrics.sh

Builds and runs deterministic branch-metric equivalence checks for:
  - scalar reference
  - NEON, or reference fallback when NEON is not compiled
  - SME2, or reference fallback when SME2 is not compiled
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

"${ROOT_DIR}/scripts/build_host_tools.sh" check_branch_metrics
OUTPUT="$("${BIN_PATH}")"

echo "${OUTPUT}"

echo "${OUTPUT}" | jq -e '.ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.implementations.reference.selected == "reference"' >/dev/null
echo "${OUTPUT}" |
  jq -e '.implementations.neon.selected == "neon" or .implementations.neon.selected == "fallback"' >/dev/null
echo "${OUTPUT}" |
  jq -e '.implementations.streaming_vector.selected == "streaming-sve" or .implementations.streaming_vector.selected == "fallback"' >/dev/null
echo "${OUTPUT}" |
  jq -e 'all(.cases[]; .neon_matches_reference == true and .streaming_vector_matches_reference == true)' >/dev/null
