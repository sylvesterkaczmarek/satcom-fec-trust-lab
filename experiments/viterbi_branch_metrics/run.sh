#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/viterbi_branch_metric_experiment"
IQ_PATH="${1:-${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq}"
WARMUP="${2:-10}"
ITERATIONS="${3:-500}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: experiments/viterbi_branch_metrics/run.sh [iq_path] [warmup_iterations] [timed_iterations]

Examples:
  bash experiments/viterbi_branch_metrics/run.sh
  bash experiments/viterbi_branch_metrics/run.sh data/synthetic/canned_replay/demo_conv_bpsk.iq 5 100
EOF
  exit 0
fi

if [[ ! -f "${IQ_PATH}" ]]; then
  echo "error: IQ file not found: ${IQ_PATH}" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"
"${ROOT_DIR}/scripts/build_host_tools.sh" viterbi_branch_metric_experiment

"${BIN_PATH}" --iq "${IQ_PATH}" --warmup "${WARMUP}" --iterations "${ITERATIONS}"
