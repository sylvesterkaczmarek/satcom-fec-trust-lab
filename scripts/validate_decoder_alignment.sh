#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/validate_decoder_alignment.sh

Runs the benchmark harness on the canned replay frame and checks:
  - both decoder paths succeed
  - both decoder paths produce matching decoded bits
  - the benchmark assumptions report same input, settings, and evaluation window
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

OUTPUT="$(bash "${ROOT_DIR}/scripts/benchmark_decoder_paths.sh" "${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq" 2 20)"

echo "${OUTPUT}"

echo "${OUTPUT}" | jq -e '.ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.outputs_match == true' >/dev/null
echo "${OUTPUT}" | jq -e '.decoded_text == "SATCOM DEMO OK"' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_input_frame == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_decoder_settings == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_evaluation_window == true' >/dev/null
