#!/bin/bash
set -euo pipefail
if (( $# < 3 || $# > 4 )); then
    echo "Usage: $0 APP_BUNDLE QML_DIR ENTITLEMENTS [SIGNING_IDENTITY]" >&2
    exit 2
fi
APP_BUNDLE=$1
QML_DIR=$2
ENTITLEMENTS=$3
SIGNING_IDENTITY=${4:-}
MACDEPLOYQT=${MACDEPLOYQT:-$(command -v macdeployqt || true)}
[[ -d "$APP_BUNDLE" && -d "$QML_DIR" && -f "$ENTITLEMENTS" ]] || {
    echo 'Bundle, QML directory or entitlements are missing' >&2; exit 1;
}
[[ -n "$MACDEPLOYQT" && -f "$MACDEPLOYQT" && -x "$MACDEPLOYQT" ]] || {
    echo 'MACDEPLOYQT must name an executable macdeployqt file' >&2; exit 1;
}
"$MACDEPLOYQT" "$APP_BUNDLE" "-qmldir=$QML_DIR" \
    -always-overwrite -appstore-compliant -verbose=1
# Remove build-machine paths from ALL Mach-O files before signing anything.
python3 "$(dirname "$0")/macos_bundle.py" clean-sign "$APP_BUNDLE" "$ENTITLEMENTS" "$SIGNING_IDENTITY"
