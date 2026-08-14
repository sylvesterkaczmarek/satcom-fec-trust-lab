#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ANDROID_ABI="${SATCOMFEC_ANDROID_ABI:-arm64-v8a}"
OUTPUT_DIR="${ROOT_DIR}/build/android/${ANDROID_ABI}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/verify_android_benchmark_build.sh [build options]

Builds through scripts/build_android_benchmark.sh, then verifies:
  - an AArch64 Android PIE was produced;
  - libc++ is statically linked;
  - the NEON object contains vector load/deinterleave and FP arithmetic;
  - the scalar reference object does not contain that NEON kernel sequence;
  - when compiled, the SME2 object contains streaming-mode and ZA VGx4 FMLA/FMLS.

Arguments such as --sme2 auto|on|off and --ndk PATH are forwarded.
EOF
  exit 0
fi

bash "${ROOT_DIR}/scripts/build_android_benchmark.sh" "$@" >/dev/null

BUILD_INFO="${OUTPUT_DIR}/last-build.env"
if [[ ! -f "${BUILD_INFO}" ]]; then
  echo "error: Android build metadata not found: ${BUILD_INFO}" >&2
  exit 1
fi
read_build_value() {
  local key="$1"
  sed -n "s/^${key}=//p" "${BUILD_INFO}"
}

ANDROID_NDK_HOME="$(read_build_value ANDROID_NDK_HOME)"
SATCOMFEC_ANDROID_SME2_COMPILED="$(read_build_value SATCOMFEC_ANDROID_SME2_COMPILED)"
ANDROID_BENCHMARK_BINARY="$(read_build_value ANDROID_BENCHMARK_BINARY)"
ANDROID_BENCHMARK_BUILD_DIR="$(read_build_value ANDROID_BENCHMARK_BUILD_DIR)"
if [[ -z "${ANDROID_NDK_HOME}" || -z "${ANDROID_BENCHMARK_BINARY}" ||
      -z "${ANDROID_BENCHMARK_BUILD_DIR}" ]]; then
  echo "error: Android build metadata is incomplete" >&2
  exit 1
fi

find_ndk_tool() {
  local tool_name="$1"
  local candidate=""
  while IFS= read -r path; do
    candidate="${path}"
    break
  done < <(find "${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt" \
    -path "*/bin/${tool_name}" -print | sort)
  printf '%s\n' "${candidate}"
}

READELF="$(find_ndk_tool llvm-readelf)"
OBJDUMP="$(find_ndk_tool llvm-objdump)"
NM="$(find_ndk_tool llvm-nm)"
for tool in "${READELF}" "${OBJDUMP}" "${NM}"; do
  if [[ -z "${tool}" || ! -x "${tool}" ]]; then
    echo "error: required NDK LLVM inspection tool was not found" >&2
    exit 1
  fi
done

ELF_HEADER="$(${READELF} -h "${ANDROID_BENCHMARK_BINARY}")"
if ! grep -q 'Machine:.*AArch64' <<<"${ELF_HEADER}"; then
  echo "error: benchmark is not an AArch64 ELF" >&2
  exit 1
fi
if ! grep -q 'Type:.*DYN' <<<"${ELF_HEADER}"; then
  echo "error: benchmark is not an Android-compatible PIE" >&2
  exit 1
fi

DYNAMIC_SECTION="$(${READELF} -d "${ANDROID_BENCHMARK_BINARY}")"
if grep -q 'libc++_shared' <<<"${DYNAMIC_SECTION}"; then
  echo "error: benchmark unexpectedly requires libc++_shared.so" >&2
  exit 1
fi

DEFINED_SYMBOLS="$(${NM} --defined-only "${ANDROID_BENCHMARK_BINARY}")"
for symbol in acquisition_neon_kernel_compiled acquisition_sme2_kernel_compiled; do
  if ! grep -q "${symbol}" <<<"${DEFINED_SYMBOLS}"; then
    echo "error: Android benchmark is missing acquisition symbol: ${symbol}" >&2
    exit 1
  fi
done

OBJECT_ROOT="${ANDROID_BENCHMARK_BUILD_DIR}/CMakeFiles/satcom_replay_core.dir/src/acquisition"
REFERENCE_OBJECT="${OBJECT_ROOT}/acquisition_reference.cpp.o"
NEON_OBJECT="${OBJECT_ROOT}/acquisition_neon.cpp.o"
SME2_OBJECT="${OBJECT_ROOT}/acquisition_sme2.cpp.o"
for object in "${REFERENCE_OBJECT}" "${NEON_OBJECT}" "${SME2_OBJECT}"; do
  if [[ ! -f "${object}" ]]; then
    echo "error: expected Android acquisition object not found: ${object}" >&2
    exit 1
  fi
done

NEON_DISASSEMBLY="$(${OBJDUMP} -d "${NEON_OBJECT}")"
REFERENCE_DISASSEMBLY="$(${OBJDUMP} -d "${REFERENCE_OBJECT}")"
if ! grep -Eq '(ld2|uzp1|uzp2)[[:space:]]' <<<"${NEON_DISASSEMBLY}"; then
  echo "error: Android NEON object lacks vector deinterleave evidence" >&2
  exit 1
fi
if ! grep -Eq '(fmul|fmla|fmls)[[:space:]].*v[0-9]+\.' <<<"${NEON_DISASSEMBLY}"; then
  echo "error: Android NEON object lacks vector floating-point evidence" >&2
  exit 1
fi
if grep -Eq '(ld2|uzp1|uzp2)[[:space:]]' <<<"${REFERENCE_DISASSEMBLY}" &&
   grep -Eq '(fmul|fmla|fmls)[[:space:]].*v[0-9]+\.' <<<"${REFERENCE_DISASSEMBLY}"; then
  echo "error: scalar reference object contains the equivalent NEON kernel sequence" >&2
  exit 1
fi

if [[ "${SATCOMFEC_ANDROID_SME2_COMPILED}" == "ON" ]]; then
  SME2_DISASSEMBLY="$(${OBJDUMP} -d "${SME2_OBJECT}")"
  for pattern in 'smstart' 'smstop' 'fmla.*za\.s.*vgx4' 'fmls.*za\.s.*vgx4'; do
    if ! grep -Eq "${pattern}" <<<"${SME2_DISASSEMBLY}"; then
      echo "error: Android SME2 object lacks instruction pattern: ${pattern}" >&2
      exit 1
    fi
  done
fi

echo "Android ELF evidence:"
grep -E 'Type:|Machine:' <<<"${ELF_HEADER}"
echo "Android runtime libraries:"
grep 'NEEDED' <<<"${DYNAMIC_SECTION}"
echo "NEON instruction evidence:"
grep -E '(ld2|uzp1|uzp2|fmul|fmla|fmls)[[:space:]]' <<<"${NEON_DISASSEMBLY}" | sed -n '1,8p'
echo "Scalar reference contains no equivalent checked NEON sequence."

if [[ "${SATCOMFEC_ANDROID_SME2_COMPILED}" == "ON" ]]; then
  echo "SME2 instruction evidence:"
  grep -E 'smstart|smstop|fmla.*za\.s.*vgx4|fmls.*za\.s.*vgx4' \
    <<<"${SME2_DISASSEMBLY}" | sed -n '1,8p'
else
  echo "SME2 kernel was not compiled by this NDK build."
fi

echo "Android benchmark build verification passed."
