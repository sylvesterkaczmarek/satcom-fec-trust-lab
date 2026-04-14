#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
TARGET="${1:-all}"

if [[ "${TARGET}" == "--help" || "${TARGET}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/build_host_tools.sh [replay_demo|benchmark_decoders|all]

Configures and builds the supported host-side executables into build/host_replay/.
EOF
  exit 0
fi

mkdir -p "${BUILD_DIR}"

build_with_compiler() {
  if ! command -v c++ >/dev/null 2>&1; then
    echo "error: neither cmake nor c++ is available in PATH" >&2
    exit 1
  fi

  local common_sources=(
    "${ROOT_DIR}/src/demo/replay_pipeline.cpp"
    "${ROOT_DIR}/src/dsp/front_end_dsp.cpp"
    "${ROOT_DIR}/src/dsp/framing.cpp"
    "${ROOT_DIR}/src/dsp/soft_demod.cpp"
    "${ROOT_DIR}/src/fec/convolutional_codec.cpp"
    "${ROOT_DIR}/src/fec/ldpc_bitflip.cpp"
    "${ROOT_DIR}/src/fec/ldpc_decoder_neon.cpp"
    "${ROOT_DIR}/src/fec/ldpc_decoder_sme2.cpp"
    "${ROOT_DIR}/src/fec/viterbi_decoder_neon.cpp"
    "${ROOT_DIR}/src/fec/viterbi_decoder_sme2.cpp"
    "${ROOT_DIR}/src/trust/trust_features.cpp"
    "${ROOT_DIR}/src/trust/trust_score.cpp"
    "${ROOT_DIR}/src/util/iq_reader.cpp"
    "${ROOT_DIR}/src/util/logging.cpp"
  )

  build_binary() {
    local output_name="$1"
    local entry_source="$2"
    c++ -std=c++17 -O2 \
      -I"${ROOT_DIR}/src" \
      "${entry_source}" \
      "${common_sources[@]}" \
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
}

if ! command -v cmake >/dev/null 2>&1; then
  build_with_compiler
  exit 0
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release >/dev/null

case "${TARGET}" in
  replay_demo)
    cmake --build "${BUILD_DIR}" --target replay_demo >/dev/null
    ;;
  benchmark_decoders)
    cmake --build "${BUILD_DIR}" --target benchmark_decoders >/dev/null
    ;;
  all)
    cmake --build "${BUILD_DIR}" --target replay_demo benchmark_decoders >/dev/null
    ;;
  *)
    echo "error: unsupported build target '${TARGET}'" >&2
    exit 1
    ;;
esac
