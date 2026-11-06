#!/bin/sh
# Copies a built plug-in bundle into the user's plug-in folder, but only when the bundle's CPU
# architecture can run natively on this Mac. Usage: install_plugin.sh <bundle> <destination folder>
bundle="$1"
dest="$2"
name="$(basename "$bundle")"
binary="$bundle/Contents/MacOS/Tablarion"

host="$(uname -m)"
[ "$(sysctl -n hw.optional.arm64 2>/dev/null)" = "1" ] && host="arm64"
archs="$(lipo -archs "$binary" 2>/dev/null)"

case " $archs " in
  *" $host "*) ;;
  *) echo "note: $name is built for $archs; this Mac is $host, so it is not installed into $dest"; exit 0 ;;
esac

# post-build steps edited Info.plist / Resources: re-seal with an ad-hoc signature
codesign --force --sign - "$bundle" >/dev/null 2>&1 || true

mkdir -p "$dest"
rm -rf "$dest/$name"
ditto "$bundle" "$dest/$name"
echo "installed $name ($archs) -> $dest"

case "$name" in
  *.component) killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true ;;
esac
exit 0
