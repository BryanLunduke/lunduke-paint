# Lunduke Paint

Lunduke Paint is a traditional Linux X11 paint program built with GTK 3.24 and
gtkmm-3.0. Look and feel: classic MS Paint + KolourPaint, with Pinta-style
user layers.


- Binary: `lunduke-paint`
- Application id: `org.lunduke.LundukePaint`
- License: GPL-3.0-or-later
- Language: C++17
- Display: Linux X11 (`GDK_BACKEND=x11`)
- Chrome: window-manager title bar, menu bar, and toolbars (no client-side
  decorations, no HeaderBar as main chrome)
- Theme: follows the active GTK3 theme; the app is not skinned
- Native project file: OpenRaster `.ora` (libarchive + pugixml)
- Version: 0.9
- Config: `$XDG_CONFIG_HOME/lunduke-paint/lunduke-paint.ini` via GKeyFile

## 0.9

LCOS 0.9 identity. Package 0.9-1. About dialog and meson version read 0.9. Same MacPaint-inspired chrome and 0.5-12 UI/icon fixes; howdy launch-gated off. Features and UI unchanged from 0.8.

## 0.8.1

Debian package 0.8-2. Meson and the About dialog show 0.8.1 (the same string the 0.8 UI used for the meson version, not the Debian revision).

Fixes floating selections on save, print, and recovery; flip and rotate of smaller layers; nested OpenRaster groups; Merge Down; layer offset for paint tools; thickness and eraser size; masked Invert Selection; autocrop side margins; and the unsaved marker after undo back to the saved image.

## 0.8

LCOS 0.8 identity. Same MacPaint-inspired chrome and 0.5-12 UI/icon fixes; howdy launch-gated off. Features and UI unchanged from 0.7.

## 0.7

LCOS 0.7 identity. Same MacPaint-inspired chrome and 0.5-12 UI/icon fixes; howdy launch-gated off.

## 0.6

LCOS 0.6 identity. Same MacPaint-inspired chrome and 0.5-12 UI/icon fixes; howdy launch-gated off.

## 0.5

LCOS 0.5 identity. MacPaint-inspired single-window chrome (0.5-3/0.5-12); howdy launch-gated off.

0.5-2: selection move/rubber-band no longer substitutes the empty tool layer for the active layer during is_stroking capture (R-F03 false checkerboard flash); opaque-move empty float pixels keep underlying/canvas BG instead of punching the checker.

0.5-3: MacPaint-inspired single-window layout — left tool grid + line-width selector, pattern strip under the canvas (with FG/BG wells), right rail stacked Layers / History / Colors (not tabbed). Toolbox is the MacPaint tool set (hollow+filled shapes, hand/pan, freeform); 0.4 SVG icons kept. Menus remain File/Edit/View/Image/Layers/Adjustments/Effects/Help.

0.5-4: Two-row MacPaint pattern strip; remove under-canvas ToolOptionsBar; MacPaint brush/stroke pickers live under the left tool grid (line width / brush tips / spray radius). Layers panel grows above History, drops the blend dropdown for icon-only Delete/Rename/Add (right-justified), and supports drag-and-drop reorder.

0.5-5: Hairline separator + air between the left tool grid and stroke/brush pickers; Layers row thumbnail sits on the same row as visibility + lock (with a little horizontal spacing).

0.5-6: Colors panel — drop the inner duplicate "Colors" label; 9×5 column-of-shades palette (light→dark per hue) with rounded swatches and FG checkmark; Custom row with "+" to add colors via the color chooser. L/M/R FG/BG/store kept.

0.5-7: Pattern strip drops the overlapping FG/BG color wells (color picking stays in the right Colors panel); two-row pattern swatches only.

0.5-8: Drop the black outline/border box around the left-rail line-width (and matching brush-tip / spray-radius) pickers; size/shape options unchanged.

0.5-9: Left-rail line-width / brush / spray pickers use theme_bg_color so their fill matches the toolbox strip (no darker gray panel); suppress bottom-left status-bar open/save/hint chatter (keep size/zoom/coords).

0.5-10: Replace app icon — full hicolor PNG set (16/22/24/32/48/64/96/128/256/512); drop misleading scalable SVG so theme uses the new rasters.

0.5-11: Toolbox (and Layers) symbolic icons use currentColor + theme_fg_color at pixbuf load so tools look enabled on Clearlooks instead of washed #bebebe grey; selected-tool chrome unchanged.

0.5-12: Set GTK/X window icon via Gtk::Window::set_default_icon_name (and MainWindow set_icon_name) so the title bar shows the Paint palette icon, not a generic default; desktop StartupWMClass remains lunduke-paint.

## 0.4

Tool options sit in a horizontal strip under the canvas (above the status bar), with the 0.3-style left toolbox (tools + FG/BG only). Keeps the 0.2 feature set. (Howdy intro sources retained but launch-gated off by default.)

0.4-3: dock show/hide re-centers the canvas; Save Image dialog sized + live type/extension sync; About tagline; wand/freeform toolbox icons sized to peers; selection move no longer flashes a bogus transparency checker; Color Eraser tool removed.

## 0.2

What works:

- One or more user layers (bottom → top in storage; Layers list is reversed
  so the top of the list is the top of the stack)
- Each layer: name, visible, locked, opacity 0–100%, blend, offset, RGBA8888
  buffer sized to the document, thumbnail cache
- Internal ToolLayer + SelectionLayer stay out of the Layers list
- Compositor draws the visible viewport only with v1 blends: Normal,
  Multiply, Screen, Overlay, Darken, Lighten
- Paint/effects affect **active layer ∩ selection**
- Locked layer refuses pixel edits (status hint); hidden layers stay in the
  stack but are omitted from the composite
- Layers dock: thumbnail, name, eye, lock, opacity; blend combo; New /
  Duplicate / Delete / Up / Down / Merge down / Flatten; right-click + rename
- Layers menu: New (Ctrl+Shift+N), Duplicate, Delete, Raise, Lower, Merge
  down (Ctrl+E), Flatten (Ctrl+Shift+E), Properties
- Each layer op is one named, undoable Command
- OpenRaster `.ora` read/write (`mimetype` uncompressed first, `stack.xml`,
  `data/layer-N.png`, `mergedimage.png`; lock as `lundukepaint:locked`)
- Non-ORA files still open as a single layer named after the file
- Save As can pick ora/png/jpeg/bmp; multi-layer flat export warns and can
  also keep a `.ora`; never silently overwrites an `.ora` with a flat PNG
- JPEG/BMP flatten onto white and warn
- Multi-tab documents under the tool-options bar; File → Close (Ctrl+W);
  dirty tab close asks to save; New/Open add a tab (replace only an unused
  untitled placeholder)
- Per-document history; the active document drives canvas, layers, colors,
  undo
- Pencil, brush, eraser, fill, picker, rect select, line, rectangle, ellipse
- Color eraser, spraycan, rounded rect, polyline, polygon, curve, text
- Freeform (lasso) and ellipse select (same move / cut / copy / paste / delete / crop ops)
- History dock: named list, click an older row to undo there, a newer row to redo
- Transparent is a first-class FG/BG color (palette last cell + toolbox checker)
- Canvas size / scale / crop / rotate / flip apply to every user layer
- Adjustments (active layer, clipped to selection): Brightness/Contrast,
  Invert, Grayscale, Hue/Saturation, Posterize — each one undoable Command
- Effects (active layer, clipped to selection): box blur, Sharpen, Emboss —
  each one undoable Command
- Preferences: default new-document size, undo limit (default 50, cap 200),
  checker colors, grid threshold (default 400%)
- Help: Keyboard Shortcuts window; About (Lunduke Paint, GPL-3.0-or-later,
  app id `org.lunduke.LundukePaint`)
- Print: Gtk::PrintOperation, fit-to-page, Ctrl+P
- AppStream metainfo (homepage https://lunduke.com, mimetypes), `.desktop` (`%F`, MimeType, StartupWMClass=lunduke-paint), hicolor 16/22/24/32/48/64/96/128/256/512 icons (PNGs; no scalable app SVG)
- Headless tests: `dummy`, `fill`, `history`, `selection`, `transform`,
  `blend`, `ora`, `stroke`, `effects`

## Out of scope

- Live text objects (text still rasterizes on commit)
- Gradient tool
- Clone stamp
- ICO load/save
- Tablet pressure

## Development

Development happens in the agent environment; this repo is the published
source. This tree is the working copy. Publishing to GitHub is a later joint
step (see `PUBLISH.md`). You do not need to install a toolchain or an IDE on
your own machine for day-to-day work.

## How you build on X11 (Debian / Ubuntu)

These are the commands the agent runs. They also document how a human can
optionally test-build after clone.

```sh
sudo apt install -y build-essential meson ninja-build \
  libgtkmm-3.0-dev libcairomm-1.0-dev libgdk-pixbuf-2.0-dev \
  libarchive-dev libpugixml-dev

cd lunduke-paint
meson setup build
meson compile -C build
meson test -C build
GDK_BACKEND=x11 ./build/lunduke-paint
```

On Debian 13 the pixbuf development package is `libgdk-pixbuf-2.0-dev`.

A missing `DISPLAY` is fine for compile and `meson test`. The GUI is not
required for the headless gate.

## Optional human test-build

After GitHub publish, a human may clone and run the same commands on an X11
machine if they want a local test-build. That is optional verification, not
part of the development loop. Target runtime: Linux X11 / XLibre + XFCE.

## Decisions

- Flood-fill similarity is Chebyshev distance across RGBA (`max(|Δr|,|Δg|,|Δb|,|Δa|)`), matching classic Paint-style “how far from the seed color.”
- Eraser always paints the current background color (transparent BG punches alpha); both mouse buttons erase.
- JPEG and BMP flatten onto white (JPEG quality 90) and warn if the composite has transparency or more than one layer.
- PNG export from a multi-layer document flattens visible layers and keeps alpha, after a warning.
- Palette is an 8×6 Paint-like grid; the last swatch is transparent.
- History stores 32×32 tiles that actually changed inside the stroke dirty rect, not the whole layer.
- New canvas-resize pixels default to the current background color (Paint-like); the dialog also offers transparent.
- Image → Clear fills the active layer with the current background color (Paint-like).
- Delete/Backspace fills the selection with transparency, not the background.
- Moving a selection is preview-only until deselect; Ctrl during the move copies (original stays).
- Transparent move skips fully-transparent pixels while dragging and on drop; opaque move replaces the destination rect.
- Paste creates a floating selection on the active layer at the pointer (viewport center if unknown).
- Crop to selection uses the rectangle bounds (insensitive for an inverted selection).
- Autocrop trims any outer row/column that is a single uniform color.
- Rotate 90 is clockwise.
- Scale dialog defaults to nearest neighbor and keep-aspect.
- Pixel grid is on by default and drawn at zoom ≥ the configured threshold (default 400%).
- Blend onto a fully transparent destination uses the source color (Paint/Pinta-like; Multiply does not go black).
- Flatten discards hidden layers and composites the visible ones into one Background layer.
- Merge down blends the active layer onto the one below using the upper layer’s blend and opacity.
- Layer opacity in the dock commits on Enter or focus-out so a spin does not flood undo.
- Canvas size, scale, crop, rotate, and flip transform every user layer (KolourPaint/Pinta document ops).
- New/Open add a tab unless the only document is an unused untitled placeholder, which is replaced.
- Zoom is stored per document/tab.
- OpenRaster uses libarchive for the zip and pugixml for `stack.xml`.
- History dock starts with “New document”; later rows are command names; click jumps the per-document stack (not saved in `.ora`).
- Color eraser replaces FG-similar pixels (Chebyshev) with BG; right-drag swaps the roles.
- Spraycan drops a disk of random dots; density 1–100 is dots per stamp.
- Curve is the KolourPaint two-handle cubic: drag endpoints, then two control points.
- Polyline / polygon take click vertices; Enter or double-click finishes (polygon closes).
- Lasso and ellipse selections store an 8-bit mask; invert is still “canvas minus that region.”
- Text is a popup Entry; Pango rasterizes onto the layer on Enter or click-away (not a live object).
- Toolbox checker under the wells sets Transparent FG (left) or BG (right); painting with it punches alpha.
- Brightness (−100..100) adds a channel offset; contrast (−100..100) scales around 128.
- Hue/Saturation is a basic RGB↔HSV shift (hue ±180°, saturation ±100).
- Posterize quantizes each RGB channel to 2–16 levels.
- Box blur is separable with edge clamp; radius 1–16. Sharpen is a 3×3 unsharp kernel. Emboss is a 3×3 relief kernel with a +128 bias.
- Preferences are `$XDG_CONFIG_HOME/lunduke-paint/lunduke-paint.ini` via GKeyFile (default size, undo limit, checker colors, grid threshold).
- Print is a single fit-to-page `Gtk::PrintOperation` of the visible composite, painted on white.
- File menu includes Recent (local paths in lunduke-paint.ini) and Revert. Edit includes Copy Merged. View includes Fullscreen (F11).
- Toolbox tool icons are Pinta scalable symbolic SVGs (Paint.NET/Pinta/Fluent MIT, Material Apache-2.0, some GIMP CC-BY-4.0) plus original spray/curve/polyline icons; see `data/icons/ATTRIBUTION.md`.

## Layout

Menu bar, main toolbar, tool-options bar (active tool only), document tabs
when more than one file is open, 2-column toolbox with FG/BG wells, canvas
with scrollbars, right dock (Colors / Layers / History), status bar
(`hint | x,y | sel w×h | canvas | zoom | modified`). F12 toggles the
right dock. History is a click-to-jump list for the active document.
