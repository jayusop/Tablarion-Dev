#!/bin/sh
# Kept for compatibility: same as ./build.sh universal
exec "$(dirname "$0")/build.sh" universal "$@"
