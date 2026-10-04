#!/bin/sh
#
# Copyright (c) 2026 Simon Peter
#
# SPDX-License-Identifier: BSD-2-Clause OR GPL-3.0-or-later
#
set -e

# Build the GTK3 global-menu module for the bundle.
#
# Alpine does not package appmenu-gtk-module, and borrowing the host's copy
# would not work either: the bundle ships a musl-linked GTK stack, so a
# glibc-linked module from the host can never be loaded into this process.
#
# GTK loads this module only when the session asks for it through GTK_MODULES,
# so desktops without a global menu keep the in-window menu bar untouched.

VERSION="25.04"
SHA256="2ab8cc56c4a14cb9e0dd24351392c3c8d965a65c4e02c155c1a2e8cba5b21b86"
URL="https://gitlab.com/-/project/6865053/uploads/4f517338d3c65a0ea6f49faf36a4f3e6/appmenu-gtk-module-${VERSION}.tar.xz"

WORK="/tmp/appmenu-gtk-module-build"
rm -rf "$WORK"
mkdir -p "$WORK"

wget -q -O "$WORK/src.tar.xz" "$URL"
echo "$SHA256  $WORK/src.tar.xz" | sha256sum -c -

tar -xJf "$WORK/src.tar.xz" -C "$WORK"
SRC="$WORK/appmenu-gtk-module-${VERSION}"

# Installing into the container's /usr puts the GSettings schema where
# setup-appdir.sh already harvests schemas from; the module aborts at startup
# if org.appmenu.gtk-module is not compiled into the bundle.
meson setup "$SRC/build" "$SRC" --prefix=/usr --buildtype=release \
    -Dgtk=3 -Dtests=false -Dgtk_doc=false
meson compile -C "$SRC/build"
meson install -C "$SRC/build"

echo "appmenu-gtk-module ${VERSION} built:"
ls -l /usr/lib/gtk-3.0/modules/libappmenu-gtk-module.so
ls -l /usr/lib/libappmenu-gtk3-parser.so.*
ls -l /usr/share/glib-2.0/schemas/org.appmenu.gtk-module.gschema.xml
