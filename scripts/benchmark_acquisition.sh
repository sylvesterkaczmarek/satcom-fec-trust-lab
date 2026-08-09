#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

bash "${ROOT_DIR}/scripts/build_host_tools.sh" benchmark_acquisition
exec "${ROOT_DIR}/build/host_replay/benchmark_acquisition" "$@"
