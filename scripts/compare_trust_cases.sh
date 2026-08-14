#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEALTHY_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk.iq"
IMPAIRED_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq"
AMBIGUOUS_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq"
FAILED_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_failed.iq"
NO_SIGNAL_IQ="${ROOT_DIR}/data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/compare_trust_cases.sh

Runs the healthy, impaired, ambiguous, CRC-failed, and noise-only replay
fixtures and prints a compact trust comparison JSON object.
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
AMBIGUOUS_JSON="${TMP_DIR}/ambiguous.json"
FAILED_JSON="${TMP_DIR}/failed.json"
NO_SIGNAL_JSON="${TMP_DIR}/no-signal.json"

bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${HEALTHY_IQ}" >"${HEALTHY_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${IMPAIRED_IQ}" >"${IMPAIRED_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" "${AMBIGUOUS_IQ}" >"${AMBIGUOUS_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" --allow-failure "${FAILED_IQ}" >"${FAILED_JSON}"
bash "${ROOT_DIR}/scripts/run_replay_demo.sh" --allow-failure "${NO_SIGNAL_IQ}" >"${NO_SIGNAL_JSON}"

jq -n \
  --slurpfile healthy "${HEALTHY_JSON}" \
  --slurpfile impaired "${IMPAIRED_JSON}" \
  --slurpfile ambiguous "${AMBIGUOUS_JSON}" \
  --slurpfile failed "${FAILED_JSON}" \
  --slurpfile no_signal "${NO_SIGNAL_JSON}" \
  '{
    healthy: ($healthy[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_soft_decision_fraction: .trust_features.weak_soft_decision_fraction,
      mean_abs_soft_decision: .trust_features.mean_abs_soft_decision,
      acquisition_confidence: .acquisition.confidence,
      acquisition_peak_separation: .acquisition.normalized_peak_separation,
      decoded_text
    }),
    impaired: ($impaired[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_soft_decision_fraction: .trust_features.weak_soft_decision_fraction,
      mean_abs_soft_decision: .trust_features.mean_abs_soft_decision,
      acquisition_confidence: .acquisition.confidence,
      acquisition_peak_separation: .acquisition.normalized_peak_separation,
      decoded_text
    }),
    ambiguous: ($ambiguous[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      ambiguous_acquisition: .trust_assessment.ambiguous_acquisition,
      acquisition_confidence: .acquisition.confidence,
      acquisition_peak_separation: .acquisition.normalized_peak_separation,
      decoded_text
    }),
    failed: ($failed[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      weak_soft_decision_fraction: .trust_features.weak_soft_decision_fraction,
      mean_abs_soft_decision: .trust_features.mean_abs_soft_decision,
      acquisition_success: .acquisition.acquisition_success,
      crc_failed: .trust_assessment.crc_failed,
      error
    }),
    no_signal: ($no_signal[0] | {
      iq_path,
      ok,
      crc_ok,
      trust_score,
      trust_band: .trust_assessment.band,
      acquisition_success: .acquisition.acquisition_success,
      normalized_acquisition_peak: .acquisition.normalized_peak,
      acquisition_rejected: .trust_assessment.acquisition_rejected,
      crc_not_evaluated: .trust_assessment.crc_not_evaluated,
      demod_symbol_count: .demod.symbol_count,
      error
    }),
    comparison: {
      healthy_impaired_same_payload: ($healthy[0].decoded_text == $impaired[0].decoded_text),
      trust_score_order_ok:
        ($healthy[0].trust_score > $impaired[0].trust_score and
         $impaired[0].trust_score > $ambiguous[0].trust_score and
         $ambiguous[0].trust_score > $failed[0].trust_score and
         $failed[0].trust_score > $no_signal[0].trust_score),
      acquisition_confidence_order_ok:
        ($healthy[0].acquisition.confidence > $impaired[0].acquisition.confidence and
         $impaired[0].acquisition.confidence > $ambiguous[0].acquisition.confidence and
         $ambiguous[0].acquisition.confidence > $no_signal[0].acquisition.confidence),
      impaired_score_delta: ($healthy[0].trust_score - $impaired[0].trust_score),
      ambiguous_score_delta: ($healthy[0].trust_score - $ambiguous[0].trust_score),
      failed_score_delta: ($healthy[0].trust_score - $failed[0].trust_score),
      impaired_has_more_weak_soft_decisions:
        ($impaired[0].trust_features.weak_soft_decision_fraction >
         $healthy[0].trust_features.weak_soft_decision_fraction),
      impaired_has_lower_soft_decision_strength:
        ($impaired[0].trust_features.mean_abs_soft_decision <
         $healthy[0].trust_features.mean_abs_soft_decision),
      failed_crc_rejected:
        ($failed[0].crc_ok == false and $failed[0].trust_assessment.crc_failed == true),
      ambiguous_peak_detected:
        ($ambiguous[0].trust_assessment.ambiguous_acquisition == true and
         $ambiguous[0].acquisition.acquisition_success == true),
      no_signal_rejected_before_demod:
        ($no_signal[0].acquisition.acquisition_success == false and
         $no_signal[0].demod.symbol_count == 0 and
         $no_signal[0].trust_assessment.crc_not_evaluated == true),
      trust_band_progression: [
        $healthy[0].trust_assessment.band,
        $impaired[0].trust_assessment.band,
        $ambiguous[0].trust_assessment.band,
        $failed[0].trust_assessment.band,
        $no_signal[0].trust_assessment.band
      ]
    }
  }'
