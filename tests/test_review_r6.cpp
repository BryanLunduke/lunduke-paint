// SPDX-License-Identifier: GPL-3.0-or-later
// Round-6 review: colour wells, toolbox catalog, patterns, free zoom-to-fit,
// scaled composite, region blur, undo patches, and the menu/shortcut class.

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/history.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "raster/brush_tip.hpp"
#include "raster/effects.hpp"
#include "raster/shapes.hpp"
#include "raster/types.hpp"
#include "ui/color_well.hpp"
#include "ui/palette_click.hpp"
#include "ui/toolbox_catalog.hpp"
#include "ui/tool_selection.hpp"
#include "ui/zoom_policy.hpp"
#include "app/shortcut_help.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

using lundukepaint::BlendMode;
using lundukepaint::BlurWork;
using lundukepaint::BrushTip;
using lundukepaint::BrushTipKind;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::History;
using lundukepaint::Layer;
using lundukepaint::LayerStack;
using lundukepaint::PaletteClick;
using lundukepaint::PixelPatchCommand;
using lundukepaint::Rect;
using lundukepaint::ShapeFillMode;
using lundukepaint::ShortcutHelpRow;
using lundukepaint::ToolSelection;
using lundukepaint::WellHit;
using lundukepaint::box_blur_rect;
using lundukepaint::box_blur_scratch_bytes;
using lundukepaint::color_well_hit;
using lundukepaint::draw_line;
using lundukepaint::draw_rectangle;
using lundukepaint::kDefaultUndoBytes;
using lundukepaint::palette_click;
using lundukepaint::pattern_at;
using lundukepaint::shortcut_help_rows;
using lundukepaint::snapped_zoom_step;
using lundukepaint::stamp_brush_tip;
using lundukepaint::toolbox_has_tool;
using lundukepaint::toolbox_tools;
using lundukepaint::zoom_fit_ratio;

int g_errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r6: %s\n", msg);
    ++g_errors;
  }
}

Color pixel_at(const std::uint8_t* buf, int stride, int x, int y) {
  const std::uint8_t* p = buf + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                          static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void test_palette_and_wells() {
  expect(palette_click(1, false, false) == PaletteClick::Foreground, "left click sets foreground");
  expect(palette_click(3, false, false) == PaletteClick::Background, "right click sets background");
  expect(palette_click(2, false, false) == PaletteClick::Background, "middle click sets background");
  expect(palette_click(1, true, false) == PaletteClick::Background, "shift-left sets background");
  expect(palette_click(1, false, true) == PaletteClick::Edit, "double-click edits the swatch");
  expect(palette_click(3, false, true) == PaletteClick::Background,
         "a right double-click still sets the background");
  expect(palette_click(8, false, false) == PaletteClick::Ignore, "other buttons are ignored");

  expect(color_well_hit(4, 4) == WellHit::Foreground, "the front well is the foreground");
  expect(color_well_hit(20, 18) == WellHit::Foreground, "the overlap belongs to the foreground");
  expect(color_well_hit(36, 30) == WellHit::Background, "the peeking corner is the background");
  expect(color_well_hit(0, 0) == WellHit::None, "the margin is not a well");
}

void test_toolbox_catalog() {
  expect(toolbox_has_tool("picker"), "picker is a toolbox tool");
  expect(toolbox_has_tool("magic-wand"), "magic wand is a toolbox tool");
  expect(toolbox_has_tool("ellipse-select"), "ellipse select is a toolbox tool");
  expect(toolbox_has_tool("curve"), "curve is a toolbox tool");
  expect(toolbox_has_tool("polyline"), "polyline is a toolbox tool");
  expect(toolbox_has_tool("pencil"), "pencil stays in the catalog");
  expect(!toolbox_has_tool("no-such-tool"), "unknown ids are not tools");

  int count = 0;
  const auto* tools = toolbox_tools(count);
  ToolSelection selection;
  for (int i = 0; i < count; ++i) {
    selection.add(tools[i].id);
  }
  expect(selection.select("magic-wand"), "selecting the wand moves the highlight");
  expect(selection.active_id() == "magic-wand", "the active id is the wand");
  expect(selection.is_selected("magic-wand"), "the wand button is the selected one");
  expect(!selection.select("no-such-tool"), "an unknown id does not move the highlight");
  expect(selection.active_id() == "magic-wand", "an unknown id keeps the previous tool");
  expect(selection.select("curve"), "the curve button can take the highlight");
  expect(!selection.is_selected("magic-wand"), "the previous button is no longer lit");
}

void test_patterns() {
  const lundukepaint::Pattern& checker = pattern_at(4);
  const Color fg{220, 20, 20, 255};
  const Color bg{20, 20, 220, 255};
  const int w = 32;
  const int stride = w * 4;
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(stride) * w, 0);

  draw_rectangle(buf.data(), w, w, stride, 2, 2, 28, 28, 1, fg, ShapeFillMode::Fill, false, nullptr,
                 &checker, bg);
  bool saw_fg = false;
  bool saw_bg = false;
  for (int y = 8; y < 22; ++y) {
    for (int x = 8; x < 22; ++x) {
      const Color c = pixel_at(buf.data(), stride, x, y);
      if (c == fg) {
        saw_fg = true;
      }
      if (c == bg) {
        saw_bg = true;
      }
    }
  }
  expect(saw_fg && saw_bg, "a filled rectangle interior uses the pattern");

  std::vector<std::uint8_t> stroke(static_cast<std::size_t>(stride) * w, 0);
  for (std::size_t i = 0; i < stroke.size(); i += 4) {
    stroke[i] = 10;
    stroke[i + 1] = 200;
    stroke[i + 2] = 10;
    stroke[i + 3] = 255;
  }
  draw_rectangle(stroke.data(), w, w, stride, 2, 2, 28, 28, 1, fg, ShapeFillMode::Stroke, false,
                 nullptr, nullptr, bg);
  const Color edge = pixel_at(stroke.data(), stride, 10, 2);
  const Color inside = pixel_at(stroke.data(), stride, 10, 14);
  expect(edge == fg, "a shape outline is solid foreground");
  expect(inside.g == 200 && inside.r == 10, "a hollow shape leaves the interior unpatterned");

  std::vector<std::uint8_t> both(static_cast<std::size_t>(stride) * w, 0);
  draw_rectangle(both.data(), w, w, stride, 2, 2, 28, 28, 1, fg, ShapeFillMode::Both, false, nullptr,
                 &checker, bg);
  expect(pixel_at(both.data(), stride, 10, 2) == fg, "Both mode keeps the outline solid");
  bool both_bg = false;
  for (int y = 8; y < 22; ++y) {
    for (int x = 8; x < 22; ++x) {
      if (pixel_at(both.data(), stride, x, y) == bg) {
        both_bg = true;
      }
    }
  }
  expect(both_bg, "Both mode patterns the interior");

  std::vector<std::uint8_t> brush(static_cast<std::size_t>(stride) * w, 0);
  const BrushTip tip{BrushTipKind::Square, 8};
  stamp_brush_tip(brush.data(), w, w, stride, 16, 16, tip, fg, nullptr, &checker, bg);
  bool brush_fg = false;
  bool brush_bg = false;
  for (int y = 0; y < w; ++y) {
    for (int x = 0; x < w; ++x) {
      const Color c = pixel_at(brush.data(), stride, x, y);
      if (c == fg) {
        brush_fg = true;
      }
      if (c == bg) {
        brush_bg = true;
      }
    }
  }
  expect(brush_fg && brush_bg, "the brush stamps the active pattern");

  std::vector<std::uint8_t> pencil(static_cast<std::size_t>(stride) * w, 255);
  draw_line(pencil.data(), w, w, stride, 2, 8, 24, 8, 1, fg, false, nullptr);
  bool pencil_other = false;
  bool pencil_fg = false;
  for (int x = 2; x <= 24; ++x) {
    const Color c = pixel_at(pencil.data(), stride, x, 8);
    if (c == fg) {
      pencil_fg = true;
    } else if (c != Color::white()) {
      pencil_other = true;
    }
  }
  expect(pencil_fg && !pencil_other, "the pencil stays a solid color");
}

void test_zoom_fit() {
  const double fitted = zoom_fit_ratio(592, 552, 800, 600);
  expect(fitted > 0.70 && fitted < 0.80, "800x600 in a padded viewport is about 0.74");
  expect(std::abs(fitted - snapped_zoom_step(fitted)) > 0.05, "fit is not a power-of-two step");
  expect(snapped_zoom_step(0.74) == 0.5 || snapped_zoom_step(0.74) == 1.0,
         "in and out still snap");
  expect(zoom_fit_ratio(100, 100, 800, 600) == 0.125, "fit clamps at the minimum zoom");
  expect(zoom_fit_ratio(4000, 4000, 100, 100) == 16.0, "fit clamps at the maximum zoom");
}

void test_composite_scale() {
  auto doc = Document::create(4, 2, Color::white());
  Layer& layer = doc->layers().active_layer();
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 4; ++x) {
      layer.set_pixel(x, y, x < 2 ? Color{200, 0, 0, 255} : Color{0, 0, 180, 255});
    }
  }
  std::vector<std::uint8_t> out(8, 0);
  LayerStack::CompositeWork work;
  doc->layers().composite_scaled(out.data(), 8, Rect{0, 0, 4, 2}, 2, 1, nullptr, -1, -1, {}, &work);
  expect(out[0] == 200 && out[2] == 0, "the left output sample is the red column");
  expect(out[4] == 0 && out[6] == 180, "the right output sample is the blue column");
  expect(work.samples == 2, "a 2x1 downsample samples two pixels");

  constexpr int kSide = 2000;
  constexpr int kOutW = 100;
  constexpr int kOutH = 80;
  auto big = Document::create(kSide, kSide, Color{180, 0, 0, 255});
  for (int i = 0; i < 3; ++i) {
    auto extra = std::make_unique<Layer>(kSide, kSide, Color{0, static_cast<std::uint8_t>(40 + i), 255, 255},
                                         "extra");
    big->layers().insert(big->layers().count(), std::move(extra));
  }
  const int layers = big->layers().count();
  std::vector<std::uint8_t> scaled(static_cast<std::size_t>(kOutW) * kOutH * 4, 0);
  LayerStack::CompositeWork big_work;
  const auto start = std::chrono::steady_clock::now();
  big->layers().composite_scaled(scaled.data(), kOutW * 4, Rect{0, 0, kSide, kSide}, kOutW, kOutH,
                                 nullptr, -1, -1, {}, &big_work);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const int expected = kOutW * kOutH * layers;
  expect(big_work.samples == expected, "zoomed-out composite samples output pixels, not the canvas");
  expect(big_work.samples < kSide * kSide, "sample count stays under one full-canvas pass");
  expect(elapsed < std::chrono::seconds(5), "a 2000px four-layer downsample stays under 5s");
}

void test_blur_region() {
  constexpr int kW = 80;
  constexpr int kH = 70;
  const int stride = kW * 4;
  std::vector<std::uint8_t> src(static_cast<std::size_t>(stride) * kH, 0);
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      std::uint8_t* p = src.data() + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
      p[0] = static_cast<std::uint8_t>(x * 3);
      p[1] = static_cast<std::uint8_t>(y * 3);
      p[2] = 40;
      p[3] = 255;
    }
  }
  std::vector<std::uint8_t> dest = src;
  const Rect region{8, 6, 40, 30};
  BlurWork work;
  box_blur_rect(src.data(), kW, kH, stride, dest.data(), stride, 2, region, &work);
  expect(work.pixels_written == region.w * region.h, "blur writes only the region");
  expect(work.scratch_bytes == box_blur_scratch_bytes(region.w, 2), "scratch follows the region span");
  bool outside = true;
  for (int y = 0; y < kH && outside; ++y) {
    for (int x = 0; x < kW; ++x) {
      if (region.contains(x, y)) {
        continue;
      }
      if (pixel_at(dest.data(), stride, x, y) != pixel_at(src.data(), stride, x, y)) {
        outside = false;
        break;
      }
    }
  }
  expect(outside, "pixels outside the blur region stay put");

  constexpr int kWide = 4000;
  std::vector<std::uint8_t> wide(static_cast<std::size_t>(kWide) * 8 * 4, 255);
  BlurWork wide_work;
  box_blur_rect(wide.data(), kWide, 8, kWide * 4, wide.data(), kWide * 4, 16, Rect{20, 1, 40, 4},
                &wide_work);
  expect(wide_work.scratch_bytes == box_blur_scratch_bytes(40, 16),
         "a narrow region on a wide layer does not size scratch to the canvas");
  expect(box_blur_scratch_bytes(4000, 16) < 8ull * 1024ull * 1024ull,
         "a full-width radius-16 blur scratch stays under 8 MB");

  constexpr int kBlur = 2000;
  std::vector<std::uint8_t> big(static_cast<std::size_t>(kBlur) * kBlur * 4, 200);
  std::vector<std::uint8_t> big_dest(big.size(), 0);
  const auto start = std::chrono::steady_clock::now();
  box_blur_rect(big.data(), kBlur, kBlur, kBlur * 4, big_dest.data(), kBlur * 4, 2,
                Rect{0, 0, kBlur, kBlur}, nullptr);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  expect(elapsed < std::chrono::seconds(5), "a 2000px blur stays under 5s");
}

void test_undo_patches() {
  Layer before(256, 256, Color::white(), "base");
  auto after = before.clone();
  after->set_pixel(3, 4, Color::black());
  auto dot = PixelPatchCommand::from_layers(before, *after, Rect{3, 4, 1, 1}, "Dot");
  expect(dot != nullptr && !dot->empty(), "a one-pixel edit records a patch");
  const std::size_t dot_bytes = dot->memory_bytes();
  expect(dot_bytes < 4096, "a one-pixel edit stores a small patch");
  expect(dot_bytes < static_cast<std::size_t>(256) * 256 * 4, "the patch is smaller than the canvas");

  Layer solid_before(128, 128, Color::white(), "solid");
  auto solid_after = solid_before.clone();
  solid_after->fill(Color::black());
  auto fill = PixelPatchCommand::from_layers(solid_before, *solid_after, Rect{0, 0, 128, 128}, "Fill");
  expect(fill != nullptr && fill->memory_bytes() < 8192, "a solid fill stores colors, not two buffers");

  History history(50);
  expect(history.byte_cap() == kDefaultUndoBytes, "the default undo cap is 256 MB");
  const std::size_t one = dot_bytes;
  history.set_byte_cap(one);
  history.commit_applied(std::move(dot));
  auto second = PixelPatchCommand::from_layers(before, *after, Rect{3, 4, 1, 1}, "Two");
  auto third = PixelPatchCommand::from_layers(before, *after, Rect{3, 4, 1, 1}, "Three");
  history.commit_applied(std::move(second));
  history.commit_applied(std::move(third));
  expect(history.count() == 1, "the byte cap drops older steps");
  expect(history.name_at(0) == "Three", "the newest step is the one that remains");
  expect(history.memory_bytes() <= one, "stored undo stays inside the cap");
}

std::string label_text(const std::string& xml, std::size_t label_pos) {
  const std::size_t gt = xml.find('>', label_pos);
  const std::size_t end = xml.find("</attribute>", label_pos);
  if (gt == std::string::npos || end == std::string::npos || end < gt) {
    return {};
  }
  return xml.substr(gt + 1, end - gt - 1);
}

char mnemonic_of(const std::string& text) {
  const std::size_t mark = text.find('_');
  if (mark == std::string::npos || mark + 1 >= text.size()) {
    return 0;
  }
  unsigned char ch = static_cast<unsigned char>(text[mark + 1]);
  if (ch >= 'a' && ch <= 'z') {
    ch = static_cast<unsigned char>(ch - 'a' + 'A');
  }
  return static_cast<char>(ch);
}

void test_menus_and_shortcuts() {
  const std::string root = REVIEW_R6_SOURCE;
  std::ifstream in(root + "/data/ui/menus.xml");
  const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  expect(!xml.empty(), "menus.xml was readable");
  expect(xml.find("Und_o") != std::string::npos, "Undo has a mnemonic");
  expect(xml.find("_Redo") != std::string::npos, "Redo has a mnemonic");
  expect(xml.find("Zoom _In") != std::string::npos, "Zoom In has a mnemonic");
  expect(xml.find("Zoom to Fi_t") != std::string::npos, "Zoom to Fit has a mnemonic");
  expect(xml.find("_Layers, History, and Colors") != std::string::npos,
         "the dock item names layers, history, and colors");
  expect(xml.find("_Canvas Size") != std::string::npos, "Canvas Size has a mnemonic");
  expect(xml.find("_Brightness / Contrast") != std::string::npos, "Brightness has a mnemonic");
  expect(xml.find("Dese_lect") != std::string::npos, "Deselect mnemonic is unchanged");
  expect(xml.find("Lo_wer Layer") != std::string::npos, "Lower Layer mnemonic is unchanged");

  struct Scope {
    std::string name;
    std::set<char> used;
  };
  std::vector<Scope> scopes{{"menubar", {}}};
  bool pending_title = false;
  bool pending_item = false;
  std::size_t pos = 0;
  while (pos < xml.size()) {
    const std::size_t sub = xml.find("<submenu>", pos);
    const std::size_t endsub = xml.find("</submenu>", pos);
    const std::size_t item = xml.find("<item>", pos);
    const std::size_t label = xml.find("name=\"label\"", pos);
    std::size_t next = xml.size();
    int which = -1;
    auto consider = [&](std::size_t at, int kind) {
      if (at != std::string::npos && at < next) {
        next = at;
        which = kind;
      }
    };
    consider(sub, 0);
    consider(endsub, 1);
    consider(item, 2);
    consider(label, 3);
    if (which < 0) {
      break;
    }
    if (which == 0) {
      pending_title = true;
      pos = next + 9;
      continue;
    }
    if (which == 1) {
      if (scopes.size() > 1) {
        scopes.pop_back();
      }
      pos = next + 11;
      continue;
    }
    if (which == 2) {
      pending_item = true;
      pos = next + 6;
      continue;
    }
    const std::string text = label_text(xml, next);
    const char key = mnemonic_of(text);
    if (pending_title) {
      if (key == 0) {
        expect(false, "a menu title is missing a mnemonic");
      } else if (!scopes.back().used.insert(key).second) {
        std::fprintf(stderr, "test_review_r6: duplicate mnemonic %c in %s (%s)\n", key,
                     scopes.back().name.c_str(), text.c_str());
        ++g_errors;
      }
      scopes.push_back({text, {}});
      pending_title = false;
    } else if (pending_item) {
      if (text != "(empty)") {
        if (key == 0) {
          std::fprintf(stderr, "test_review_r6: missing mnemonic on %s\n", text.c_str());
          ++g_errors;
        } else if (!scopes.back().used.insert(key).second) {
          std::fprintf(stderr, "test_review_r6: duplicate mnemonic %c in %s (%s)\n", key,
                       scopes.back().name.c_str(), text.c_str());
          ++g_errors;
        }
      }
      pending_item = false;
    }
    pos = next + 12;
  }

  int rows = 0;
  const ShortcutHelpRow* help = shortcut_help_rows(rows);
  bool hand = false;
  bool merged = false;
  bool paste_new = false;
  bool revert = false;
  bool space = false;
  bool nudge = false;
  bool finish = false;
  bool backspace = false;
  bool kept_u = false;
  bool kept_k = false;
  bool kept_q = false;
  bool kept_z = false;
  bool kept_j = false;
  for (int i = 0; i < rows; ++i) {
    const std::string action = help[i].action != nullptr ? help[i].action : "";
    const std::string keys = help[i].keys != nullptr ? help[i].keys : "";
    if (action == "Hand" && keys == "H") {
      hand = true;
    }
    if (action == "Copy merged" && keys == "Ctrl+Shift+C") {
      merged = true;
    }
    if (action == "Paste into New" && keys.find("Edit menu") != std::string::npos) {
      paste_new = true;
    }
    if (action == "Revert" && keys == "Ctrl+R") {
      revert = true;
    }
    if (keys.find("Space") != std::string::npos) {
      space = true;
    }
    if (keys.find("Arrow keys") != std::string::npos && keys.find("Shift") != std::string::npos) {
      nudge = true;
    }
    if (action.find("Finish polygon") != std::string::npos && keys.find("Enter") != std::string::npos) {
      finish = true;
    }
    if (action == "Delete selection" && keys.find("BackSpace") != std::string::npos) {
      backspace = true;
    }
    if (action.find("Rounded rectangle") != std::string::npos && keys == "U") {
      kept_u = true;
    }
    if (keys == "K / O") {
      kept_k = true;
    }
    if (keys == "G / Q") {
      kept_q = true;
    }
    if (keys == "E / Z") {
      kept_z = true;
    }
    if (keys == "L / R / J") {
      kept_j = true;
    }
  }
  expect(hand, "shortcut help lists Hand (H)");
  expect(merged, "shortcut help lists Copy merged");
  expect(paste_new, "Paste into New is listed without an invented accelerator");
  expect(revert, "shortcut help lists Revert");
  expect(space, "shortcut help lists Space");
  expect(nudge, "shortcut help lists arrow nudges");
  expect(finish, "shortcut help lists Enter");
  expect(backspace, "shortcut help lists BackSpace");
  expect(kept_u && kept_k && kept_q && kept_z && kept_j, "K J Z Q U shortcuts are unchanged");
}

}  // namespace

int main() {
  test_palette_and_wells();
  test_toolbox_catalog();
  test_patterns();
  test_zoom_fit();
  test_composite_scale();
  test_blur_region();
  test_undo_patches();
  test_menus_and_shortcuts();
  if (g_errors != 0) {
    std::fprintf(stderr, "test_review_r6: %d failure(s)\n", g_errors);
    return 1;
  }
  std::printf("test_review_r6: ok\n");
  return 0;
}
