#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/replay_demo"
ALLOW_FAILURE=0
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-failure)
      ALLOW_FAILURE=1
      shift
      ;;
    --help|-h)
      break
      ;;
    *)
      POSITIONAL_ARGS+=("$1")
      shift
      ;;
  esac
done

IQ_PATH="${POSITIONAL_ARGS[0]:-${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq}"
DECODER="${POSITIONAL_ARGS[1]:-viterbi-neon}"

if [[ "${POSITIONAL_ARGS[0]:-}" == "--help" || "${POSITIONAL_ARGS[0]:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/run_replay_demo.sh [--allow-failure] [iq_path] [decoder]

Supported decoders:
  viterbi-neon
  viterbi-sme2

Examples:
  bash scripts/run_replay_demo.sh
  bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk.iq viterbi-sme2
  bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq
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

if [[ "${ALLOW_FAILURE}" == "1" ]]; then
  set +e
  OUTPUT="$("${BIN_PATH}" --iq "${IQ_PATH}" --decoder "${DECODER}")"
  STATUS=$?
  set -e
  printf '%s\n' "${OUTPUT}"
  exit 0
fi

"${BIN_PATH}" --iq "${IQ_PATH}" --decoder "${DECODER}"
