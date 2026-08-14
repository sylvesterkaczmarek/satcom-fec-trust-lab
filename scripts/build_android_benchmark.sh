#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_ABI="${SATCOMFEC_ANDROID_ABI:-arm64-v8a}"
ANDROID_PLATFORM="${SATCOMFEC_ANDROID_PLATFORM:-android-28}"
SME2_MODE="${SATCOMFEC_ANDROID_SME2:-auto}"
OUTPUT_DIR="${ROOT_DIR}/build/android/${ANDROID_ABI}"

usage() {
  cat <<'EOF'
Usage: scripts/build_android_benchmark.sh [--sme2 auto|on|off]
       [--ndk PATH] [--platform android-N] [--output-dir PATH]

Builds a statically linked libc++ arm64-v8a command-line acquisition benchmark
with the Android NDK. Only the SME2 translation unit receives an SME2 target
flag. Modes:

  auto  Try an SME2-enabled build, then build reference/NEON only if the NDK
        compiler does not support the required SME2 ACLE interface.
  on    Require the real SME2 kernel to compile; fail otherwise.
  off   Build the reference and NEON paths only.

NDK discovery order: --ndk, ANDROID_NDK_HOME, ANDROID_NDK_ROOT, then the newest
directory under $ANDROID_SDK_ROOT/ndk or $ANDROID_HOME/ndk. GitHub's
ANDROID_NDK_LATEST_HOME is also recognized.
EOF
}

NDK_PATH=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --sme2)
      [[ $# -ge 2 ]] || { echo "error: --sme2 requires auto, on, or off" >&2; exit 2; }
      SME2_MODE="$2"
      shift 2
      ;;
    --ndk)
      [[ $# -ge 2 ]] || { echo "error: --ndk requires a path" >&2; exit 2; }
      NDK_PATH="$2"
      shift 2
      ;;
    --platform)
      [[ $# -ge 2 ]] || { echo "error: --platform requires android-N" >&2; exit 2; }
      ANDROID_PLATFORM="$2"
      shift 2
      ;;
    --output-dir)
      [[ $# -ge 2 ]] || { echo "error: --output-dir requires a path" >&2; exit 2; }
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "${SME2_MODE}" in
  auto|on|off) ;;
  *)
    echo "error: SME2 mode must be auto, on, or off; got '${SME2_MODE}'" >&2
    exit 2
    ;;
esac

if [[ "${ANDROID_ABI}" != "arm64-v8a" ]]; then
  echo "error: only arm64-v8a is supported; got '${ANDROID_ABI}'" >&2
  exit 2
fi
if [[ ! "${ANDROID_PLATFORM}" =~ ^android-[0-9]+$ ]]; then
  echo "error: Android platform must have the form android-N" >&2
  exit 2
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake is required" >&2
  exit 1
fi

discover_ndk() {
  if [[ -n "${NDK_PATH}" ]]; then
    printf '%s\n' "${NDK_PATH}"
    return
  fi
  if [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
    printf '%s\n' "${ANDROID_NDK_HOME}"
    return
  fi
  if [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
    printf '%s\n' "${ANDROID_NDK_ROOT}"
    return
  fi
  if [[ -n "${ANDROID_NDK_LATEST_HOME:-}" ]]; then
    printf '%s\n' "${ANDROID_NDK_LATEST_HOME}"
    return
  fi

  local sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
  if [[ -n "${sdk_root}" && -d "${sdk_root}/ndk" ]]; then
    local candidate=""
    while IFS= read -r path; do
      candidate="${path}"
    done < <(find "${sdk_root}/ndk" -mindepth 1 -maxdepth 1 -type d -print | sort)
    if [[ -n "${candidate}" ]]; then
      printf '%s\n' "${candidate}"
      return
    fi
  fi
}

NDK_PATH="$(discover_ndk)"
if [[ -z "${NDK_PATH}" ]]; then
  echo "error: Android NDK not found; set ANDROID_NDK_HOME or pass --ndk" >&2
  exit 1
fi

TOOLCHAIN_FILE="${NDK_PATH}/build/cmake/android.toolchain.cmake"
if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
  echo "error: NDK CMake toolchain not found: ${TOOLCHAIN_FILE}" >&2
  exit 1
fi

configure_android() {
  local build_dir="$1"
  local enable_sme2="$2"
  cmake -S "${ROOT_DIR}" -B "${build_dir}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DANDROID_ABI="${ANDROID_ABI}" \
    -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=-Wall -Wextra -Wpedantic -Werror" \
    -DBUILD_TESTING=OFF \
    -DSATCOMFEC_ANDROID_BENCHMARK_ONLY=ON \
    -DSATCOMFEC_ENABLE_NEON=ON \
    -DSATCOMFEC_ENABLE_SME2="${enable_sme2}"
}

mkdir -p "${OUTPUT_DIR}"
SME2_COMPILED="OFF"
BUILD_DIR="${OUTPUT_DIR}/baseline"

if [[ "${SME2_MODE}" != "off" ]]; then
  SME2_BUILD_DIR="${OUTPUT_DIR}/sme2"
  SME2_CONFIGURE_LOG="${OUTPUT_DIR}/sme2-configure.log"
  if configure_android "${SME2_BUILD_DIR}" ON >"${SME2_CONFIGURE_LOG}" 2>&1; then
    BUILD_DIR="${SME2_BUILD_DIR}"
    SME2_COMPILED="ON"
    cat "${SME2_CONFIGURE_LOG}" >&2
  elif [[ "${SME2_MODE}" == "on" ]]; then
    cat "${SME2_CONFIGURE_LOG}" >&2
    echo "error: the selected NDK cannot configure the required SME2 kernel" >&2
    exit 1
  else
    cat "${SME2_CONFIGURE_LOG}" >&2
    if grep -Eq \
      'requires a compiler that accepts|requires ACLE SME2 support|requires a NEON-only comparison' \
      "${SME2_CONFIGURE_LOG}"; then
      echo "warning: SME2 ACLE build unavailable; producing reference/NEON binary" >&2
    else
      echo "error: SME2 configuration failed for a reason unrelated to feature support" >&2
      exit 1
    fi
  fi
fi

if [[ "${SME2_COMPILED}" == "OFF" ]]; then
  configure_android "${BUILD_DIR}" OFF
fi

cmake --build "${BUILD_DIR}" --target benchmark_acquisition --parallel

BUILT_BINARY="${BUILD_DIR}/benchmark_acquisition"
OUTPUT_BINARY="${OUTPUT_DIR}/benchmark_acquisition"
if [[ ! -f "${BUILT_BINARY}" ]]; then
  echo "error: Android benchmark binary was not produced: ${BUILT_BINARY}" >&2
  exit 1
fi
cp "${BUILT_BINARY}" "${OUTPUT_BINARY}"
chmod +x "${OUTPUT_BINARY}"

cat >"${OUTPUT_DIR}/last-build.env" <<EOF
ANDROID_ABI=${ANDROID_ABI}
ANDROID_PLATFORM=${ANDROID_PLATFORM}
ANDROID_NDK_HOME=${NDK_PATH}
SATCOMFEC_ANDROID_SME2_COMPILED=${SME2_COMPILED}
ANDROID_BENCHMARK_BINARY=${OUTPUT_BINARY}
ANDROID_BENCHMARK_BUILD_DIR=${BUILD_DIR}
EOF

echo "Android benchmark built: ${OUTPUT_BINARY}" >&2
echo "SME2 kernel compiled: ${SME2_COMPILED}" >&2
printf '%s\n' "${OUTPUT_BINARY}"
