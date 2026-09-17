#!/bin/bash
set -euo pipefail
# Run from client/. linuxdeploy inspects dependencies; it never launches xPilot.
: "${Qt6_HOME:?Set Qt6_HOME to the shared Qt installation}"
: "${RUNNER_TEMP:?Set RUNNER_TEMP to a temporary tools directory}"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$Qt6_HOME/bin/qmake"
export QML_SOURCES_PATHS="$PWD/Resources"
export LD_LIBRARY_PATH="$Qt6_HOME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

TOOLS_DIR="$RUNNER_TEMP/xpilot-linuxdeploy"
mkdir -p "$TOOLS_DIR" ../dist
# Immutable releases and verified payload hashes; see PACKAGING.md.
curl --fail --show-error --location \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    -o "$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
curl --fail --show-error --location \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    -o "$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
(
    cd "$TOOLS_DIR"
    printf '%s\n' \
        'c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d  linuxdeploy-x86_64.AppImage' \
        '15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724  linuxdeploy-plugin-qt-x86_64.AppImage' \
        | sha256sum --check --strict
)
chmod +x "$TOOLS_DIR/"*.AppImage
export PATH="$TOOLS_DIR:$PATH"
APPDIR=$(mktemp -d "$RUNNER_TEMP/xpilot-appdir.XXXXXX")
trap 'rm -rf "$APPDIR"' EXIT
export OUTPUT="$PWD/../dist/xPilot.AppImage"
"$TOOLS_DIR/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" \
    --executable build/xpilot --desktop-file xpilot.desktop --icon-file xpilot.png \
    --plugin qt --output appimage
test -s "$OUTPUT"
