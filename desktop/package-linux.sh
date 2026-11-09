#!/usr/bin/env bash
set -euo pipefail
build=${1:?Build directory required}
output=${2:?Output directory required}
stage=$(mktemp -d)
trap 'rm -r -- "$stage"' EXIT
cmake --install "$build" --prefix "$stage/usr"
mkdir -p "$stage/DEBIAN" "$output"
cat > "$stage/DEBIAN/control" <<'EOF'
Package: lightcapture
Version: 1.2.0~preview.1
Section: graphics
Priority: optional
Architecture: amd64
Maintainer: LightCapture <https://github.com/friedSaewoo/LightCapture>
Depends: libc6 (>= 2.38), libstdc++6 (>= 13), libqt6widgets6t64 (>= 6.4), libqt6network6t64 (>= 6.4), libqt6dbus6t64 (>= 6.4), libx11-6, libgif7, qt6-qpa-plugins
Recommends: qt6-wayland, xdg-desktop-portal, fonts-noto-cjk
Description: Screen capture and image editing for Linux
 Capture a region, edit screenshots and record GIF animations on X11.
 Wayland still capture uses the desktop screenshot portal.
EOF
strip "$stage/usr/bin/lightcapture"
dpkg-deb --root-owner-group --build "$stage" "$output/LightCapture-1.2.0-preview.1-ubuntu24.04-amd64.deb"
