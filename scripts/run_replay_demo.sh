#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/host_replay}"
if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi
BIN_PATH="${BUILD_DIR}/replay_demo"
ALLOW_FAILURE=0
ACQUISITION="reference"
METADATA_PATH=""
PREAMBLE_PATH=""
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --allow-failure)
      ALLOW_FAILURE=1
      shift
      ;;
    --acquisition)
      if [[ $# -lt 2 ]]; then
        echo "error: --acquisition requires reference, neon, or sme2" >&2
        exit 1
      fi
      ACQUISITION="$2"
      shift 2
      ;;
    --metadata)
      if [[ $# -lt 2 ]]; then
        echo "error: --metadata requires a path" >&2
        exit 1
      fi
      METADATA_PATH="$2"
      shift 2
      ;;
    --preamble)
      if [[ $# -lt 2 ]]; then
        echo "error: --preamble requires a path" >&2
        exit 1
      fi
      PREAMBLE_PATH="$2"
      shift 2
      ;;
    --help|-h)
      cat <<'EOF'
Usage: scripts/run_replay_demo.sh [--allow-failure]
       [--acquisition reference|neon|sme2] [--metadata path]
       [--preamble path] [iq_path] [decoder]

Supported decoders:
  viterbi-reference
  viterbi-neon

The portable default uses reference IQ acquisition. Requested NEON or SME2
acquisition fails explicitly when that implementation is unavailable.
EOF
      exit 0
      ;;
    *)
      POSITIONAL_ARGS+=("$1")
      shift
      ;;
  esac
done

IQ_PATH="${POSITIONAL_ARGS[0]:-${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq}"
DECODER="${POSITIONAL_ARGS[1]:-viterbi-reference}"

if [[ ! -f "${IQ_PATH}" ]]; then
  echo "error: IQ file not found: ${IQ_PATH}" >&2
  exit 1
fi

if [[ -n "${PREAMBLE_PATH}" && ! -f "${PREAMBLE_PATH}" ]]; then
  echo "error: acquisition preamble not found: ${PREAMBLE_PATH}" >&2
  exit 1
fi

case "${DECODER}" in
  viterbi-reference|viterbi-neon)
    ;;
  *)
    echo "error: unsupported decoder '${DECODER}'" >&2
    exit 1
    ;;
esac

case "${ACQUISITION}" in
  reference|neon|sme2)
    ;;
  *)
    echo "error: unsupported acquisition implementation '${ACQUISITION}'" >&2
    exit 1
    ;;
esac

if [[ -z "${METADATA_PATH}" ]]; then
  INFERRED_METADATA="${IQ_PATH%.*}.json"
  if [[ -f "${INFERRED_METADATA}" ]]; then
    METADATA_PATH="${INFERRED_METADATA}"
  fi
fi

CLI_ARGS=(
  --iq "${IQ_PATH}"
  --acquisition "${ACQUISITION}"
  --decoder "${DECODER}"
)
if [[ -n "${METADATA_PATH}" ]]; then
  CLI_ARGS+=(--metadata "${METADATA_PATH}")
fi
if [[ -n "${PREAMBLE_PATH}" ]]; then
  CLI_ARGS+=(--preamble "${PREAMBLE_PATH}")
fi

SATCOMFEC_BUILD_DIR="${BUILD_DIR}" \
  "${ROOT_DIR}/scripts/build_host_tools.sh" replay_demo

if [[ "${ALLOW_FAILURE}" == "1" ]]; then
  set +e
  OUTPUT="$("${BIN_PATH}" "${CLI_ARGS[@]}")"
  STATUS=$?
  set -e
  printf '%s\n' "${OUTPUT}"
  exit 0
fi

"${BIN_PATH}" "${CLI_ARGS[@]}"
