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
    echo "Building explicit AArch64 NEON path:"
    SATCOMFEC_ENABLE_NEON=ON \
      bash "${ROOT_DIR}/scripts/build_host_tools.sh" all

    echo
    echo "Running NEON kernel equivalence with execution required:"
    bash "${ROOT_DIR}/scripts/check_acquisition_neon.sh" --require-neon
    ;;
  *)
    echo
    echo "Native NEON execution is unavailable on ${ARCHITECTURE}."
    echo "Building the portable path and checking explicit unavailable reporting:"
    SATCOMFEC_ENABLE_NEON=OFF \
      bash "${ROOT_DIR}/scripts/build_host_tools.sh" all
    bash "${ROOT_DIR}/scripts/check_acquisition_neon.sh"
    exit 0
    ;;
esac

object_path="$(
  find "${BUILD_DIR}" -type f \
    \( -name 'acquisition_neon.cpp.o' -o -name 'acquisition_neon.o' \) \
    -print -quit
)"
if [[ -z "${object_path}" ]]; then
  echo "error: acquisition NEON object was not found under ${BUILD_DIR}" >&2
  exit 1
fi

reference_object_path="$(
  find "${BUILD_DIR}" -type f \
    \( -name 'acquisition_reference.cpp.o' -o -name 'acquisition_reference.o' \) \
    -print -quit
)"
if [[ -z "${reference_object_path}" ]]; then
  echo "error: scalar acquisition reference object was not found" >&2
  exit 1
fi

echo
echo "NEON object: ${object_path}"
if command -v nm >/dev/null 2>&1; then
  echo "NEON symbols:"
  nm "${object_path}" | grep -E \
    'run_neon_acquisition|acquisition_neon_kernel_compiled'
fi

assembly_file="${BUILD_DIR}/acquisition_neon.disassembly.txt"
reference_assembly_file="${BUILD_DIR}/acquisition_reference.disassembly.txt"
if command -v otool >/dev/null 2>&1; then
  otool -tvV "${object_path}" >"${assembly_file}"
  otool -tvV "${reference_object_path}" >"${reference_assembly_file}"
elif command -v objdump >/dev/null 2>&1; then
  objdump -d "${object_path}" >"${assembly_file}"
  objdump -d "${reference_object_path}" >"${reference_assembly_file}"
else
  echo "error: neither otool nor objdump is available for instruction verification" >&2
  exit 1
fi

if grep -Eiq 'ld2[^[:cntrl:]]*\.4s|fmul[^[:cntrl:]]*\.4s|fmla[^[:cntrl:]]*\.4s|fmls[^[:cntrl:]]*\.4s' \
  "${reference_assembly_file}"; then
  echo "error: scalar reference object contains acquisition-style NEON vector instructions" >&2
  exit 1
fi

if ! grep -Eiq 'ld2[^[:cntrl:]]*\.4s|ld2[^[:cntrl:]]*\{[^}]*v[0-9]+' "${assembly_file}"; then
  echo "error: expected NEON deinterleaving load instruction was not found" >&2
  exit 1
fi
if ! grep -Eiq 'fmul[^[:cntrl:]]*\.4s|fmul[^[:cntrl:]]*v[0-9]+' "${assembly_file}"; then
  echo "error: expected vector floating-point multiply instruction was not found" >&2
  exit 1
fi
if ! grep -Eiq 'fmla[^[:cntrl:]]*\.4s|fmls[^[:cntrl:]]*\.4s|fmla[^[:cntrl:]]*v[0-9]+|fmls[^[:cntrl:]]*v[0-9]+' "${assembly_file}"; then
  echo "error: expected vector floating-point multiply-accumulate instruction was not found" >&2
  exit 1
fi

echo
echo "Verified NEON instruction evidence:"
grep -m 12 -Ei \
  'ld2[^[:cntrl:]]*(\.4s|\{[^}]*v[0-9]+)|fmul[^[:cntrl:]]*(\.4s|v[0-9]+)|fmla[^[:cntrl:]]*(\.4s|v[0-9]+)|fmls[^[:cntrl:]]*(\.4s|v[0-9]+)' \
  "${assembly_file}"

echo
echo "Scalar reference object contains none of the checked vector instruction patterns."
