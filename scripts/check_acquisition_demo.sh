#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SATCOMFEC_BUILD_DIR:-${ROOT_DIR}/build/host_replay}"
if [[ "${BUILD_DIR}" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check_acquisition_demo.sh

Builds the scalar acquisition demo and verifies all checked-in deterministic
fixtures against their ground-truth timing and CFO metadata.
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

SATCOMFEC_BUILD_DIR="${BUILD_DIR}" \
  "${ROOT_DIR}/scripts/build_host_tools.sh" acquisition_demo

for fixture_name in clean noisy frequency_offset ambiguous weak_faded; do
  iq_path="${ROOT_DIR}/data/synthetic/acquisition/${fixture_name}.iq"
  metadata_path="${ROOT_DIR}/data/synthetic/acquisition/${fixture_name}.json"
  output="$(
    "${BUILD_DIR}/acquisition_demo" \
      --iq "${iq_path}" \
      --metadata "${metadata_path}"
  )"

  printf '%s\n' "${output}"
  jq -e \
    --slurpfile metadata "${metadata_path}" \
    '.ok == true and
     .acquisition_success == true and
     .implementation == "reference" and
     .detected_timing_offset == $metadata[0].true_timing_offset and
     .detected_cfo_hz == $metadata[0].true_cfo_hz and
     .best_score > .second_best_score and
     .evaluated_candidate_count ==
       (.timing_hypothesis_count * .cfo_hypothesis_count)' \
    >/dev/null <<<"${output}"
done
