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
  --output PATH           New local JSON result path; existing files are preserved.
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
if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 is required to validate the benchmark report" >&2
  exit 1
fi
if [[ -e "${OUTPUT_PATH}" || -L "${OUTPUT_PATH}" ]]; then
  echo "error: output already exists; choose a new path: ${OUTPUT_PATH}" >&2
  exit 1
fi
if [[ "${ANDROID_ABI}" != "arm64-v8a" ]]; then
  echo "error: only arm64-v8a is supported; got '${ANDROID_ABI}'" >&2
  exit 2
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

REMOTE_DIR=""
LOCAL_JSON=""
cleanup() {
  local status=$?
  if [[ -n "${LOCAL_JSON}" ]]; then
    rm -f -- "${LOCAL_JSON}"
  fi
  if [[ -n "${REMOTE_DIR}" ]]; then
    "${ADB[@]}" shell rm -rf "${REMOTE_DIR}" >/dev/null 2>&1 || true
  fi
  return "${status}"
}
trap cleanup EXIT

# adb shell joins its arguments into a remote shell command. Preserve each
# benchmark argument literally, including spaces, quotes and shell metacharacters.
quote_remote_argument() {
  local argument="$1"
  argument="${argument//\'/\'\\\'\'}"
  printf "'%s'" "${argument}"
}

created_directory="$("${ADB[@]}" shell mktemp -d /data/local/tmp/satcom-fec-trust-lab.XXXXXX | tr -d '\r')"
if [[ ! "${created_directory}" =~ ^/data/local/tmp/satcom-fec-trust-lab\.[A-Za-z0-9]+$ ]]; then
  echo "error: adb did not create an isolated benchmark directory" >&2
  exit 1
fi
REMOTE_DIR="${created_directory}"
REMOTE_BINARY="${REMOTE_DIR}/benchmark_acquisition"
REMOTE_JSON="${REMOTE_DIR}/acquisition-result.json"
"${ADB[@]}" push "${BINARY_PATH}" "${REMOTE_BINARY}" >/dev/null
"${ADB[@]}" shell chmod 755 "${REMOTE_BINARY}"

echo "Running acquisition benchmark on ${DEVICE_ABI} device..." >&2
REMOTE_COMMAND="$(quote_remote_argument "${REMOTE_BINARY}")"
for argument in "${BENCHMARK_ARGS[@]}" --json "${REMOTE_JSON}"; do
  REMOTE_COMMAND+=" $(quote_remote_argument "${argument}")"
done
"${ADB[@]}" shell "${REMOTE_COMMAND}" >/dev/null

mkdir -p "$(dirname "${OUTPUT_PATH}")"
LOCAL_JSON="$(mktemp "${OUTPUT_PATH}.tmp.XXXXXX")"
"${ADB[@]}" pull "${REMOTE_JSON}" "${LOCAL_JSON}" >/dev/null
python3 - "${LOCAL_JSON}" "${OUTPUT_PATH}" <<'PY'
import json
import os
import sys

def reject_constant(value):
    raise ValueError(f"benchmark JSON contains a non-finite number: {value}")

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        report = json.load(source, parse_constant=reject_constant)
    if (
        not isinstance(report, dict)
        or report.get("ok") is not True
        or not isinstance(report.get("benchmark"), dict)
        or report["benchmark"].get("name") != "acquisition-workload-sweep"
        or not isinstance(report.get("workloads"), list)
        or not report["workloads"]
    ):
        raise ValueError("expected a successful acquisition benchmark report")
    # Both paths share a directory. Linking publishes the exact pulled bytes
    # atomically and fails if another run has already claimed the output path.
    os.link(sys.argv[1], sys.argv[2])
except (OSError, UnicodeError, ValueError) as error:
    print(f"error: {error}", file=sys.stderr)
    sys.exit(1)
PY

echo "Android benchmark result: ${OUTPUT_PATH}" >&2
if command -v jq >/dev/null 2>&1; then
  jq '{host, runtime_cpu_features, workloads: [.workloads[] | {name, implementations: [.implementations[] | {requested_implementation, available, correctness, modes}]}]}' "${OUTPUT_PATH}"
else
  cat "${OUTPUT_PATH}"
fi
