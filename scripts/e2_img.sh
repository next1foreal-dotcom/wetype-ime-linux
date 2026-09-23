#!/bin/bash
# e2_img.sh — AppImage packaging with a pinned appimagetool release.
set -eo pipefail
BASE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$BASE"
TOOL="${APPIMAGETOOL:-$BASE/.deps/tools/appimagetool-1.9.1-x86_64.AppImage}"
if [ ! -x "$TOOL" ]; then
  echo "== 下载 appimagetool 1.9.1 =="
  mkdir -p "$(dirname "$TOOL")"
  curl -fL --retry 3 -o "$TOOL" \
    https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
bash scripts/e1_appdir.sh
cp AppDir/usr/share/applications/wetype-ime.desktop AppDir/
ARCH="${ARCH:-$(uname -m)}"
OUT="WeTypeIME-Engine-${ARCH}.AppImage"
rm -f WeTypeIME-Engine-*.AppImage
TOOL_ARGS=(--comp zstd)
if [ -n "${APPIMAGE_RUNTIME:-}" ]; then
  TOOL_ARGS+=(--runtime-file "$APPIMAGE_RUNTIME")
fi
ARCH="$ARCH" "$TOOL" "${TOOL_ARGS[@]}" AppDir "$OUT" 2>&1 | tail -3
ls -la "$OUT" | awk '{print $5, $9}'
if [ -f WeTypeIME-Engine-x86_64.AppImage ]; then
  ls -la WeTypeIME-Engine-x86_64.AppImage | awk '{print $5, $9}'
fi
