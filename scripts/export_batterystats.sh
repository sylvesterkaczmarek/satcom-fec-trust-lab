#!/usr/bin/env bash
#
# Export a batterystats history for offline inspection.
#
# You probably want to:
#   1) reset stats
#   2) run the app for a while
#   3) export stats

set -euo pipefail

OUT_FILE="${1:-batterystats_history.bin}"

echo "[satcom-fec] Resetting batterystats (optional, requires confirmation)..."
echo "If you do not want this, press Ctrl+C now."
sleep 2
adb shell dumpsys batterystats --reset || echo "reset failed or not allowed, continuing"

echo "[satcom-fec] Running batterystats unplugged..."
adb shell dumpsys batterystats --enable full-wake-history >/dev/null 2>&1 || true

echo "[satcom-fec] Exporting batterystats history to ${OUT_FILE}..."
adb shell dumpsys batterystats --proto > "${OUT_FILE}" || echo "export failed"

echo "[satcom-fec] Done."
