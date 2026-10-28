#!/bin/sh
# Build "Prayer Reminder.app" (universal: Apple silicon + Intel) with the
# Xcode command line tools. No Xcode project, no dependencies.
#
#   ./build.sh             build into macos/build/
#   ./build.sh --install   also copy to /Applications and launch it
set -e

ROOT=$(cd "$(dirname "$0")" && pwd)
OUT="$ROOT/build"
APP="$OUT/Prayer Reminder.app"

command -v swiftc >/dev/null 2>&1 || {
    echo "swiftc not found. Run: xcode-select --install" >&2
    exit 1
}

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

for ARCH in arm64 x86_64; do
    echo "Compiling $ARCH..."
    swiftc -O -swift-version 5 -target "$ARCH-apple-macos13.0" \
        -o "$OUT/PrayerReminder-$ARCH" "$ROOT"/Sources/*.swift
done
lipo -create -output "$APP/Contents/MacOS/PrayerReminder" "$OUT"/PrayerReminder-{arm64,x86_64}
rm -f "$OUT"/PrayerReminder-{arm64,x86_64}

cp "$ROOT/Resources/Info.plist"   "$APP/Contents/"
cp "$ROOT/Resources/AppIcon.icns" "$APP/Contents/Resources/"
codesign --force --sign - "$APP"          # ad-hoc; enough to run locally
echo "Built $APP"

if [ "$1" = "--install" ]; then
    pkill -x PrayerReminder 2>/dev/null || true
    rm -rf "/Applications/Prayer Reminder.app"
    cp -R "$APP" /Applications/
    open "/Applications/Prayer Reminder.app"
    echo "Installed to /Applications and launched"
fi
