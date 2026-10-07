project(
  'lunduke-paint',
  'cpp',
  version: '0.9',
  license: 'GPL-3.0-or-later',
  meson_version: '>=0.56.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'werror=false',
  ],
)

# warning_level=2 => -Wall -Wextra
gtkmm_dep = dependency('gtkmm-3.0', version: '>=3.24')
cairomm_dep = dependency('cairomm-1.0')
gdkpixbuf_dep = dependency('gdk-pixbuf-2.0')
glib_dep = dependency('glib-2.0')
libarchive_dep = dependency('libarchive')
pugixml_dep = dependency('pugixml')
zlib_dep = dependency('zlib')
pangocairo_dep = dependency('pangocairo')

gnome = import('gnome')
lunduke_paint_resources = gnome.compile_resources(
  'lunduke-paint-resources',
  'data/lunduke-paint.gresource.xml',
  source_dir: 'data',
  c_name: 'lunduke_paint',
)

lunduke_paint_inc = include_directories('src')

lunduke_paint_core_sources = files(
  'src/doc/document.cpp',
  'src/doc/workspace.cpp',
  'src/doc/layer.cpp',
  'src/doc/layer_stack.cpp',
  'src/doc/history.cpp',
  'src/doc/commands_pixels.cpp',
  'src/doc/commands_image.cpp',
  'src/doc/effect_preview.cpp',
  'src/doc/selection.cpp',
  'src/raster/fill.cpp',
  'src/raster/stroke.cpp',
  'src/raster/brush_tip.cpp',
  'src/raster/transform.cpp',
  'src/raster/shapes.cpp',
  'src/raster/blend.cpp',
  'src/raster/text.cpp',
  'src/raster/text_box.cpp',
  'src/raster/effects.cpp',
  'src/raster/pattern.cpp',
  'src/doc/commands_layers.cpp',
  'src/io/image_io.cpp',
  'src/io/ora.cpp',
  'src/io/crash_recovery.cpp',
)

lunduke_paint_core = static_library(
  'lunduke_paint_core',
  lunduke_paint_core_sources,
  include_directories: lunduke_paint_inc,
  dependencies: [gdkpixbuf_dep, glib_dep, libarchive_dep, pugixml_dep, pangocairo_dep, zlib_dep],
)

lunduke_paint_sources = files(
  'src/main.cpp',
  'src/app/application.cpp',
  'src/app/main_window.cpp',
  'src/app/actions.cpp',
  'src/app/preferences.cpp',
  'src/ui/canvas_view.cpp',
  'src/ui/intro_howdy.cpp',
  'src/ui/toolbox.cpp',
  'src/ui/pattern_strip.cpp',
  'src/ui/tool_options_bar.cpp',
  'src/ui/colors_panel.cpp',
  'src/ui/layers_panel.cpp',
  'src/ui/history_panel.cpp',
  'src/ui/status_bar.cpp',
  'src/ui/dialogs_new.cpp',
  'src/tools/tool.cpp',
  'src/tools/tool_pencil.cpp',
  'src/tools/tool_brush.cpp',
  'src/tools/tool_eraser.cpp',
  'src/tools/tool_picker.cpp',
  'src/tools/tool_fill.cpp',
  'src/tools/tool_rect_select.cpp',
  'src/tools/tool_line.cpp',
  'src/tools/tool_rectangle.cpp',
  'src/tools/tool_ellipse.cpp',
  'src/tools/tool_spray.cpp',
  'src/tools/tool_rounded_rect.cpp',
  'src/tools/tool_polyline.cpp',
  'src/tools/tool_polygon.cpp',
  'src/tools/tool_curve.cpp',
  'src/tools/tool_lasso.cpp',
  'src/tools/tool_ellipse_select.cpp',
  'src/tools/tool_text.cpp',
  'src/tools/tool_magic_wand.cpp',
  'src/tools/tool_freeform.cpp',
  'src/tools/tool_hand.cpp',
  'src/tools/selection_xform.cpp',
  'src/ui/dialogs_image.cpp',
  'src/ui/dialogs_adjust.cpp',
  'src/ui/dialogs_prefs.cpp',
)

executable(
  'lunduke-paint',
  lunduke_paint_sources,
  lunduke_paint_resources,
  dependencies: [gtkmm_dep, cairomm_dep, gdkpixbuf_dep, pangocairo_dep],
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: true,
)

test_dummy = executable(
  'test_dummy',
  files('tests/test_dummy.cpp'),
  install: false,
)
test('dummy', test_dummy)

test_fill = executable(
  'test_fill',
  files('tests/test_fill.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('fill', test_fill)

test_history = executable(
  'test_history',
  files('tests/test_history.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('history', test_history)

test_selection = executable(
  'test_selection',
  files('tests/test_selection.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('selection', test_selection)

test_transform = executable(
  'test_transform',
  files('tests/test_transform.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('transform', test_transform)

test_blend = executable(
  'test_blend',
  files('tests/test_blend.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('blend', test_blend)

test_ora = executable(
  'test_ora',
  files('tests/test_ora.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  dependencies: [gdkpixbuf_dep, libarchive_dep, pugixml_dep],
  install: false,
)
test('ora', test_ora)

test_stroke = executable(
  'test_stroke',
  files('tests/test_stroke.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('stroke', test_stroke)

test_effects = executable(
  'test_effects',
  files('tests/test_effects.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('effects', test_effects)

test_crash_recovery = executable(
  'test_crash_recovery',
  files('tests/test_crash_recovery.cpp', 'src/app/preferences.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  dependencies: [gdkpixbuf_dep, glib_dep, gtkmm_dep, libarchive_dep, pugixml_dep],
  install: false,
)
test('crash_recovery', test_crash_recovery)

test_effect_preview = executable(
  'test_effect_preview',
  files('tests/test_effect_preview.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  install: false,
)
test('effect_preview', test_effect_preview)

# Non-GUI half of the startup greeting: bundled path length and the one-second
# stroke-progress maths. Cairo only, no display or font needed.
test_intro_howdy = executable(
  'test_intro_howdy',
  files('tests/test_intro_howdy.cpp', 'src/ui/intro_howdy.cpp'),
  include_directories: lunduke_paint_inc,
  dependencies: [pangocairo_dep, glib_dep],
  install: false,
)
test('intro_howdy', test_intro_howdy)

# Widget-level checks that need an X display: skipped (exit 77) when there is
# none, so `meson test` stays clean headless and can be re-run under xvfb-run.
test_widgets = executable(
  'test_widgets',
  files('tests/test_widgets.cpp', 'src/ui/canvas_view.cpp', 'src/ui/toolbox.cpp',
  'src/ui/pattern_strip.cpp',
        'src/ui/intro_howdy.cpp', 'src/tools/tool.cpp', 'src/tools/selection_xform.cpp'),
  lunduke_paint_resources,
  include_directories: lunduke_paint_inc,
  dependencies: [gtkmm_dep, cairomm_dep, gdkpixbuf_dep, pangocairo_dep],
  link_with: lunduke_paint_core,
  install: false,
)
test('widgets', test_widgets)

test_copy_merged = executable(
  'test_copy_merged',
  files('tests/test_copy_merged.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  dependencies: [gdkpixbuf_dep],
  install: false,
)
test('copy_merged', test_copy_merged)

test_text_box = executable(
  'test_text_box',
  files('tests/test_text_box.cpp'),
  include_directories: lunduke_paint_inc,
  link_with: lunduke_paint_core,
  dependencies: [gdkpixbuf_dep, glib_dep, libarchive_dep, pugixml_dep, pangocairo_dep],
  install: false,
)
test('text_box', test_text_box)

test_shortcuts = executable(
  'test_shortcuts',
  files('tests/test_shortcuts.cpp'),
  include_directories: lunduke_paint_inc,
  install: false,
)
test('shortcuts', test_shortcuts)

install_data(
  'data/org.lunduke.LundukePaint.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data(
  'data/org.lunduke.LundukePaint.metainfo.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

# App icon: hicolor PNGs are authoritative (no scalable SVG — GTK prefers SVG over PNG).
app_icon_sizes = ['16', '22', '24', '32', '48', '64', '96', '128', '256', '512']
foreach size : app_icon_sizes
  install_data(
    'data/icons/hicolor/' + size + 'x' + size + '/apps/org.lunduke.LundukePaint.png',
    install_dir: join_paths(get_option('datadir'), 'icons/hicolor/' + size + 'x' + size + '/apps'),
  )
endforeach

install_data(
  'data/mime/org.lunduke.LundukePaint.xml',
  install_dir: join_paths(get_option('datadir'), 'mime/packages'),
)
