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

bash "${ROOT_DIR}/scripts/build_android_benchmark.sh" "$@"

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

compile_mode="neon"
if [[ "${SATCOMFEC_ANDROID_SME2_COMPILED}" == "ON" ]]; then
  compile_mode="sme2"
fi
python3 "${ROOT_DIR}/scripts/check_compile_commands.py" \
  --build-dir "${ANDROID_BENCHMARK_BUILD_DIR}" \
  --expect "${compile_mode}"

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
SME2_CONTROL_OBJECT="${OBJECT_ROOT}/acquisition_sme2.cpp.o"
SME2_KERNEL_OBJECT="${OBJECT_ROOT}/acquisition_sme2_kernel.cpp.o"
for object in \
  "${REFERENCE_OBJECT}" \
  "${NEON_OBJECT}" \
  "${SME2_CONTROL_OBJECT}" \
  "${SME2_KERNEL_OBJECT}"; do
  if [[ ! -f "${object}" ]]; then
    echo "error: expected Android acquisition object not found: ${object}" >&2
    exit 1
  fi
done

NEON_DISASSEMBLY_FILE="${OUTPUT_DIR}/acquisition_neon.disassembly.txt"
REFERENCE_DISASSEMBLY_FILE="${OUTPUT_DIR}/acquisition_reference.disassembly.txt"
"${OBJDUMP}" -d "${NEON_OBJECT}" >"${NEON_DISASSEMBLY_FILE}"
"${OBJDUMP}" -d "${REFERENCE_OBJECT}" >"${REFERENCE_DISASSEMBLY_FILE}"
bash "${ROOT_DIR}/scripts/check_neon_disassembly.sh" \
  "${NEON_DISASSEMBLY_FILE}" "${REFERENCE_DISASSEMBLY_FILE}"

if [[ "${SATCOMFEC_ANDROID_SME2_COMPILED}" == "ON" ]]; then
  SME2_DISASSEMBLY_FILE="${OUTPUT_DIR}/acquisition_sme2.disassembly.txt"
  SME2_CONTROL_DISASSEMBLY_FILE="${OUTPUT_DIR}/acquisition_sme2_control.disassembly.txt"
  "${OBJDUMP}" -d "${SME2_KERNEL_OBJECT}" >"${SME2_DISASSEMBLY_FILE}"
  "${OBJDUMP}" -d "${SME2_CONTROL_OBJECT}" >"${SME2_CONTROL_DISASSEMBLY_FILE}"
  for pattern in 'smstart' 'smstop' 'fmla.*za\.s.*vgx4' 'fmls.*za\.s.*vgx4'; do
    if ! grep -Eq "${pattern}" "${SME2_DISASSEMBLY_FILE}"; then
      echo "error: Android SME2 object lacks instruction pattern: ${pattern}" >&2
      exit 1
    fi
  done
  if grep -Eiq '(^|[[:space:]])ld2w[[:space:]]' "${SME2_DISASSEMBLY_FILE}"; then
    SME2_INPUT_LOAD_EVIDENCE='predicated SVE LD2W deinterleaving load'
  elif grep -Eiq '(^|[[:space:]])ld1w[[:space:]]' "${SME2_DISASSEMBLY_FILE}" &&
       grep -Eiq '(^|[[:space:]])uzp[12][[:space:]]' "${SME2_DISASSEMBLY_FILE}"; then
    SME2_INPUT_LOAD_EVIDENCE='predicated SVE loads plus explicit UZP deinterleaving'
  else
    echo "error: Android SME2 object lacks interleaved-IQ vector load evidence" >&2
    exit 1
  fi
  if grep -Eiq \
    'smstart|smstop|(^|[[:space:],{])z[0-9]+[.][bhsd]|(^|[[:space:]])(ptrue|whilel[ot]|ld[1-4][bhwd]|st[1-4][bhwd])[[:space:]]|(fmla|fmls).*za\.[sd]' \
    "${SME2_CONTROL_DISASSEMBLY_FILE}"; then
    echo "error: Android SME2 control/workspace object contains SVE/SME instructions" >&2
    exit 1
  fi
fi

echo "Android ELF evidence:"
grep -E 'Type:|Machine:' <<<"${ELF_HEADER}"
echo "Android runtime libraries:"
grep 'NEEDED' <<<"${DYNAMIC_SECTION}"

if [[ "${SATCOMFEC_ANDROID_SME2_COMPILED}" == "ON" ]]; then
  echo "SME2 instruction evidence:"
  grep -E 'smstart|smstop|fmla.*za\.s.*vgx4|fmls.*za\.s.*vgx4' \
    "${SME2_DISASSEMBLY_FILE}" | sed -n '1,8p'
  echo "SME2 input load/deinterleave: ${SME2_INPUT_LOAD_EVIDENCE}"
  echo "Generic SME2 control/workspace object contains no checked SVE/SME instructions."
else
  echo "SME2 kernel was not compiled by this NDK build."
fi

echo "Android benchmark build verification passed."
