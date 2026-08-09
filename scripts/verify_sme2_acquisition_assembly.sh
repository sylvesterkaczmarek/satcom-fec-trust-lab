#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/host_replay"
CXX_BIN="${CXX:-c++}"
ARCHITECTURE="$(uname -m)"

echo "Compiler:"
"${CXX_BIN}" --version
echo
echo "Architecture: ${ARCHITECTURE}"

case "${ARCHITECTURE}" in
  arm64|aarch64)
    echo
    echo "Building the explicit SME2 path:"
    SATCOMFEC_ENABLE_SME2=ON \
      bash "${ROOT_DIR}/scripts/build_host_tools.sh" all
    ;;
  *)
    echo
    echo "Native SME2 execution is unavailable on ${ARCHITECTURE}."
    echo "Checking portable unavailable reporting instead."
    SATCOMFEC_ENABLE_SME2=OFF \
      bash "${ROOT_DIR}/scripts/build_host_tools.sh" check_sme2_acquisition
    "${BUILD_DIR}/check_sme2_acquisition"
    exit 0
    ;;
esac

echo
echo "Running SME2 equivalence with execution required:"
"${BUILD_DIR}/check_sme2_acquisition" --require-sme2

if command -v cmake >/dev/null 2>&1; then
  object_path="${BUILD_DIR}/CMakeFiles/satcom_replay_core.dir/src/acquisition/acquisition_sme2.cpp.o"
  reference_object_path="${BUILD_DIR}/CMakeFiles/satcom_replay_core.dir/src/acquisition/acquisition_reference.cpp.o"
else
  object_path="${BUILD_DIR}/direct_objects/acquisition_sme2.o"
  reference_object_path="${BUILD_DIR}/direct_objects/acquisition_reference.o"
fi
if [[ ! -f "${object_path}" ]]; then
  echo "error: SME2 acquisition object was not found under ${BUILD_DIR}" >&2
  exit 1
fi
if [[ ! -f "${reference_object_path}" ]]; then
  echo "error: scalar acquisition reference object was not found" >&2
  exit 1
fi

echo
echo "SME2 object: ${object_path}"
if command -v nm >/dev/null 2>&1; then
  echo "SME2 symbols:"
  nm "${object_path}" | grep -E \
    'correlate_sme2_kernel|run_sme2_acquisition|acquisition_sme2_kernel_compiled'
fi

assembly_file="${BUILD_DIR}/acquisition_sme2.disassembly.txt"
reference_assembly_file="${BUILD_DIR}/acquisition_reference_sme2_check.disassembly.txt"
if command -v llvm-objdump >/dev/null 2>&1; then
  llvm-objdump --disassemble --no-show-raw-insn "${object_path}" >"${assembly_file}"
  llvm-objdump --disassemble --no-show-raw-insn \
    "${reference_object_path}" >"${reference_assembly_file}"
elif command -v xcrun >/dev/null 2>&1 && \
     xcrun --find llvm-objdump >/dev/null 2>&1; then
  xcrun llvm-objdump --disassemble --no-show-raw-insn \
    "${object_path}" >"${assembly_file}"
  xcrun llvm-objdump --disassemble --no-show-raw-insn \
    "${reference_object_path}" >"${reference_assembly_file}"
elif command -v objdump >/dev/null 2>&1; then
  objdump -d "${object_path}" >"${assembly_file}"
  objdump -d "${reference_object_path}" >"${reference_assembly_file}"
elif command -v otool >/dev/null 2>&1; then
  otool -tvV "${object_path}" >"${assembly_file}"
  otool -tvV "${reference_object_path}" >"${reference_assembly_file}"
else
  echo "error: llvm-objdump, objdump, or otool is required for instruction verification" >&2
  exit 1
fi

require_instruction() {
  local description="$1"
  local pattern="$2"
  if ! grep -Eiq "${pattern}" "${assembly_file}"; then
    echo "error: expected ${description} was not found in the SME2 object" >&2
    exit 1
  fi
}

require_instruction "streaming-mode entry" 'smstart'
require_instruction "streaming-mode exit" 'smstop'
require_instruction "SME2 VGx4 ZA FMLA" 'fmla.*za\.s.*vgx4'
require_instruction "SME2 VGx4 ZA FMLS" 'fmls.*za\.s.*vgx4'
require_instruction "VGx4 transfer into ZA" 'mov.*za\.[ds].*vgx4'
require_instruction "VGx4 transfer from ZA" 'mov.*\{[^}]*z[0-9]+[^}]*\}.*za\.[ds].*vgx4'

if grep -Eiq '(fmla|fmls).*za\.[sd].*vgx4' \
  "${reference_assembly_file}"; then
  echo "error: scalar reference object contains SME2 ZA VGx4 instructions" >&2
  exit 1
fi

echo
echo "Verified SME2 instruction evidence:"
grep -m 20 -Ei \
  'smstart|smstop|(fmla|fmls).*za\.[sd].*vgx4|mov.*(za\.[ds].*vgx4|\{[^}]*z[0-9]+[^}]*\}.*za\.[ds])' \
  "${assembly_file}"

echo
echo "Scalar reference object contains none of the checked SME2 ZA VGx4 patterns."
