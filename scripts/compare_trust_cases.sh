#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEALTHY_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq"
IMPAIRED_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq"
FAILED_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_failed.iq"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/compare_trust_cases.sh

Runs the healthy, impaired, and failed replay fixtures and prints a compact trust
comparison JSON object.
EOF
  exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq not found in PATH" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

HEALTHY_JSON="${TMP_DIR}/healthy.json"
IMPAIRED_JSON="${TMP_DIR}/impaired.json"
FAILED_JSON="${TMP_DIR}/failed.json"

bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${HEALTHY_IQ}" >"${HEALTHY_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${IMPAIRED_IQ}" >"${IMPAIRED_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" --allow-failure "${FAILED_IQ}" >"${FAILED_JSON}"

jq -n \
  --slurpfile healthy "${HEALTHY_JSON}" \
  --slurpfile impaired "${IMPAIRED_JSON}" \
  --slurpfile failed "${FAILED_JSON}" \
  '{
    healthy: ($healthy[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_llr_fraction: .trust_features.weak_llr_fraction,
      mean_abs_llr: .trust_features.mean_abs_llr,
      decoded_text
    }),
    impaired: ($impaired[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_llr_fraction: .trust_features.weak_llr_fraction,
      mean_abs_llr: .trust_features.mean_abs_llr,
      decoded_text
    }),
    failed: ($failed[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_llr_fraction: .trust_features.weak_llr_fraction,
      mean_abs_llr: .trust_features.mean_abs_llr,
      error
    }),
    comparison: {
      healthy_impaired_same_payload: ($healthy[0].decoded_text == $impaired[0].decoded_text),
      trust_score_order_ok:
        ($healthy[0].trust_score > $impaired[0].trust_score and
         $impaired[0].trust_score > $failed[0].trust_score),
      impaired_score_delta: ($healthy[0].trust_score - $impaired[0].trust_score),
      failed_score_delta: ($healthy[0].trust_score - $failed[0].trust_score),
      impaired_has_more_weak_bits:
        ($impaired[0].trust_features.weak_llr_fraction >
         $healthy[0].trust_features.weak_llr_fraction),
      impaired_has_lower_llr_strength:
        ($impaired[0].trust_features.mean_abs_llr < $healthy[0].trust_features.mean_abs_llr),
      failed_crc_rejected:
        ($failed[0].crc_ok == false and $failed[0].trust_assessment.crc_failed == true),
      trust_band_progression: [
        $healthy[0].trust_assessment.band,
        $impaired[0].trust_assessment.band,
        $failed[0].trust_assessment.band
      ]
    }
  }'
