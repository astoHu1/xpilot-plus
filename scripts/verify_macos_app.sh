#!/bin/bash

set -euo pipefail

if (( $# != 1 )); then
    echo "Usage: $0 APP_BUNDLE" >&2
    exit 2
fi

APP_BUNDLE=$1
INFO_PLIST="$APP_BUNDLE/Contents/Info.plist"

fail() {
    echo "macOS bundle verification failed: $*" >&2
    exit 1
}

[[ -d "$APP_BUNDLE/Contents/Frameworks" ]] || fail "Frameworks directory is missing"
[[ -f "$APP_BUNDLE/Contents/PlugIns/platforms/libqcocoa.dylib" ]] || fail "Qt Cocoa platform plugin is missing"
[[ -d "$APP_BUNDLE/Contents/Resources/qml/QtQuick" ]] || fail "deployed Qt Quick modules are missing"
[[ -f "$INFO_PLIST" ]] || fail "Info.plist is missing"

EXECUTABLE_NAME=$(plutil -extract CFBundleExecutable raw -o - "$INFO_PLIST")
APP_EXECUTABLE="$APP_BUNDLE/Contents/MacOS/$EXECUTABLE_NAME"
[[ -x "$APP_EXECUTABLE" ]] || fail "bundle executable is missing: $APP_EXECUTABLE"

MACHO_COUNT=0
while IFS= read -r -d '' CANDIDATE; do
    if ! file "$CANDIDATE" | grep -q 'Mach-O'; then
        continue
    fi

    ((MACHO_COUNT += 1))

    while IFS= read -r DEPENDENCY; do
        case "$DEPENDENCY" in
            @*|/System/Library/*|/usr/lib/*)
                ;;
            *)
                fail "$CANDIDATE has a non-portable dependency: $DEPENDENCY"
                ;;
        esac
    done < <(otool -L "$CANDIDATE" | awk '/^[[:space:]]/ { print $1 }')

    while IFS= read -r RPATH; do
        case "$RPATH" in
            @*)
                ;;
            *)
                fail "$CANDIDATE has an absolute runtime path: $RPATH"
                ;;
        esac
    done < <(otool -l "$CANDIDATE" | awk '/LC_RPATH/ { getline; getline; print $2 }')
done < <(find "$APP_BUNDLE" -type f -print0)

(( MACHO_COUNT > 0 )) || fail "no Mach-O binaries were found"
if ! codesign --verify --deep --strict "$APP_BUNDLE"; then
    codesign --verify --deep --strict --verbose=4 "$APP_BUNDLE" || true
    fail "code signature validation failed"
fi

SMOKE_LOG=$(mktemp "${TMPDIR:-/tmp}/xpilot-smoke.XXXXXX")
APP_PID=

cleanup() {
    if [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    rm -f "$SMOKE_LOG"
}
trap cleanup EXIT

env \
    -u QT_PLUGIN_PATH \
    -u QML2_IMPORT_PATH \
    -u QML_IMPORT_PATH \
    -u QT_QPA_PLATFORM_PLUGIN_PATH \
    -u DYLD_LIBRARY_PATH \
    -u DYLD_FRAMEWORK_PATH \
    -u DYLD_INSERT_LIBRARIES \
    "$APP_EXECUTABLE" >"$SMOKE_LOG" 2>&1 &
APP_PID=$!
sleep 5

if ! kill -0 "$APP_PID" 2>/dev/null; then
    set +e
    wait "$APP_PID"
    APP_STATUS=$?
    set -e
    APP_PID=
    sed -n '1,200p' "$SMOKE_LOG" >&2
    fail "application exited during launch (status $APP_STATUS)"
fi

echo "Verified $MACHO_COUNT Mach-O files; xPilot remained running for 5 seconds."
