#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERIFY_ROOT="${ROOT_DIR}/build/verify"
PORTABLE_BUILD="${VERIFY_ROOT}/portable"
SANITIZER_BUILD="${VERIFY_ROOT}/sanitizers"
WORKFLOW_BUILD="${VERIFY_ROOT}/workflow"

require_command() {
  local command_name="$1"
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "error: required command not found: ${command_name}" >&2
    exit 1
  fi
}

for command_name in cmake python3 jq; do
  require_command "${command_name}"
done

echo "== Fixture integrity =="
python3 "${ROOT_DIR}/scripts/update_fixture_checksums.py" --check

echo
echo "== Clean portable strict-warning build =="
cmake -E remove_directory "${VERIFY_ROOT}"
cmake -S "${ROOT_DIR}" -B "${PORTABLE_BUILD}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSATCOMFEC_ENABLE_NEON=OFF \
  -DSATCOMFEC_ENABLE_SME2=OFF \
  -DSATCOMFEC_ENABLE_WARNINGS=ON \
  -DSATCOMFEC_WARNINGS_AS_ERRORS=ON
cmake --build "${PORTABLE_BUILD}" --parallel
python3 "${ROOT_DIR}/scripts/check_compile_commands.py" \
  --build-dir "${PORTABLE_BUILD}" \
  --expect portable
ctest --test-dir "${PORTABLE_BUILD}" --output-on-failure

echo
echo "== Supported replay, trust, acquisition, and FEC scripts =="
export SATCOMFEC_BUILD_DIR="${WORKFLOW_BUILD}"
bash "${ROOT_DIR}/scripts/check_replay_demo.sh"
bash "${ROOT_DIR}/scripts/compare_trust_cases.sh"
bash "${ROOT_DIR}/scripts/check_acquisition_demo.sh"
bash "${ROOT_DIR}/scripts/check_acquisition_neon.sh"
bash "${ROOT_DIR}/scripts/check_sme2_acquisition.sh"
bash "${ROOT_DIR}/scripts/validate_decoder_alignment.sh"
bash "${ROOT_DIR}/scripts/check_branch_metrics.sh"

echo
echo "== Python regression suite =="
SATCOMFEC_TEST_BUILD_DIR="${WORKFLOW_BUILD}" \
  python3 -m unittest discover -s "${ROOT_DIR}/tests" -v

echo
echo "== Portable ASan/UBSan build =="
cmake -S "${ROOT_DIR}" -B "${SANITIZER_BUILD}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSATCOMFEC_ENABLE_NEON=OFF \
  -DSATCOMFEC_ENABLE_SME2=OFF \
  -DSATCOMFEC_ENABLE_WARNINGS=ON \
  -DSATCOMFEC_WARNINGS_AS_ERRORS=ON \
  -DSATCOMFEC_ENABLE_SANITIZERS=ON
cmake --build "${SANITIZER_BUILD}" --parallel
ctest --test-dir "${SANITIZER_BUILD}" --output-on-failure

echo
echo "== Architecture-specific build probes =="
bash "${ROOT_DIR}/scripts/verify_arm_paths.sh"

echo
echo "Public correctness verification passed."
