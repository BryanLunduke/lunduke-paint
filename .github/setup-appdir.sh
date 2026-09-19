#!/bin/sh
set -e

# Assemble a fully self-contained AppDir.
# Bundles the dynamic linker, all shared libraries, pixbuf loaders,
# GLib schemas, icon themes, and GTK resources.
# Uses Alpine 3.20 to avoid glycin (Alpine edge's sandboxed image loading
# is not suitable for AppDir/AppImage - we want traditional pixbuf loaders).

APPDIR="AppDir"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/lib"

# --- binary, desktop file --------------------------------------------------

cp /tmp/lunduke-install/usr/bin/lunduke-paint "$APPDIR/usr/bin/"
cp /tmp/lunduke-install/usr/share/applications/*.desktop "$APPDIR/usr/share/applications/"
cp /tmp/lunduke-install/usr/share/applications/*.desktop "$APPDIR/"

# appimagetool requires the icon at AppDir root
if [ -f /tmp/lunduke-install/usr/share/icons/hicolor/48x48/apps/org.lunduke.LundukePaint.png ]; then
    cp /tmp/lunduke-install/usr/share/icons/hicolor/48x48/apps/org.lunduke.LundukePaint.png "$APPDIR/"
elif [ -f /tmp/lunduke-install/usr/share/icons/hicolor/scalable/apps/org.lunduke.LundukePaint.svg ]; then
    cp /tmp/lunduke-install/usr/share/icons/hicolor/scalable/apps/org.lunduke.LundukePaint.svg "$APPDIR/"
fi

# --- AppRun entry point -----------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -f "${SCRIPT_DIR}/AppRun" ]; then
    cp "${SCRIPT_DIR}/AppRun" "$APPDIR/AppRun"
    chmod +x "$APPDIR/AppRun"
else
    echo "WARNING: .github/AppRun not found"
fi

# --- dynamic linker --------------------------------------------------------
# Bundle the dynamic linker so the binary has zero host dependencies.

LINKER=""
LINKER=$(readelf -l "$APPDIR/usr/bin/lunduke-paint" 2>/dev/null | awk '/interpreter/ { gsub(/.*: /,""); gsub(/\]/,""); print $1 }')
if [ -z "$LINKER" ] || [ ! -f "$LINKER" ]; then
    LINKER=$(ldd "$APPDIR/usr/bin/lunduke-paint" 2>/dev/null | head -1 | awk '{ print $1 }')
fi
if [ -z "$LINKER" ] || [ ! -f "$LINKER" ]; then
    for p in /lib/ld-musl-x86_64.so.1 /lib64/ld-linux-x86-64.so.2 /lib/ld-linux-x86-64.so.2; do
        if [ -f "$p" ]; then LINKER="$p"; break; fi
    done
fi

if [ -n "$LINKER" ] && [ -f "$LINKER" ]; then
    mkdir -p "$APPDIR/lib"
    cp -a "$LINKER" "$APPDIR/lib/"
    LINKER_NAME=$(basename "$LINKER")
    # Set interpreter to a path relative to AppDir root.
    # The kernel resolves relative interp paths from cwd, so AppRun
    # must cd to AppDir before exec'ing the binary.
    patchelf --set-interpreter "lib/$LINKER_NAME" \
        "$APPDIR/usr/bin/lunduke-paint" 2>/dev/null || true
    echo "Dynamic linker bundled: $LINKER_NAME (from $LINKER)"
else
    echo "WARNING: Could not find dynamic linker"
    LINKER_NAME=""
fi

# --- shared libraries ------------------------------------------------------
# Copy every library the binary needs into usr/lib.

echo "Copying shared libraries..."
ldd "$APPDIR/usr/bin/lunduke-paint" 2>/dev/null | while read -r line; do
    lib_path=$(echo "$line" | awk '/=>/ { print $3 }')
    if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
        lib_name=$(basename "$lib_path")
        cp -f "$lib_path" "$APPDIR/usr/lib/$lib_name" 2>/dev/null || true
    fi
done

# Also copy transitive dependencies
for lib in "$APPDIR/usr/lib/"lib*.so.*; do
    [ -f "$lib" ] || continue
    ldd "$lib" 2>/dev/null | while read -r line; do
        lib_path=$(echo "$line" | awk '/=>/ { print $3 }')
        if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
            lib_name=$(basename "$lib_path")
            if [ ! -f "$APPDIR/usr/lib/$lib_name" ]; then
                cp -f "$lib_path" "$APPDIR/usr/lib/$lib_name" 2>/dev/null || true
            fi
        fi
    done
done

# Also copy the linker's search path libs (ld-linux needs them)
for lib in "$APPDIR/lib/"ld-*.so.*; do
    [ -f "$lib" ] || continue
    ldd "$lib" 2>/dev/null | while read -r line; do
        lib_path=$(echo "$line" | awk '/=>/ { print $3 }')
        if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
            lib_name=$(basename "$lib_path")
            if [ ! -f "$APPDIR/usr/lib/$lib_name" ] && [ ! -f "$APPDIR/lib/$lib_name" ]; then
                cp -f "$lib_path" "$APPDIR/usr/lib/$lib_name" 2>/dev/null || true
            fi
        fi
    done
done

echo "Libraries bundled: $(ls "$APPDIR/usr/lib/" 2>/dev/null | wc -l)"

# --- libdbusmenu-gtk3 ------------------------------------------------------
# The bundled GTK is musl-linked, so a host copy of libdbusmenu can never be
# loaded into this process - it has to travel inside the AppDir.

echo "Bundling libdbusmenu..."
DBUSMENU_FOUND=0
for lib in /usr/lib/libdbusmenu-gtk3.so.* /usr/lib/libdbusmenu-glib.so.*; do
    [ -f "$lib" ] || continue
    cp -a "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
    DBUSMENU_FOUND=1
done

if [ "$DBUSMENU_FOUND" = 1 ]; then
    # Pull in whatever the dbusmenu libraries themselves need.
    for lib in "$APPDIR/usr/lib/"libdbusmenu-*.so.*; do
        [ -f "$lib" ] || continue
        ldd "$lib" 2>/dev/null | while read -r line; do
            lib_path=$(echo "$line" | awk '/=>/ { print $3 }')
            if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
                lib_name=$(basename "$lib_path")
                if [ ! -f "$APPDIR/usr/lib/$lib_name" ]; then
                    cp -f "$lib_path" "$APPDIR/usr/lib/" 2>/dev/null || true
                    echo "libdbusmenu dependency bundled: $lib_name"
                fi
            fi
        done
    done
    echo "libdbusmenu bundled: $(ls "$APPDIR/usr/lib/"libdbusmenu-*.so.* 2>/dev/null | wc -l) files"
else
    echo "WARNING: libdbusmenu-gtk3 not found in /usr/lib"
fi

# --- patchelf: set RPATH ---------------------------------------------------
# Set RPATH so the binary finds bundled libs.

patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN' \
    "$APPDIR/usr/bin/lunduke-paint" 2>/dev/null || true

# Patch bundled libs to use relative paths
for lib in "$APPDIR/usr/lib/"lib*.so.*; do
    [ -f "$lib" ] || continue
    file "$lib" 2>/dev/null | grep -q "ELF" || continue
    patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
done

# --- hicolor + adwaita icon themes -----------------------------------------

mkdir -p "$APPDIR/usr/share/icons/hicolor"
if [ -f /usr/share/icons/hicolor/index.theme ]; then
    cp /usr/share/icons/hicolor/index.theme "$APPDIR/usr/share/icons/hicolor/"
else
    cat > "$APPDIR/usr/share/icons/hicolor/index.theme" <<'EOF'
[Icon Theme]
Name=hicolor
Comment=Fallback icon theme
Directories=16x16/apps,32x32/apps,48x48/apps,scalable/apps,scalable/actions

[16x16/apps]
Size=16
Type=Fixed
Context=Apps

[32x32/apps]
Size=32
Type=Fixed
Context=Apps

[48x48/apps]
Size=48
Type=Fixed
Context=Apps

[scalable/apps]
Size=48
MinSize=16
MaxSize=256
Type=Scalable
Context=Apps

[scalable/actions]
Size=48
MinSize=16
MaxSize=256
Type=Scalable
Context=Actions
EOF
fi

for size in 16x16 32x32 48x48 scalable; do
    src_dir="/tmp/lunduke-install/usr/share/icons/hicolor/${size}/apps"
    dst_dir="$APPDIR/usr/share/icons/hicolor/${size}/apps"
    if [ -d "$src_dir" ]; then
        mkdir -p "$dst_dir"
        cp -a "$src_dir"/* "$dst_dir/" 2>/dev/null || true
    fi
done

# Bundle Adwaita icons (GTK needs these for stock icons like image-missing)
if [ -d /usr/share/icons/Adwaita ]; then
    mkdir -p "$APPDIR/usr/share/icons"
    cp -a /usr/share/icons/Adwaita "$APPDIR/usr/share/icons/"
    echo "Adwaita icons bundled: $(find "$APPDIR/usr/share/icons/Adwaita" -name "*.svg" -o -name "*.png" 2>/dev/null | wc -l) files"
else
    echo "WARNING: /usr/share/icons/Adwaita not found"
    # Fallback: try to find Adwaita elsewhere
    find /usr/share/icons -maxdepth 1 -name "Adwaita" -type d 2>/dev/null | while read d; do
        echo "Found Adwaita at $d"
        mkdir -p "$APPDIR/usr/share/icons"
        cp -a "$d" "$APPDIR/usr/share/icons/"
    done
fi

# Also ensure hicolor has the actions directory for standard GTK icons
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/actions"

# Copy standard GTK action icons from Adwaita into hicolor so they are found first
# (list-add, edit-copy, edit-delete, go-up, view-reveal, etc.)
for icon in list-add edit-copy edit-delete go-up go-down go-bottom \
            changes-prevent view-restore view-reveal view-sidebar-end \
            window-close media-playback-start media-playback-pause; do
    for ext in svg png; do
        src=$(find "$APPDIR/usr/share/icons/Adwaita" -name "${icon}-symbolic.${ext}" -type f 2>/dev/null | head -1)
        if [ -n "$src" ]; then
            cp -f "$src" "$APPDIR/usr/share/icons/hicolor/scalable/actions/" 2>/dev/null || true
        fi
    done
done
echo "Standard action icons copied to hicolor: $(ls "$APPDIR/usr/share/icons/hicolor/scalable/actions/" 2>/dev/null | wc -l)"

# --- GTK 3.0 data ----------------------------------------------------------

if [ -d /usr/share/gtk-3.0 ]; then
    mkdir -p "$APPDIR/usr/share/gtk-3.0"
    cp -rn /usr/share/gtk-3.0/* "$APPDIR/usr/share/gtk-3.0/" 2>/dev/null || true
fi

# --- GTK GResource files ----------------------------------------------------
# On Alpine, GTK's UI templates (gtkcombobox.ui etc) are in separate
# .gresource files alongside the .so. We must bundle these.

for gtk_lib in "$APPDIR/usr/lib/"libgtk-3*.so.*; do
    [ -f "$gtk_lib" ] || continue
    gresource="${gtk_lib}.gresource"
    if [ -f "$gresource" ]; then
        echo "GTK GResource bundled: $(basename "$gresource")"
    else
        # Check system path
        sys_gr="/usr/lib/$(basename "$gresource")"
        if [ -f "$sys_gr" ]; then
            cp -f "$sys_gr" "$APPDIR/usr/lib/"
            echo "GTK GResource bundled from system: $(basename "$sys_gr")"
        fi
    fi
done

# Also check for standalone GTK GResource files
for gr in /usr/lib/libgtk-3*.so.*.gresource; do
    [ -f "$gr" ] || continue
    gr_name=$(basename "$gr")
    if [ ! -f "$APPDIR/usr/lib/$gr_name" ]; then
        cp -f "$gr" "$APPDIR/usr/lib/"
        echo "GTK GResource bundled: $gr_name"
    fi
done

# --- GdkPixbuf loaders (traditional .so) -----------------------------------

# Find the actual loaders directory dynamically (avoids hardcoded versions)
PIXBUF_LOADERS_DIR=""
PIXBUF_CACHE_FILE=""

for dir in \
    /usr/lib/gdk-pixbuf-2.0/*/loaders \
    /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/*/loaders \
    /usr/lib64/gdk-pixbuf-2.0/*/loaders; do
    if [ -d "$dir" ]; then
        PIXBUF_LOADERS_DIR="$dir"
        PIXBUF_CACHE_FILE="$(dirname "$dir")/loaders.cache"
        break
    fi
done

# Determine the version subdirectory for the AppDir layout
if [ -n "$PIXBUF_LOADERS_DIR" ]; then
    # Extract the version part (e.g., "2.10.0") from the source path
    PIXBUF_VERSION=$(basename "$(dirname "$PIXBUF_LOADERS_DIR")")
else
    PIXBUF_VERSION="2.10.0"
fi
PIXBUF_BASE="gdk-pixbuf-2.0/$PIXBUF_VERSION"

if [ -n "$PIXBUF_LOADERS_DIR" ]; then
    mkdir -p "$APPDIR/usr/lib/$PIXBUF_BASE/loaders"
    cp -a "$PIXBUF_LOADERS_DIR"/* "$APPDIR/usr/lib/$PIXBUF_BASE/loaders/" 2>/dev/null || true
    # Regenerate loaders.cache from the bundled loaders (clean relative paths)
    if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
        gdk-pixbuf-query-loaders "$APPDIR/usr/lib/$PIXBUF_BASE/loaders/"* 2>/dev/null \
            | sed 's|".*/loaders/|"|g' \
            > "$APPDIR/usr/lib/$PIXBUF_BASE/loaders.cache" || true
    elif [ -f "$PIXBUF_CACHE_FILE" ]; then
        sed 's|".*/loaders/|"|g' "$PIXBUF_CACHE_FILE" \
            > "$APPDIR/usr/lib/$PIXBUF_BASE/loaders.cache"
    fi
    echo "Pixbuf loaders bundled from $PIXBUF_LOADERS_DIR: $(ls "$APPDIR/usr/lib/$PIXBUF_BASE/loaders/" 2>/dev/null | wc -l)"
else
    echo "WARNING: No pixbuf loaders directory found"
fi

# Copy transitive dependencies of pixbuf loaders (e.g., librsvg for SVG)
for loader in "$APPDIR/usr/lib/$PIXBUF_BASE/loaders/"*.so; do
    [ -f "$loader" ] || continue
    ldd "$loader" 2>/dev/null | while read -r line; do
        lib_path=$(echo "$line" | awk '/=>/ { print $3 }')
        if [ -n "$lib_path" ] && [ -f "$lib_path" ]; then
            lib_name=$(basename "$lib_path")
            if [ ! -f "$APPDIR/usr/lib/$lib_name" ]; then
                cp -f "$lib_path" "$APPDIR/usr/lib/" 2>/dev/null || true
                echo "Loader dependency bundled: $lib_name"
            fi
        fi
    done
done

# --- GLib compiled schemas -------------------------------------------------

SCHEMA_DIR="/usr/share/glib-2.0/schemas"
if [ -d "$SCHEMA_DIR" ]; then
    mkdir -p "$APPDIR/usr/share/glib-2.0/schemas"
    cp -a "$SCHEMA_DIR"/* "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "$APPDIR/usr/share/glib-2.0/schemas/" 2>/dev/null || true
    fi
    echo "GLib schemas bundled"
fi

echo ""
echo "AppDir assembled."
echo "  Dynamic linker: $(ls "$APPDIR/lib/"ld-* 2>/dev/null || echo 'none')"
echo "  Libraries: $(ls "$APPDIR/usr/lib/"*.so.* 2>/dev/null | wc -l)"
echo "  Loaders: $(ls "$APPDIR/usr/lib/$PIXBUF_BASE/loaders/" 2>/dev/null | wc -l)"
