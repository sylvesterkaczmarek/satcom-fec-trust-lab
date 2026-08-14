#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_ABI="${SATCOMFEC_ANDROID_ABI:-arm64-v8a}"
OUTPUT_PATH="${ROOT_DIR}/build/android-results/acquisition-$(date -u +%Y%m%dT%H%M%SZ).json"
SERIAL="${ANDROID_SERIAL:-}"
SKIP_BUILD=0
BUILD_ARGS=()
BENCHMARK_ARGS=()

usage() {
  cat <<'EOF'
Usage: scripts/run_android_benchmark.sh [options] [-- benchmark options]

Options:
  --serial SERIAL         Select one adb device (or set ANDROID_SERIAL).
  --output PATH           Local JSON result path.
  --skip-build            Reuse build/android/arm64-v8a/benchmark_acquisition.
  --sme2 auto|on|off      Forward the Android SME2 build policy.
  --ndk PATH              Forward an explicit NDK path.
  --platform android-N    Forward the target Android API level.

Without benchmark options, the script runs the predetermined small workload
with 1 warm-up, 7 timed samples, and a 20 ms minimum sample duration. It builds,
pushes to /data/local/tmp, executes through adb, and pulls authoritative JSON.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --serial)
      [[ $# -ge 2 ]] || { echo "error: --serial requires a value" >&2; exit 2; }
      SERIAL="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || { echo "error: --output requires a path" >&2; exit 2; }
      OUTPUT_PATH="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --sme2|--ndk|--platform)
      [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
      BUILD_ARGS+=("$1" "$2")
      shift 2
      ;;
    --)
      shift
      BENCHMARK_ARGS=("$@")
      break
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument before --: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v adb >/dev/null 2>&1; then
  echo "error: adb is required; install Android SDK Platform-Tools" >&2
  exit 1
fi

if [[ "${SKIP_BUILD}" == "0" ]]; then
  bash "${ROOT_DIR}/scripts/build_android_benchmark.sh" "${BUILD_ARGS[@]}"
fi

BINARY_PATH="${ROOT_DIR}/build/android/${ANDROID_ABI}/benchmark_acquisition"
if [[ ! -f "${BINARY_PATH}" ]]; then
  echo "error: Android benchmark binary not found: ${BINARY_PATH}" >&2
  exit 1
fi

ADB=(adb)
if [[ -n "${SERIAL}" ]]; then
  ADB+=(-s "${SERIAL}")
fi

if [[ "$("${ADB[@]}" get-state 2>/dev/null || true)" != "device" ]]; then
  echo "error: no authorized adb device selected; enable developer mode and USB debugging" >&2
  adb devices -l >&2 || true
  exit 1
fi

DEVICE_ABI="$("${ADB[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')"
if [[ "${DEVICE_ABI}" != "arm64-v8a" ]]; then
  echo "error: benchmark requires arm64-v8a; selected device reports '${DEVICE_ABI}'" >&2
  exit 1
fi

if [[ ${#BENCHMARK_ARGS[@]} -eq 0 ]]; then
  BENCHMARK_ARGS=(
    --workload small
    --warmup-rounds 1
    --samples 7
    --min-sample-ms 20
  )
fi

REMOTE_DIR="/data/local/tmp/satcom-fec-trust-lab"
REMOTE_BINARY="${REMOTE_DIR}/benchmark_acquisition"
REMOTE_JSON="${REMOTE_DIR}/acquisition-result.json"
"${ADB[@]}" shell mkdir -p "${REMOTE_DIR}"
"${ADB[@]}" push "${BINARY_PATH}" "${REMOTE_BINARY}" >/dev/null
"${ADB[@]}" shell chmod 755 "${REMOTE_BINARY}"

echo "Running acquisition benchmark on ${DEVICE_ABI} device..." >&2
"${ADB[@]}" shell "${REMOTE_BINARY}" \
  "${BENCHMARK_ARGS[@]}" --json "${REMOTE_JSON}" >/dev/null

mkdir -p "$(dirname "${OUTPUT_PATH}")"
"${ADB[@]}" pull "${REMOTE_JSON}" "${OUTPUT_PATH}" >/dev/null
python3 -m json.tool "${OUTPUT_PATH}" >/dev/null

echo "Android benchmark result: ${OUTPUT_PATH}" >&2
if command -v jq >/dev/null 2>&1; then
  jq '{host, runtime_cpu_features, workloads: [.workloads[] | {name, implementations: [.implementations[] | {requested_implementation, available, correctness, modes}]}]}' "${OUTPUT_PATH}"
else
  cat "${OUTPUT_PATH}"
fi
