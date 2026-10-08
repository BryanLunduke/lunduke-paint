#!/bin/sh
# Build lunduke-paint_0.9-10_amd64.deb into packaging/debs/ (repo-local).
# Does NOT seed lcos-live-06 or lcos-live-07.
#
# Build-Depends for this script (it is not a dpkg-buildpackage source package):
# meson, ninja-build, g++, pkg-config, libgtkmm-3.0-dev and the other libraries
# meson.build already requires, plus xvfb and xauth so the widgets GUI test
# can run with no DISPLAY and no window manager.
set -eu

# A package build must not skip the GUI test. Start Xvfb when the caller did
# not provide a display. If DISPLAY is already set (including `xvfb-run -a
# packaging/build-deb.sh`), do not nest another server.
if [ -z "${DISPLAY:-}" ]; then
  if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "packaging/build-deb.sh: DISPLAY is unset and xvfb-run is not installed (need xvfb and xauth)" >&2
    exit 1
  fi
  exec xvfb-run -a "$0" "$@"
fi
export LUNDUKEPAINT_REQUIRE_DISPLAY=1

# Some images point c++ at clang. This package is built with g++.
if [ -z "${CXX:-}" ]; then
  if command -v c++ >/dev/null 2>&1 && c++ --version 2>&1 | head -n 1 | grep -qi clang; then
    if command -v g++ >/dev/null 2>&1; then
      export CXX=g++
    fi
  fi
fi

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
VERSION="0.9-10"
PKGNAME="lunduke-paint_${VERSION}_amd64"
BUILD="$ROOT/build"
DEST="$ROOT/packaging/src/lunduke-paint"
DEB_DIR="$ROOT/packaging/debs"

cd "$ROOT"

rm -rf "$BUILD"
meson setup "$BUILD" --prefix=/usr --buildtype=release -Dstrip=true
meson compile -C "$BUILD"
meson test -C "$BUILD" --print-errorlogs

rm -rf "$DEST"
meson install -C "$BUILD" --destdir "$DEST"

# Tool/layer symbolic SVGs are in gresource; also ship on-disk for icon theme.
mkdir -p "$DEST/usr/share/icons/hicolor/scalable/actions"
cp -a "$ROOT"/data/icons/hicolor/scalable/actions/*.svg \
  "$DEST/usr/share/icons/hicolor/scalable/actions/"

mkdir -p "$DEST/debian"
cp "$ROOT/debian/control" "$DEST/debian/control"

# dpkg-shlibdeps needs debian/control in cwd.
SHLIBS="$(
  cd "$DEST"
  dpkg-shlibdeps --ignore-missing-info -O \
    -e usr/bin/lunduke-paint
)"
# shlibs:Depends=foo, bar
SHLIBS_DEPS="${SHLIBS#shlibs:Depends=}"

# Installed-Size is KiB.
SIZE="$(du -sk "$DEST/usr" | awk '{print $1}')"

mkdir -p "$DEST/DEBIAN"
cat > "$DEST/DEBIAN/control" << CTRL
Package: lunduke-paint
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: amd64
Installed-Size: ${SIZE}
Maintainer: LCOS <lcos@lunduke.com>
Homepage: https://lunduke.com
Depends: ${SHLIBS_DEPS}, librsvg2-common, shared-mime-info, desktop-file-utils, gtk-update-icon-cache
Description: Lunduke Paint, a GTK3 layered bitmap paint program for Linux X11
 Lunduke Paint is a traditional GTK 3 paint program for Linux X11.
 Look and feel: classic MS Paint plus KolourPaint, with user layers
 and a visible history list. Native project format is OpenRaster (.ora).
CTRL

cat > "$DEST/DEBIAN/postinst" << 'POST'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
  if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database /usr/share/mime >/dev/null 2>&1 || true
  fi
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q /usr/share/applications >/dev/null 2>&1 || true
  fi
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true
  fi
fi
exit 0
POST
chmod 0755 "$DEST/DEBIAN/postinst"

(
  cd "$DEST"
  find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums
)

rm -rf "$DEST/debian"

mkdir -p "$DEB_DIR"
fakeroot dpkg-deb --root-owner-group --build "$DEST" "$DEB_DIR/${PKGNAME}.deb"

echo "built $DEB_DIR/${PKGNAME}.deb"
