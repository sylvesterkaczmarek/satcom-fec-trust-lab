#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REQUIRE_SME2=0

if [[ "${1:-}" == "--require-sme2" ]]; then
  REQUIRE_SME2=1
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check_sme2_acquisition.sh [--require-sme2]

Runs deterministic SME2 acquisition equivalence cases. The default portable
build reports SME2 as unavailable and succeeds. --require-sme2 enables an SME2
build and fails unless the real SME2 kernel is compiled and executed.
EOF
  exit 0
elif [[ $# -gt 0 ]]; then
  echo "error: unsupported argument '$1'" >&2
  exit 1
fi

if [[ "${REQUIRE_SME2}" == "1" ]]; then
  SATCOMFEC_ENABLE_SME2=ON \
    "${ROOT_DIR}/scripts/build_host_tools.sh" check_sme2_acquisition
else
  "${ROOT_DIR}/scripts/build_host_tools.sh" check_sme2_acquisition
fi

arguments=()
if [[ "${REQUIRE_SME2}" == "1" ]]; then
  arguments+=(--require-sme2)
fi
"${ROOT_DIR}/build/host_replay/check_sme2_acquisition" "${arguments[@]}"
