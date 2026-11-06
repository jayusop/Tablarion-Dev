#!/bin/sh
# Fetches JUCE and generates build/Tablarion.xcodeproj with one scheme per CPU:
#   "Tablarion - Apple Silicon" (arm64), "Tablarion - Intel" (x86_64), "Tablarion - Debug" (this Mac)
set -e
cd "$(dirname "$0")"
if [ ! -f ThirdParty/JUCE/CMakeLists.txt ]; then
  git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git ThirdParty/JUCE
fi
command -v cmake >/dev/null || { echo "cmake required: brew install cmake"; exit 1; }
cmake -S . -B build -G Xcode "$@"
cmake -DXCODEPROJ=build/Tablarion.xcodeproj -P Tools/xcode_schemes.cmake
echo "Open build/Tablarion.xcodeproj and pick the scheme 'Tablarion - Apple Silicon' or 'Tablarion - Intel'."
