#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEALTHY_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq"
IMPAIRED_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/compare_trust_cases.sh

Runs the healthy and impaired replay fixtures and prints a compact trust
comparison JSON object.
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

HEALTHY_OUTPUT="$(bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${HEALTHY_IQ}")"
IMPAIRED_OUTPUT="$(bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${IMPAIRED_IQ}")"

jq -n \
  --argjson healthy "${HEALTHY_OUTPUT}" \
  --argjson impaired "${IMPAIRED_OUTPUT}" \
  '{
    healthy: {
      iq_path: $healthy.iq_path,
      trust_score: $healthy.trust_score,
      trust_band: $healthy.trust_assessment.band,
      weak_llr_fraction: $healthy.trust_features.weak_llr_fraction,
      mean_abs_llr: $healthy.trust_features.mean_abs_llr,
      decoded_text: $healthy.decoded_text
    },
    impaired: {
      iq_path: $impaired.iq_path,
      trust_score: $impaired.trust_score,
      trust_band: $impaired.trust_assessment.band,
      weak_llr_fraction: $impaired.trust_features.weak_llr_fraction,
      mean_abs_llr: $impaired.trust_features.mean_abs_llr,
      decoded_text: $impaired.decoded_text
    },
    comparison: {
      same_payload: ($healthy.decoded_text == $impaired.decoded_text),
      impaired_score_delta: ($healthy.trust_score - $impaired.trust_score),
      impaired_has_more_weak_bits:
        ($impaired.trust_features.weak_llr_fraction >
         $healthy.trust_features.weak_llr_fraction),
      impaired_has_lower_llr_strength:
        ($impaired.trust_features.mean_abs_llr < $healthy.trust_features.mean_abs_llr)
    }
  }'
