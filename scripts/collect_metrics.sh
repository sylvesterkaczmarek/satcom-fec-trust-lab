#!/usr/bin/env bash
#
# Collect basic logs and power stats from a connected Android device.
# Requires adb in PATH and USB debugging enabled.
#
# This is a simple helper, not a full profiler.

set -euo pipefail

OUT_DIR="${1:-metrics_run}"
mkdir -p "${OUT_DIR}"

echo "[satcom-fec] Collecting logcat snapshot..."
adb logcat -d > "${OUT_DIR}/logcat.txt" || echo "logcat failed"

echo "[satcom-fec] Collecting batterystats dump..."
adb shell dumpsys batterystats > "${OUT_DIR}/batterystats.txt" || echo "batterystats failed"

echo "[satcom-fec] Collecting simple device info..."
adb shell getprop > "${OUT_DIR}/device_props.txt" || echo "getprop failed"

echo "[satcom-fec] Done. Output in ${OUT_DIR}"
