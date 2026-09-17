#!/bin/bash
set -euo pipefail
umask 077
: "${CERTIFICATE:?Missing signing certificate}"
: "${CERTIFICATE_PASSWORD:?Missing certificate password}"
: "${RUNNER_TEMP:?Missing runner temporary directory}"
: "${GITHUB_ENV:?Missing GitHub environment file}"

SIGNING_KEYCHAIN="$RUNNER_TEMP/xpilot-signing.keychain-db"
CERT_ON_DISK=$(mktemp "$RUNNER_TEMP/xpilot-certificate.XXXXXX")
trap 'rm -f "$CERT_ON_DISK"' EXIT
KEYCHAIN_PASSWORD=$(openssl rand -base64 32)
printf '::add-mask::%s\n' "$KEYCHAIN_PASSWORD"
# Export only the path, never a password or certificate, to later steps.
printf 'SIGNING_KEYCHAIN=%s\n' "$SIGNING_KEYCHAIN" >> "$GITHUB_ENV"
security create-keychain -p "$KEYCHAIN_PASSWORD" "$SIGNING_KEYCHAIN"
security set-keychain-settings -lut 21600 "$SIGNING_KEYCHAIN"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$SIGNING_KEYCHAIN"
printf '%s' "$CERTIFICATE" | openssl base64 -A -d > "$CERT_ON_DISK"
security import "$CERT_ON_DISK" -k "$SIGNING_KEYCHAIN" -f pkcs12 \
    -T /usr/bin/codesign -T /usr/bin/security -P "$CERTIFICATE_PASSWORD"
security set-key-partition-list -S apple-tool:,apple:,codesign: -s \
    -k "$KEYCHAIN_PASSWORD" "$SIGNING_KEYCHAIN" >/dev/null
security list-keychains -d user -s "$SIGNING_KEYCHAIN" "$HOME/Library/Keychains/login.keychain-db"
