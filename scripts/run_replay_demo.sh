#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/replay_demo"
IQ_PATH="${1:-${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq}"
DECODER="${2:-viterbi-neon}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/run_replay_demo.sh [iq_path] [decoder]

Supported decoders:
  viterbi-neon
  viterbi-sme2

Examples:
  bash scripts/run_replay_demo.sh
  bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk.iq viterbi-sme2
EOF
  exit 0
fi

if [[ ! -f "${IQ_PATH}" ]]; then
  echo "error: IQ file not found: ${IQ_PATH}" >&2
  exit 1
fi

case "${DECODER}" in
  viterbi-neon|viterbi-sme2)
    ;;
  *)
    echo "error: unsupported decoder '${DECODER}'" >&2
    exit 1
    ;;
esac

mkdir -p "${BUILD_DIR}"
"${ROOT_DIR}/scripts/build_host_tools.sh" replay_demo

"${BIN_PATH}" --iq "${IQ_PATH}" --decoder "${DECODER}"
