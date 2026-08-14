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

if [[ ! -d "$APP_BUNDLE" ]]; then
    echo "Application bundle not found: $APP_BUNDLE" >&2
    exit 1
fi

if [[ ! -d "$QML_DIR" ]]; then
    echo "QML source directory not found: $QML_DIR" >&2
    exit 1
fi

if [[ ! -f "$ENTITLEMENTS" ]]; then
    echo "Entitlements file not found: $ENTITLEMENTS" >&2
    exit 1
fi

if [[ -z "$MACDEPLOYQT" ]]; then
    echo "macdeployqt was not found on PATH" >&2
    exit 1
fi

DEPLOY_OPTIONS=(
    "-qmldir=$QML_DIR"
    -always-overwrite
    -appstore-compliant
    -verbose=1
)

if [[ -n "$SIGNING_IDENTITY" ]]; then
    DEPLOY_OPTIONS+=("-sign-for-notarization=$SIGNING_IDENTITY")
else
    DEPLOY_OPTIONS+=("-codesign=-")
fi

"$MACDEPLOYQT" "$APP_BUNDLE" "${DEPLOY_OPTIONS[@]}"

EXECUTABLE_NAME=$(plutil -extract CFBundleExecutable raw -o - "$APP_BUNDLE/Contents/Info.plist")
APP_EXECUTABLE="$APP_BUNDLE/Contents/MacOS/$EXECUTABLE_NAME"
BUNDLE_RPATH=@executable_path/../Frameworks
HAS_BUNDLE_RPATH=false

while IFS= read -r RPATH; do
    if [[ "$RPATH" == "$BUNDLE_RPATH" ]]; then
        HAS_BUNDLE_RPATH=true
    elif [[ "$RPATH" == /* ]]; then
        install_name_tool -delete_rpath "$RPATH" "$APP_EXECUTABLE"
    fi
done < <(otool -l "$APP_EXECUTABLE" | awk '/LC_RPATH/ { getline; getline; print $2 }' | sort -u)

if [[ "$HAS_BUNDLE_RPATH" == false ]]; then
    install_name_tool -add_rpath "$BUNDLE_RPATH" "$APP_EXECUTABLE"
fi

# macdeployqt signs nested code first. Re-sign the outer bundle so the main
# executable receives the application-specific entitlements.
if [[ -n "$SIGNING_IDENTITY" ]]; then
    codesign \
        --force \
        --options runtime \
        --timestamp \
        --sign "$SIGNING_IDENTITY" \
        --entitlements "$ENTITLEMENTS" \
        "$APP_BUNDLE"
else
    codesign --force --deep --sign - "$APP_BUNDLE"
    codesign \
        --force \
        --sign - \
        --entitlements "$ENTITLEMENTS" \
        "$APP_BUNDLE"
fi
