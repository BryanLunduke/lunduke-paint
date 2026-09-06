// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/text.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {
namespace {

bool render_text_rgba(const std::string& text, const std::string& family, int size_pt, bool bold,
                      bool italic, Color color, std::vector<std::uint8_t>& out, int& out_w,
                      int& out_h) {
  out.clear();
  out_w = 0;
  out_h = 0;
  if (text.empty() || size_pt < 1) {
    return false;
  }
  auto measure = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, 8, 8);
  auto mcr = Cairo::Context::create(measure);
  PangoLayout* layout = pango_cairo_create_layout(mcr->cobj());
  pango_layout_set_text(layout, text.c_str(), -1);
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, family.c_str());
  pango_font_description_set_size(desc, size_pt * PANGO_SCALE);
  pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
  pango_font_description_set_style(desc, italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_width(layout, 40 * size_pt * PANGO_SCALE);
  pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  int tw = 0;
  int th = 0;
  pango_layout_get_pixel_size(layout, &tw, &th);
  if (tw < 1) {
    tw = 1;
  }
  if (th < 1) {
    th = 1;
  }
  auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, tw, th);
  auto cr = Cairo::Context::create(surface);
  cr->set_operator(Cairo::OPERATOR_SOURCE);
  cr->set_source_rgba(0, 0, 0, 0);
  cr->paint();
  cr->set_operator(Cairo::OPERATOR_OVER);
  if (color.a == 0) {
    cr->set_source_rgba(0, 0, 0, 1);
  } else {
    cr->set_source_rgba(color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
  }
  pango_cairo_update_layout(cr->cobj(), layout);
  pango_cairo_show_layout(cr->cobj(), layout);
  surface->flush();
  const int stride = surface->get_stride();
  const std::uint8_t* src = surface->get_data();
  out.assign(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4, 0);
  for (int y = 0; y < th; ++y) {
    const std::uint8_t* srow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    std::uint8_t* drow = out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tw) * 4;
    for (int x = 0; x < tw; ++x) {
      const std::uint8_t* p = srow + static_cast<std::size_t>(x) * 4;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      const int a = p[3];
      const int b = p[0];
      const int g = p[1];
      const int r = p[2];
#else
      const int a = p[0];
      const int r = p[1];
      const int g = p[2];
      const int b = p[3];
#endif
      std::uint8_t* d = drow + static_cast<std::size_t>(x) * 4;
      if (a == 0) {
        d[0] = d[1] = d[2] = d[3] = 0;
        continue;
      }
      if (color.a == 0) {
        d[0] = d[1] = d[2] = d[3] = 0;
        continue;
      }
      d[0] = static_cast<std::uint8_t>(r * 255 / a);
      d[1] = static_cast<std::uint8_t>(g * 255 / a);
      d[2] = static_cast<std::uint8_t>(b * 255 / a);
      d[3] = static_cast<std::uint8_t>(a);
    }
  }
  pango_font_description_free(desc);
  g_object_unref(layout);
  out_w = tw;
  out_h = th;
  return true;
}

}  // namespace

class TextTool : public Tool {
public:
  const char* id() const override { return "text"; }
  const char* name() const override { return "Text"; }
  char shortcut() const override { return 'T'; }
  const char* hint() const override {
    return "Text: click to type; Enter or click away rasterizes; Esc cancels";
  }
  bool is_stroking() const override { return editing_; }
  bool captures_keys() const override { return editing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent /*event*/) override {}
  void on_release(CanvasEvent /*event*/) override {}
  void on_cancel() override;
  bool on_commit() override;

private:
  void start_editor(int x, int y, unsigned button);
  void close_editor();
  void rasterize();

  bool editing_ = false;
  int x_ = 0;
  int y_ = 0;
  unsigned button_ = 1;
  std::string family_{"Sans"};
  int size_pt_ = 16;
  bool bold_ = false;
  bool italic_ = false;
  std::unique_ptr<Gtk::Window> popup_;
  Gtk::Entry* entry_{nullptr};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* TextTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* flabel = Gtk::manage(new Gtk::Label("Font"));
    auto* font = Gtk::manage(new Gtk::ComboBoxText());
    PangoFontMap* map = pango_cairo_font_map_get_default();
    PangoFontFamily** families = nullptr;
    int nfam = 0;
    pango_font_map_list_families(map, &families, &nfam);
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(std::max(0, nfam)));
    for (int i = 0; i < nfam; ++i) {
      const char* name = pango_font_family_get_name(families[i]);
      if (name != nullptr && name[0] != '\0') {
        names.emplace_back(name);
      }
    }
    g_free(families);
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      font->append(name);
    }
    if (names.empty()) {
      font->append("Sans");
      font->append("Serif");
      font->append("Monospace");
    }
    font->set_active_text(family_);
    if (font->get_active_row_number() < 0 && font->get_model()) {
      font->set_active(0);
      family_ = font->get_active_text();
    }
    font->signal_changed().connect([this, font]() { family_ = font->get_active_text(); });
    auto* slabel = Gtk::manage(new Gtk::Label("Size"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(6, 128);
    spin->set_increments(1, 8);
    spin->set_digits(0);
    spin->set_value(size_pt_);
    spin->signal_value_changed().connect([this, spin]() { size_pt_ = spin->get_value_as_int(); });
    auto* bold = Gtk::manage(new Gtk::CheckButton("Bold"));
    bold->set_active(bold_);
    bold->signal_toggled().connect([this, bold]() { bold_ = bold->get_active(); });
    auto* italic = Gtk::manage(new Gtk::CheckButton("Italic"));
    italic->set_active(italic_);
    italic->signal_toggled().connect([this, italic]() { italic_ = italic->get_active(); });
    options_->pack_start(*flabel, Gtk::PACK_SHRINK);
    options_->pack_start(*font, Gtk::PACK_SHRINK);
    options_->pack_start(*slabel, Gtk::PACK_SHRINK);
    options_->pack_start(*spin, Gtk::PACK_SHRINK);
    options_->pack_start(*bold, Gtk::PACK_SHRINK);
    options_->pack_start(*italic, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void TextTool::start_editor(int x, int y, unsigned button) {
  if (host_ == nullptr || !ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  // While editing_ is true the canvas draws the (empty) tool layer in place of
  // the active layer, so mirror the layer into it: without this the picture
  // appears to vanish behind the entry until the text is committed.
  host_->document().layers().copy_active_to_tool();
  x_ = x;
  y_ = y;
  button_ = button;
  editing_ = true;
  // A Gtk::WINDOW_POPUP is an override-redirect X11 window: the window manager
  // never manages it, so under X11/XFCE it does not get the keyboard focus and
  // everything typed went nowhere. Use a real toplevel, undecorated and hinted
  // as a utility window, parented to the main window, and present() it after
  // show_all() so the WM actually hands it X input focus.
  popup_ = std::make_unique<Gtk::Window>(Gtk::WINDOW_TOPLEVEL);
  popup_->set_decorated(false);
  popup_->set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
  popup_->set_skip_taskbar_hint(true);
  popup_->set_skip_pager_hint(true);
  popup_->set_accept_focus(true);
  popup_->set_focus_on_map(true);
  popup_->set_resizable(false);
  popup_->set_position(Gtk::WIN_POS_NONE);
  popup_->set_border_width(2);
  if (Gtk::Window* parent = host_->host_window()) {
    popup_->set_transient_for(*parent);
  }
  entry_ = Gtk::manage(new Gtk::Entry());
  entry_->set_width_chars(16);
  entry_->signal_activate().connect([this]() { on_commit(); });
  entry_->signal_key_press_event().connect(
      [this](GdkEventKey* event) {
        if (event != nullptr && event->keyval == GDK_KEY_Escape) {
          on_cancel();
          return true;
        }
        return false;
      },
      false);
  popup_->add(*entry_);
  int sx = 0;
  int sy = 0;
  const bool placed = host_->canvas_to_screen(x, y, sx, sy);
  if (placed) {
    popup_->move(sx, sy);
  }
  popup_->show_all();
  if (placed) {
    // xfwm4 can re-place a utility window on map; put it back on the click.
    popup_->move(sx, sy);
  }
  popup_->present();
  entry_->grab_focus();
  if (host_ != nullptr) {
    host_->show_status_hint("Text: type, then Enter to stamp");
  }
}

void TextTool::close_editor() {
  entry_ = nullptr;
  popup_.reset();
  editing_ = false;
  if (host_ != nullptr) {
    host_->document().layers().clear_tool_layer();
    host_->invalidate_canvas(Rect{});
  }
}

void TextTool::rasterize() {
  if (host_ == nullptr || entry_ == nullptr) {
    close_editor();
    return;
  }
  const std::string text = entry_->get_text();
  close_editor();
  if (text.empty()) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  Document& doc = host_->document();
  std::vector<std::uint8_t> rgba;
  int tw = 0;
  int th = 0;
  if (!render_text_rgba(text, family_, size_pt_, bold_, italic_, stroke_color(button_), rgba, tw,
                         th)) {
    return;
  }
  doc.layers().copy_active_to_tool();
  Layer& tool = doc.layers().tool_layer();
  Rect dirty{};
  blit_rgba_buffer(tool.pixels(), tool.width(), tool.height(), tool.stride(), x_, y_, rgba.data(),
                   tw, th, tw * 4, true, &dirty);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty, doc.selection());
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), tool, dirty, "Text",
                                            doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty);
  }
}

void TextTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  if (editing_) {
    on_commit();
    return;
  }
  start_editor(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
               event.button);
}

bool TextTool::on_commit() {
  if (!editing_) {
    return false;
  }
  rasterize();
  return true;
}

void TextTool::on_cancel() {
  if (!editing_) {
    return;
  }
  close_editor();
}

Tool* create_text_tool() {
  return new TextTool();
}

}  // namespace lundukepaint
