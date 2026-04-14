#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
TARGET="${1:-all}"

if [[ "${TARGET}" == "--help" || "${TARGET}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/build_host_tools.sh [replay_demo|benchmark_decoders|all]

Builds the supported host-side executables into build/host_replay/.
EOF
  exit 0
fi

if ! command -v c++ >/dev/null 2>&1; then
  echo "error: c++ compiler not found in PATH" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"

COMMON_SOURCES=(
  "${ROOT_DIR}/app/src/main/cpp/demo/replay_pipeline.cpp"
  "${ROOT_DIR}/app/src/main/cpp/dsp/front_end_dsp.cpp"
  "${ROOT_DIR}/app/src/main/cpp/dsp/framing.cpp"
  "${ROOT_DIR}/app/src/main/cpp/dsp/soft_demod.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/convolutional_codec.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_bitflip.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_decoder_neon.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/ldpc_decoder_sme2.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/viterbi_decoder_neon.cpp"
  "${ROOT_DIR}/app/src/main/cpp/fec/viterbi_decoder_sme2.cpp"
  "${ROOT_DIR}/app/src/main/cpp/trust/trust_features.cpp"
  "${ROOT_DIR}/app/src/main/cpp/trust/trust_score.cpp"
  "${ROOT_DIR}/app/src/main/cpp/util/iq_reader.cpp"
  "${ROOT_DIR}/app/src/main/cpp/util/logging.cpp"
)

build_binary() {
  local output_name="$1"
  local entry_source="$2"

  c++ -std=c++17 -O2 \
    -I"${ROOT_DIR}/app/src/main/cpp" \
    "${entry_source}" \
    "${COMMON_SOURCES[@]}" \
    -o "${BUILD_DIR}/${output_name}"
}

case "${TARGET}" in
  replay_demo)
    build_binary "replay_demo" "${ROOT_DIR}/tools/replay_demo.cpp"
    ;;
  benchmark_decoders)
    build_binary "benchmark_decoders" "${ROOT_DIR}/tools/benchmark_decoders.cpp"
    ;;
  all)
    build_binary "replay_demo" "${ROOT_DIR}/tools/replay_demo.cpp"
    build_binary "benchmark_decoders" "${ROOT_DIR}/tools/benchmark_decoders.cpp"
    ;;
  *)
    echo "error: unsupported build target '${TARGET}'" >&2
    exit 1
    ;;
esac
