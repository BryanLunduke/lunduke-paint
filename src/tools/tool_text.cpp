// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/rail_options.hpp"

#include "doc/document.hpp"
#include "raster/text.hpp"
#include "raster/text_box.hpp"

#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>
#include <pango/pangocairo.h>
#include <sigc++/connection.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {
namespace {

void paint_straight_rgba(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double zoom,
                         const std::uint8_t* rgba, int width, int height) {
  if (rgba == nullptr || width < 1 || height < 1 || zoom <= 0.0) {
    return;
  }
  auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, width, height);
  std::uint8_t* dst = surface->get_data();
  const int stride = surface->get_stride();
  for (int row = 0; row < height; ++row) {
    const std::uint8_t* srow = rgba + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4;
    std::uint8_t* drow = dst + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
    for (int col = 0; col < width; ++col) {
      const std::uint8_t* s = srow + static_cast<std::size_t>(col) * 4;
      std::uint8_t* d = drow + static_cast<std::size_t>(col) * 4;
      const int a = s[3];
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      d[0] = static_cast<std::uint8_t>((s[2] * a + 127) / 255);
      d[1] = static_cast<std::uint8_t>((s[1] * a + 127) / 255);
      d[2] = static_cast<std::uint8_t>((s[0] * a + 127) / 255);
      d[3] = s[3];
#else
      d[0] = s[3];
      d[1] = static_cast<std::uint8_t>((s[0] * a + 127) / 255);
      d[2] = static_cast<std::uint8_t>((s[1] * a + 127) / 255);
      d[3] = static_cast<std::uint8_t>((s[2] * a + 127) / 255);
#endif
    }
  }
  surface->mark_dirty();
  cr->save();
  cr->translate(x, y);
  cr->scale(zoom, zoom);
  auto pattern = Cairo::SurfacePattern::create(surface);
  pattern->set_filter(Cairo::FILTER_NEAREST);
  cr->set_source(pattern);
  cr->paint();
  cr->restore();
}

}  // namespace

class TextTool : public Tool {
public:
  ~TextTool() override { blink_.disconnect(); }

  const char* id() const override { return "text"; }
  const char* name() const override { return "Text"; }
  char shortcut() const override { return 'T'; }
  const char* hint() const override {
    return "Text: click for a box; drag handles to resize, the border to move; "
           "click away or Enter stamps; Esc cancels";
  }
  bool is_stroking() const override { return editing_; }
  bool uses_tool_layer() const override { return false; }
  bool captures_keys() const override { return editing_; }
  bool has_uncommitted_preview() const override { return editing_ && !state_.text.empty(); }
  bool paste_text(const std::string& utf8) override;
  Gtk::Widget* options_widget() override;
  void draw_overlay(const Cairo::RefPtr<Cairo::Context>& cr, int origin_x, int origin_y,
                    double zoom) override;
  bool on_key(unsigned keyval, unsigned modifiers, const std::string& text) override;
  void on_document_changed() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;
  bool paint_recovery_overlay(int layer_index, std::uint8_t* pixels, int width, int height,
                             int stride) override;

private:
  enum class Drag { None, Move, Resize };

  void begin_box(int x, int y, unsigned button);
  void close_box();
  void sync_overlay();
  void apply_style();
  void rebuild_pixels();
  void relayout_caret();
  void invalidate_box(const Rect& box) const;
  Rect chrome_rect(const Rect& box) const;
  void start_blink();
  void paste_clipboard();

  bool editing_ = false;
  unsigned button_ = 1;
  TextBoxState state_{};
  TextBoxMetrics metrics_{};
  std::vector<std::uint8_t> pixels_;
  int pix_w_ = 0;
  int pix_h_ = 0;
  bool cursor_on_ = true;
  Drag drag_ = Drag::None;
  TextBoxHit drag_hit_ = TextBoxHit::Outside;
  Rect drag_start_{};
  int grab_x_ = 0;
  int grab_y_ = 0;
  std::string family_{"Sans"};
  int size_pt_ = 16;
  bool bold_ = false;
  bool italic_ = false;
  sigc::connection blink_;
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* TextTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    prepare_rail_box(*options_);
    auto* flabel = Gtk::manage(new Gtk::Label("Font"));
    flabel->set_halign(Gtk::ALIGN_START);
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
    configure_rail_combo(*font);
    font->signal_changed().connect([this, font]() {
      family_ = font->get_active_text();
      apply_style();
    });
    auto* slabel = Gtk::manage(new Gtk::Label("Size"));
    slabel->set_halign(Gtk::ALIGN_START);
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(6, 128);
    spin->set_increments(1, 8);
    spin->set_digits(0);
    spin->set_value(size_pt_);
    configure_rail_spin(*spin);
    spin->signal_value_changed().connect([this, spin]() {
      size_pt_ = spin->get_value_as_int();
      apply_style();
    });
    auto* bold = Gtk::manage(new Gtk::CheckButton("Bold"));
    bold->set_active(bold_);
    configure_rail_check(*bold);
    bold->signal_toggled().connect([this, bold]() {
      bold_ = bold->get_active();
      apply_style();
    });
    auto* italic = Gtk::manage(new Gtk::CheckButton("Italic"));
    italic->set_active(italic_);
    configure_rail_check(*italic);
    italic->signal_toggled().connect([this, italic]() {
      italic_ = italic->get_active();
      apply_style();
    });
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

void TextTool::apply_style() {
  state_.family = family_;
  state_.size_pt = std::max(1, size_pt_);
  state_.bold = bold_;
  state_.italic = italic_;
  if (!editing_) {
    return;
  }
  rebuild_pixels();
  invalidate_box(state_.box);
}

void TextTool::on_document_changed() {
  if (!editing_ || host_ == nullptr) {
    return;
  }
  const Color color = stroke_color(button_);
  if (color == state_.color) {
    return;
  }
  state_.color = color;
  rebuild_pixels();
  invalidate_box(state_.box);
}

Rect TextTool::chrome_rect(const Rect& box) const {
  const double zoom = host_ != nullptr ? host_->canvas_zoom() : 1.0;
  const int margin = text_box_handle_radius(zoom) + text_box_border_slop(zoom) + 2;
  return Rect{box.x - margin, box.y - margin, box.w + margin * 2, box.h + margin * 2};
}

void TextTool::invalidate_box(const Rect& box) const {
  if (host_ != nullptr) {
    host_->invalidate_canvas(chrome_rect(box));
  }
}

void TextTool::rebuild_pixels() {
  pixels_.clear();
  pix_w_ = 0;
  pix_h_ = 0;
  render_text_box(state_, pixels_, pix_w_, pix_h_, &metrics_);
}

void TextTool::relayout_caret() {
  layout_text_box(state_, metrics_);
}

void TextTool::start_blink() {
  blink_.disconnect();
  cursor_on_ = true;
  blink_ = Glib::signal_timeout().connect(
      [this]() {
        if (!editing_) {
          return false;
        }
        cursor_on_ = !cursor_on_;
        invalidate_box(state_.box);
        return true;
      },
      530);
}

void TextTool::begin_box(int x, int y, unsigned button) {
  if (host_ == nullptr || !ensure_editable()) {
    return;
  }
  if (!commit_float_or_stop()) {
    return;
  }
  button_ = button;
  state_ = {};
  state_.family = family_;
  state_.size_pt = std::max(1, size_pt_);
  state_.bold = bold_;
  state_.italic = italic_;
  state_.color = stroke_color(button_);
  state_.text.clear();
  state_.cursor = 0;
  TextBoxMetrics probe;
  layout_text_box(state_, probe);
  const int height = std::max(kTextBoxMinSize, probe.cursor_h + kTextBoxPad * 2);
  const int width = std::max(160, state_.size_pt * 10);
  state_.box = Rect{x, y, width, height};
  editing_ = true;
  drag_ = Drag::None;
  rebuild_pixels();
  start_blink();
  sync_overlay();
  invalidate_box(state_.box);
  host_->show_status_hint("Text: type in the box; click away or Enter to stamp");
}

void TextTool::sync_overlay() {
  if (host_ == nullptr) {
    return;
  }
  host_->document().set_unsaved_overlay(editing_ && !state_.text.empty());
}

void TextTool::close_box() {
  const bool was = editing_;
  const Rect box = state_.box;
  editing_ = false;
  if (was && host_ != nullptr) {
    host_->document().set_unsaved_overlay(false);
  }
  drag_ = Drag::None;
  blink_.disconnect();
  pixels_.clear();
  pix_w_ = 0;
  pix_h_ = 0;
  if (was) {
    invalidate_box(box);
  }
}

void TextTool::paste_clipboard() {
  auto clipboard = Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD);
  if (!clipboard) {
    return;
  }
  paste_text(clipboard->wait_for_text());
}

bool TextTool::paste_text(const std::string& utf8) {
  if (!editing_ || utf8.empty()) {
    return false;
  }
  text_box_insert(state_, utf8);
  cursor_on_ = true;
  rebuild_pixels();
  invalidate_box(state_.box);
  sync_overlay();
  return true;
}

bool TextTool::on_key(unsigned keyval, unsigned modifiers, const std::string& text) {
  if (!editing_) {
    return false;
  }
  if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab || keyval == GDK_KEY_KP_Tab) {
    return false;
  }
  if ((modifiers & Modifier::Ctrl) != 0 && (modifiers & Modifier::Alt) == 0) {
    if (keyval == GDK_KEY_v || keyval == GDK_KEY_V) {
      paste_clipboard();
      return true;
    }
    return false;
  }
  if (keyval == GDK_KEY_Insert && (modifiers & Modifier::Shift) != 0) {
    paste_clipboard();
    return true;
  }
  bool edited = false;
  switch (keyval) {
    case GDK_KEY_BackSpace:
      text_box_backspace(state_);
      edited = true;
      break;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
      text_box_delete_forward(state_);
      edited = true;
      break;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
      text_box_move_left(state_);
      break;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
      text_box_move_right(state_);
      break;
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
      text_box_move_up(state_);
      break;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
      text_box_move_down(state_);
      break;
    case GDK_KEY_Home:
    case GDK_KEY_KP_Home:
      text_box_move_line_start(state_);
      break;
    case GDK_KEY_End:
    case GDK_KEY_KP_End:
      text_box_move_line_end(state_);
      break;
    default:
      if (text.empty() || static_cast<unsigned char>(text[0]) < 0x20) {
        return true;
      }
      text_box_insert(state_, text);
      edited = true;
      break;
  }
  cursor_on_ = true;
  if (edited) {
    rebuild_pixels();
  } else {
    relayout_caret();
  }
  sync_overlay();
  invalidate_box(state_.box);
  return true;
}

void TextTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (!editing_) {
    begin_box(x, y, event.button);
    return;
  }
  const double zoom = host_ != nullptr ? host_->canvas_zoom() : 1.0;
  const TextBoxHit hit = hit_test_text_box(state_.box, x, y, zoom);
  switch (text_box_press_action(hit)) {
    case TextBoxPress::Commit:
      on_commit();
      return;
    case TextBoxPress::PlaceCursor:
      state_.cursor = text_box_index_at(state_, x, y);
      cursor_on_ = true;
      drag_ = Drag::None;
      relayout_caret();
      invalidate_box(state_.box);
      return;
    case TextBoxPress::Move:
      drag_ = Drag::Move;
      drag_hit_ = hit;
      drag_start_ = state_.box;
      grab_x_ = x;
      grab_y_ = y;
      return;
    case TextBoxPress::Resize:
      drag_ = Drag::Resize;
      drag_hit_ = hit;
      drag_start_ = state_.box;
      grab_x_ = x;
      grab_y_ = y;
      return;
  }
}

void TextTool::on_motion(CanvasEvent event) {
  if (!editing_ || drag_ == Drag::None) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  const Rect before = state_.box;
  if (drag_ == Drag::Move) {
    state_.box = drag_start_;
    move_text_box(state_.box, x - grab_x_, y - grab_y_);
    invalidate_box(before);
    invalidate_box(state_.box);
    return;
  }
  resize_text_box(state_.box, drag_hit_, x, y, drag_start_, grab_x_, grab_y_);
  if (state_.box.w != before.w || state_.box.h != before.h) {
    rebuild_pixels();
  }
  invalidate_box(before);
  invalidate_box(state_.box);
}

void TextTool::on_release(CanvasEvent /*event*/) {
  drag_ = Drag::None;
}

bool TextTool::on_commit() {
  if (!editing_ || host_ == nullptr) {
    return false;
  }
  if (state_.text.empty()) {
    close_box();
    return true;
  }
  // Leave the box up when the layer is locked so Cancel on quit can keep it.
  if (host_->document().active_locked()) {
    host_->document().notify_blocked("Unlock the layer to place the text");
    return false;
  }
  if (!commit_text_box(host_->document(), state_)) {
    return false;
  }
  close_box();
  return true;
}

bool TextTool::paint_recovery_overlay(int layer_index, std::uint8_t* pixels, int width, int height,
                                     int stride) {
  if (!editing_ || state_.text.empty() || host_ == nullptr || pixels == nullptr || width < 1 ||
      height < 1) {
    return false;
  }
  Document& doc = host_->document();
  if (layer_index != doc.layers().active_index()) {
    return false;
  }
  if (pixels_.empty() || pix_w_ < 1 || pix_h_ < 1) {
    rebuild_pixels();
  }
  if (pixels_.empty() || pix_w_ < 1 || pix_h_ < 1) {
    return false;
  }
  const Layer& layer = doc.layers().active_layer();
  blit_rgba_buffer(pixels, width, height, stride, state_.box.x + kTextBoxPad - layer.offset_x(),
                   state_.box.y + kTextBoxPad - layer.offset_y(), pixels_.data(), pix_w_, pix_h_,
                   pix_w_ * 4, true, nullptr);
  return true;
}

void TextTool::on_cancel() {
  if (!editing_) {
    return;
  }
  close_box();
}

void TextTool::draw_overlay(const Cairo::RefPtr<Cairo::Context>& cr, int origin_x, int origin_y,
                            double zoom) {
  if (!editing_ || !cr || zoom <= 0.0) {
    return;
  }
  const Rect& box = state_.box;
  if (!pixels_.empty() && pix_w_ > 0 && pix_h_ > 0) {
    const double tx = origin_x + static_cast<double>(box.x + kTextBoxPad) * zoom;
    const double ty = origin_y + static_cast<double>(box.y + kTextBoxPad) * zoom;
    paint_straight_rgba(cr, tx, ty, zoom, pixels_.data(), pix_w_, pix_h_);
  }

  const double x = origin_x + box.x * zoom + 0.5;
  const double y = origin_y + box.y * zoom + 0.5;
  const double w = box.w * zoom;
  const double h = box.h * zoom;
  cr->save();
  cr->set_line_width(1.0);
  cr->set_source_rgb(1.0, 1.0, 1.0);
  cr->rectangle(x, y, w, h);
  cr->stroke();
  cr->set_source_rgb(0.05, 0.05, 0.05);
  std::vector<double> dash{3.0, 2.0};
  cr->set_dash(dash, 0.0);
  cr->rectangle(x, y, w, h);
  cr->stroke();
  cr->restore();

  int xs[8];
  int ys[8];
  xs[0] = box.x;
  ys[0] = box.y;
  xs[1] = box.x + box.w / 2;
  ys[1] = box.y;
  xs[2] = box.x + box.w;
  ys[2] = box.y;
  xs[3] = box.x + box.w;
  ys[3] = box.y + box.h / 2;
  xs[4] = box.x + box.w;
  ys[4] = box.y + box.h;
  xs[5] = box.x + box.w / 2;
  ys[5] = box.y + box.h;
  xs[6] = box.x;
  ys[6] = box.y + box.h;
  xs[7] = box.x;
  ys[7] = box.y + box.h / 2;
  cr->save();
  cr->set_line_width(1.0);
  for (int i = 0; i < 8; ++i) {
    const double hx = origin_x + xs[i] * zoom;
    const double hy = origin_y + ys[i] * zoom;
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->rectangle(hx - 3.5, hy - 3.5, 7.0, 7.0);
    cr->fill_preserve();
    cr->set_source_rgb(0.1, 0.1, 0.1);
    cr->stroke();
  }
  cr->restore();

  if (cursor_on_) {
    const double cx = origin_x + static_cast<double>(box.x + kTextBoxPad + metrics_.cursor_x) * zoom;
    const double cy = origin_y + static_cast<double>(box.y + kTextBoxPad + metrics_.cursor_y) * zoom;
    const double ch = std::max(zoom, static_cast<double>(metrics_.cursor_h) * zoom);
    cr->save();
    cr->set_line_width(3.0);
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->move_to(cx, cy);
    cr->line_to(cx, cy + ch);
    cr->stroke();
    cr->set_line_width(1.0);
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->move_to(cx, cy);
    cr->line_to(cx, cy + ch);
    cr->stroke();
    cr->restore();
  }
}

Tool* create_text_tool() {
  return new TextTool();
}

}  // namespace lundukepaint
