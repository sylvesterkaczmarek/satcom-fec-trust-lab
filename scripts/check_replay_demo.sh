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

OUTPUT="$(bash "${ROOT_DIR}/scripts/run_replay_demo.sh")"

echo "${OUTPUT}"

echo "${OUTPUT}" | jq -e '.ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.decoded_text == "SATCOM DEMO OK"' >/dev/null
echo "${OUTPUT}" | jq -e '.crc_ok == true' >/dev/null
