#!/bin/sh
# Builds universal (Apple Silicon + Intel) Release binaries of Tablarion.app, VST3 and AU.
set -e
cd "$(dirname "$0")"
[ -d build/Tablarion.xcodeproj ] || ./setup.sh "$@"
xcodebuild -project build/Tablarion.xcodeproj -scheme Tablarion_All -configuration Release \
           -destination 'generic/platform=macOS' build
OUT=build/Tablarion_artefacts/Release
for b in "$OUT/Standalone/Tablarion.app/Contents/MacOS/Tablarion" \
         "$OUT/VST3/Tablarion.vst3/Contents/MacOS/Tablarion" \
         "$OUT/AU/Tablarion.component/Contents/MacOS/Tablarion"; do
  lipo -info "$b"
done
echo "Universal binaries are in $OUT"
