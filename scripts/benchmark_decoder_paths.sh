#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
BIN_PATH="${BUILD_DIR}/benchmark_decoders"
IQ_PATH="${1:-${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq}"
WARMUP="${2:-10}"
ITERATIONS="${3:-500}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/benchmark_decoder_paths.sh [iq_path] [warmup_iterations] [timed_iterations]

Examples:
  bash scripts/benchmark_decoder_paths.sh
  bash scripts/benchmark_decoder_paths.sh data/synthetic/canned_replay/demo_conv_bpsk.iq 5 100
EOF
  exit 0
fi

if ! command -v c++ >/dev/null 2>&1; then
  echo "error: c++ compiler not found in PATH" >&2
  exit 1
fi

if [[ ! -f "${IQ_PATH}" ]]; then
  echo "error: IQ file not found: ${IQ_PATH}" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"

c++ -std=c++17 -O2 \
  -I"${ROOT_DIR}/app/src/main/cpp" \
  "${ROOT_DIR}/tools/benchmark_decoders.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/demo/replay_pipeline.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/dsp/front_end_dsp.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/dsp/framing.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/dsp/soft_demod.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/convolutional_codec.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_bitflip.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_decoder_neon.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_decoder_sme2.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/viterbi_decoder_neon.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/fec/viterbi_decoder_sme2.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/trust/trust_features.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/trust/trust_score.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/util/iq_reader.cpp" \
  "${ROOT_DIR}/app/src/main/cpp/util/logging.cpp" \
  -o "${BIN_PATH}"

"${BIN_PATH}" --iq "${IQ_PATH}" --warmup "${WARMUP}" --iterations "${ITERATIONS}"
