#!/bin/sh
# Fetches JUCE and generates the Xcode project.
set -e
cd "$(dirname "$0")"
if [ ! -f ThirdParty/JUCE/CMakeLists.txt ]; then
  git clone --depth 1 --branch 8.0.9 https://github.com/juce-framework/JUCE.git ThirdParty/JUCE
fi
command -v cmake >/dev/null || { echo "cmake required: brew install cmake"; exit 1; }
cmake -B build -G Xcode "$@"
echo "Open build/Tablarion.xcodeproj in Xcode and build the 'Tablarion_All' scheme."
