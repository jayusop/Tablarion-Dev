#!/bin/sh
# Builds Release binaries of Tablarion (Standalone app, VST3, AU) for a chosen CPU.
#
#   ./build.sh                 universal (Apple Silicon + Intel)  -> build/
#   ./build.sh arm64           Apple Silicon only                 -> build-arm64/
#   ./build.sh x86_64          Intel only                         -> build-x86_64/
#   ./build.sh all             all three of the above
#
# Extra CMake options can follow, e.g. ./build.sh arm64 -DTABLARION_SOURCE_URL=https://github.com/you/Tablarion
set -e
cd "$(dirname "$0")"

ARCH="${1:-universal}"
[ $# -gt 0 ] && shift

if [ "$ARCH" = "all" ]; then
  for a in universal arm64 x86_64; do "$0" "$a" "$@"; done
  exit 0
fi

case "$ARCH" in
  universal) DIR=build ;;
  arm64|apple|applesilicon) ARCH=arm64;  DIR=build-arm64 ;;
  x86_64|intel|x64)         ARCH=x86_64; DIR=build-x86_64 ;;
  *) echo "usage: $0 [universal|arm64|x86_64|all] [cmake options...]"; exit 1 ;;
esac

if [ ! -f ThirdParty/JUCE/CMakeLists.txt ]; then
  git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git ThirdParty/JUCE
fi
command -v cmake >/dev/null || { echo "cmake required: brew install cmake"; exit 1; }

echo "==> Configuring $ARCH in $DIR"
cmake -S . -B "$DIR" -G Xcode -DTABLARION_ARCH="$ARCH" "$@"

echo "==> Building $ARCH"
xcodebuild -project "$DIR/Tablarion.xcodeproj" -scheme Tablarion_All -configuration Release \
           -destination 'generic/platform=macOS' -quiet build

OUT="$DIR/Tablarion_artefacts/Release"
echo "==> $ARCH results ($OUT):"
for b in Standalone/Tablarion.app VST3/Tablarion.vst3 AU/Tablarion.component; do
  printf '  %-26s %s\n' "$b" "$(lipo -archs "$OUT/$b/Contents/MacOS/Tablarion")"
done
