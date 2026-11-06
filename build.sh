#!/bin/sh
# Builds Tablarion (Standalone app, VST3, AU) from the command line.
#
#   ./build.sh applesilicon    Apple Silicon (arm64)           -> build/Tablarion_artefacts/AppleSilicon/
#   ./build.sh intel           Intel (x86_64)                  -> build/Tablarion_artefacts/Intel/
#   ./build.sh universal       Apple Silicon + Intel, 1 binary -> build/Tablarion_artefacts/Universal/
#   ./build.sh all             all three of the above
#
# Extra CMake options can follow, e.g. ./build.sh intel -DTABLARION_SOURCE_URL=https://github.com/you/Tablarion
set -e
cd "$(dirname "$0")"

TARGET="${1:-}"
[ $# -gt 0 ] && shift

case "$TARGET" in
  all) "$0" applesilicon "$@"; "$0" intel "$@"; "$0" universal "$@"; exit 0 ;;
  applesilicon|apple|arm64|as) CONFIG=AppleSilicon; SCHEME="Tablarion - Apple Silicon" ;;
  intel|x86_64|x64)            CONFIG=Intel;        SCHEME="Tablarion - Intel" ;;
  universal|uni)               CONFIG=Universal;    SCHEME="Tablarion - Universal" ;;
  *) echo "usage: $0 applesilicon|intel|universal|all [cmake options...]"; exit 1 ;;
esac

./setup.sh "$@" >/dev/null

echo "==> Building $CONFIG"
xcodebuild -project build/Tablarion.xcodeproj -scheme "$SCHEME" \
           -destination 'generic/platform=macOS' -quiet build

OUT="build/Tablarion_artefacts/$CONFIG"
echo "==> $CONFIG results ($OUT):"
for b in Standalone/Tablarion.app VST3/Tablarion.vst3 AU/Tablarion.component; do
  printf '  %-26s %s\n' "$b" "$(lipo -archs "$OUT/$b/Contents/MacOS/Tablarion")"
done
