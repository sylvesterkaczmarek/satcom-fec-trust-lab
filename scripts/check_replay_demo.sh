#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check_replay_demo.sh

Builds and runs the canned replay demo, then checks:
  - ok == true
  - decoded_text == "SATCOM DEMO OK"
  - crc_ok == true
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

METADATA_PATH="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.json"
OUTPUT="$(bash "${ROOT_DIR}/scripts/run_replay_demo.sh")"

echo "${OUTPUT}"

echo "${OUTPUT}" | jq -e '.ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.decoded_text == "SATCOM DEMO OK"' >/dev/null
echo "${OUTPUT}" | jq -e '.crc_ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.demod.samples_per_symbol == 8' >/dev/null
echo "${OUTPUT}" | jq -e '.framing.frame_length == .frame_soft_bits' >/dev/null
echo "${OUTPUT}" | jq -e '.trust_breakdown.score == .trust_score' >/dev/null
echo "${OUTPUT}" | jq -e '.trust_breakdown.capped_by_crc_failure == false' >/dev/null

echo "${OUTPUT}" | jq -e --slurpfile metadata "${METADATA_PATH}" \
  '.decoded_text == $metadata[0].message and
   .demod.samples_per_symbol == $metadata[0].samples_per_symbol and
   .frame_soft_bits == $metadata[0].coded_bits_per_frame' >/dev/null
