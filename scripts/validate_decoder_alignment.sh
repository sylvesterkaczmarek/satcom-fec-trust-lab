#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/validate_decoder_alignment.sh

Runs the benchmark harness on the canned replay frame and checks:
  - all decoder paths succeed
  - all decoder paths produce matching decoded bits
  - the benchmark assumptions report same input, settings, and evaluation window
  - branch-metric preparation timing is reported separately from full decode timing
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
echo "${OUTPUT}" | jq -e '.benchmark.local_timing_only == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_input_frame == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_decoder_settings == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_evaluation_window == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_traceback_core == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_state_machine == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.branch_metric_timed_separately == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.full_decode_includes_branch_metric_preparation == true' >/dev/null
echo "${OUTPUT}" | jq -e '.assumptions.same_prepared_soft_bits == true' >/dev/null
echo "${OUTPUT}" | jq -e '.prepared_frame.frame_length == .assumptions.coded_bits_per_frame' >/dev/null
echo "${OUTPUT}" | jq -e '.alignment.decoded_bit_count_match == true' >/dev/null
echo "${OUTPUT}" | jq -e '.alignment.decoded_bit_checksum_match == true' >/dev/null
echo "${OUTPUT}" | jq -e '.alignment.payload_text_match == true' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[0].decode_ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[1].decode_ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[2].decode_ok == true' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[0].decoder == "viterbi-neon"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[1].decoder == "viterbi-reference"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[2].decoder == "viterbi-sme2"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[0].implementation_class == "partial"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[1].implementation_class == "real"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[2].implementation_class == "partial" or .paths[2].implementation_class == "fallback"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[0].branch_metric.selected_implementation == "neon" or .paths[0].branch_metric.selected_implementation == "fallback"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[1].branch_metric.selected_implementation == "reference"' >/dev/null
echo "${OUTPUT}" | jq -e '.paths[2].branch_metric.selected_implementation == "sme2" or .paths[2].branch_metric.selected_implementation == "fallback"' >/dev/null
echo "${OUTPUT}" | jq -e 'all(.paths[]; .branch_metric.elapsed_ms >= 0 and .branch_metric.ns_per_iteration >= 0 and .branch_metric.metric_checksum > 0)' >/dev/null
echo "${OUTPUT}" | jq -e 'all(.paths[]; .full_decode.elapsed_ms >= 0 and .full_decode.ns_per_iteration >= 0)' >/dev/null
