#!/bin/bash
set -euo pipefail
# This command only reads metadata and verifies signatures. Never launch xPilot.
exec python3 "$(dirname "$0")/macos_bundle.py" verify "$@"
