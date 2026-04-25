#!/usr/bin/env bash
# Build sensor-tag-wifi firmware for <variant>, upload to the OTA host, and
# publish a fresh manifest. Run from the repo root.
#
# Usage:  deploy/web/publish-firmware.sh <sht31|ds18b20>

set -euo pipefail

VARIANT="${1:-}"
if [[ "$VARIANT" != "sht31" && "$VARIANT" != "ds18b20" ]]; then
    echo "usage: $0 <sht31|ds18b20>" >&2
    exit 2
fi

OTA_HOST="${OTA_HOST:-192.168.1.61}"
OTA_USER="${OTA_USER:-natas}"
OTA_ROOT="/var/www/combsense-firmware/sensor-tag-wifi/${VARIANT}"
WEB_BASE="http://${OTA_HOST}/firmware/sensor-tag-wifi/${VARIANT}"

cd firmware/sensor-tag-wifi

VERSION="$(git describe --tags --always)"
echo "[publish] variant=${VARIANT} version=${VERSION}"

pio run -e "xiao-c6-${VARIANT}"
BIN=".pio/build/xiao-c6-${VARIANT}/firmware.bin"
[[ -f "$BIN" ]] || { echo "build did not produce ${BIN}" >&2; exit 1; }

SHA="$(shasum -a 256 "$BIN" | awk '{print $1}')"
SIZE="$(stat -f '%z' "$BIN" 2>/dev/null || stat -c '%s' "$BIN")"
echo "[publish] sha256=${SHA} size=${SIZE}"

MANIFEST=$(mktemp)
cat > "$MANIFEST" <<EOF
{
  "version": "${VERSION}",
  "url": "${WEB_BASE}/${VERSION}/firmware.bin",
  "sha256": "${SHA}",
  "size": ${SIZE}
}
EOF

scp "$BIN" "${OTA_USER}@${OTA_HOST}:/tmp/firmware.bin"
scp "$MANIFEST" "${OTA_USER}@${OTA_HOST}:/tmp/manifest.json"
rm -f "$MANIFEST"

ssh "${OTA_USER}@${OTA_HOST}" sudo bash -s <<EOF
set -euo pipefail
mkdir -p "${OTA_ROOT}/${VERSION}"
mv /tmp/firmware.bin "${OTA_ROOT}/${VERSION}/firmware.bin"
chmod 644 "${OTA_ROOT}/${VERSION}/firmware.bin"
mv /tmp/manifest.json "${OTA_ROOT}/manifest.json"
chmod 644 "${OTA_ROOT}/manifest.json"
EOF

echo "[publish] done — manifest at ${WEB_BASE}/manifest.json"
