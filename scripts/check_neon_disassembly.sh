#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
  echo "Usage: scripts/check_neon_disassembly.sh NEON_DISASSEMBLY REFERENCE_DISASSEMBLY" >&2
  exit 2
fi

NEON_ASSEMBLY="$1"
REFERENCE_ASSEMBLY="$2"

for assembly_file in "${NEON_ASSEMBLY}" "${REFERENCE_ASSEMBLY}"; do
  if [[ ! -s "${assembly_file}" ]]; then
    echo "error: disassembly is missing or empty: ${assembly_file}" >&2
    exit 1
  fi
done

# Apple otool puts the lane shape on the mnemonic (ld2.4s); GNU objdump puts
# it on the registers (ld2 {v0.4s-v1.4s}). Both describe the same LD2 load.
LD2_F32_PATTERN='(^|[[:space:]])ld2.*[.]4s'
VECTOR_LOAD_PATTERN='(^|[[:space:]])(ldr|ldur)[[:space:]]+q[0-9]+|(^|[[:space:]])ldp[[:space:]]+q[0-9]+|(^|[[:space:]])ld1.*[.]4s'
UZP1_F32_PATTERN='(^|[[:space:]])uzp1.*[.]4s'
UZP2_F32_PATTERN='(^|[[:space:]])uzp2.*[.]4s'
VECTOR_MULTIPLY_PATTERN='(^|[[:space:]])fmul.*[.]4s'
VECTOR_ACCUMULATE_PATTERN='(^|[[:space:]])(fmla|fmls|fadd|fsub).*[.]4s'

has_pattern() {
  local pattern="$1"
  local assembly_file="$2"
  grep -Eiq "${pattern}" "${assembly_file}"
}

has_deinterleaving_load() {
  local assembly_file="$1"
  if has_pattern "${LD2_F32_PATTERN}" "${assembly_file}"; then
    return 0
  fi
  has_pattern "${VECTOR_LOAD_PATTERN}" "${assembly_file}" &&
    has_pattern "${UZP1_F32_PATTERN}" "${assembly_file}" &&
    has_pattern "${UZP2_F32_PATTERN}" "${assembly_file}"
}

print_evidence() {
  local label="$1"
  local pattern="$2"
  local assembly_file="$3"
  echo "${label}:"
  grep -Eim 8 "${pattern}" "${assembly_file}" | sed 's/^[[:space:]]*/  /'
}

if ! has_deinterleaving_load "${NEON_ASSEMBLY}"; then
  echo "error: expected a float32 LD2 or vector-load plus UZP1/UZP2 deinterleave sequence" >&2
  exit 1
fi
if ! has_pattern "${VECTOR_MULTIPLY_PATTERN}" "${NEON_ASSEMBLY}"; then
  echo "error: expected float32 vector multiply arithmetic was not found" >&2
  exit 1
fi
if ! has_pattern "${VECTOR_ACCUMULATE_PATTERN}" "${NEON_ASSEMBLY}"; then
  echo "error: expected float32 vector accumulation arithmetic was not found" >&2
  exit 1
fi

if has_deinterleaving_load "${REFERENCE_ASSEMBLY}" &&
  has_pattern "${VECTOR_MULTIPLY_PATTERN}" "${REFERENCE_ASSEMBLY}" &&
  has_pattern "${VECTOR_ACCUMULATE_PATTERN}" "${REFERENCE_ASSEMBLY}"; then
  echo "error: scalar reference object contains an equivalent acquisition vector sequence" >&2
  exit 1
fi

echo "Verified NEON instruction evidence:"
if has_pattern "${LD2_F32_PATTERN}" "${NEON_ASSEMBLY}"; then
  echo "  deinterleave lowering: AArch64 LD2 structure load"
  print_evidence "  load instructions" "${LD2_F32_PATTERN}" "${NEON_ASSEMBLY}"
else
  echo "  deinterleave lowering: vector load(s) followed by UZP1/UZP2"
  print_evidence "  vector loads" "${VECTOR_LOAD_PATTERN}" "${NEON_ASSEMBLY}"
  print_evidence "  UZP1 instructions" "${UZP1_F32_PATTERN}" "${NEON_ASSEMBLY}"
  print_evidence "  UZP2 instructions" "${UZP2_F32_PATTERN}" "${NEON_ASSEMBLY}"
fi
print_evidence "  vector multiplies" "${VECTOR_MULTIPLY_PATTERN}" "${NEON_ASSEMBLY}"
print_evidence "  vector accumulation" "${VECTOR_ACCUMULATE_PATTERN}" "${NEON_ASSEMBLY}"

echo "Scalar reference evidence: no complete deinterleave/multiply/accumulate sequence found."
