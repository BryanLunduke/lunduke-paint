// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/document.hpp"

#include "doc/commands_layers.hpp"
#include "doc/commands_pixels.hpp"
#include "doc/layer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

namespace {

std::uint64_t make_recovery_id() {
  std::uint64_t id = 0;
  const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    if (::read(fd, &id, sizeof(id)) != static_cast<ssize_t>(sizeof(id))) {
      id = 0;
    }
    ::close(fd);
  }
  if (id == 0) {
    static std::uint64_t seq = 1;
    id = seq++;
  }
  return id;
}

}  // namespace

namespace lundukepaint {

Document::Document(int width, int height, Color background, std::string layer_name)
    : width_(width), height_(height), recovery_id_(make_recovery_id()), canvas_bg_(background) {
  layers_.reset(width_, height_, background, std::move(layer_name));
}

std::unique_ptr<Document> Document::create(int width, int height, Color background,
                                           std::string layer_name) {
  return std::unique_ptr<Document>(new Document(width, height, background, std::move(layer_name)));
}

void Document::set_dirty(bool dirty) {
  if (dirty) {
    history_.invalidate_saved();
  } else {
    history_.mark_saved();
  }
  if (dirty_ == dirty) {
    return;
  }
  dirty_ = dirty;
  notify_changed();
}

void Document::mark_clean() {
  if (unsaved_overlay_) {
    return;
  }
  set_dirty(false);
}

void Document::set_unsaved_overlay(bool on) {
  if (unsaved_overlay_ == on) {
    return;
  }
  unsaved_overlay_ = on;
  notify_changed();
}

namespace {

void bake_group_appearance(const OraNode& node, float opacity, bool visible, LayerStack& layers) {
  if (!node.is_stack) {
    if (node.layer_index < 0 || node.layer_index >= layers.count()) {
      return;
    }
    Layer& layer = layers.at(node.layer_index);
    float next = layer.opacity() * opacity;
    if (next < 0.0f) {
      next = 0.0f;
    }
    if (next > 1.0f) {
      next = 1.0f;
    }
    layer.set_opacity(next);
    if (!visible) {
      layer.set_visible(false);
    }
    return;
  }
  const float child_opacity = opacity * node.opacity;
  const bool child_visible = visible && node.visible;
  for (const OraNode& child : node.children) {
    bake_group_appearance(child, child_opacity, child_visible, layers);
  }
}

}  // namespace

void Document::set_ora_stack(OraNode node) {
  ora_stack_ = std::move(node);
  has_ora_stack_ = true;
  layers_.set_ora_stack(&ora_stack_);
}

void Document::clear_ora_stack() {
  if (has_ora_stack_) {
    bake_group_appearance(ora_stack_, 1.0f, true, layers_);
  }
  has_ora_stack_ = false;
  ora_stack_ = {};
  layers_.set_ora_stack(nullptr);
}

bool Document::set_active_layer(int index) {
  if (index < 0 || index >= layers_.count()) {
    return false;
  }
  if (index == layers_.active_index()) {
    return true;
  }
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  layers_.set_active_index(index);
  notify_changed();
  return true;
}

const OraNode* Document::ora_stack() const {
  return has_ora_stack_ ? &ora_stack_ : nullptr;
}

void Document::note_history_dirty() {
  const bool dirty = !history_.matches_saved();
  if (dirty_ == dirty) {
    return;
  }
  dirty_ = dirty;
  notify_changed();
}

void Document::set_foreground(Color color) {
  if (fg_ == color) {
    return;
  }
  fg_ = color;
  notify_changed();
}

void Document::set_background(Color color) {
  if (bg_ == color) {
    return;
  }
  bg_ = color;
  notify_changed();
}

void Document::swap_colors() {
  std::swap(fg_, bg_);
  notify_changed();
}

void Document::reset_colors() {
  fg_ = Color::black();
  bg_ = Color::white();
  notify_changed();
}

void Document::commit(std::unique_ptr<Command> command) {
  if (!command) {
    return;
  }
  const Rect dirty = command->dirty_rect();
  history_.commit(*this, std::move(command));
  note_history_dirty();
  notify_invalidated(dirty);
  notify_changed();
}

Rect Document::undo() {
  if (!history_.can_undo()) {
    return {};
  }
  const Rect dirty = history_.undo(*this);
  note_history_dirty();
  notify_invalidated(dirty);
  notify_changed();
  return dirty;
}

Rect Document::redo() {
  if (!history_.can_redo()) {
    return {};
  }
  const Rect dirty = history_.redo(*this);
  note_history_dirty();
  notify_invalidated(dirty);
  notify_changed();
  return dirty;
}

Rect Document::jump_history(int target) {
  if (target == history_.index()) {
    return {};
  }
  const Rect dirty = history_.jump_to(*this, target);
  note_history_dirty();
  notify_invalidated(dirty);
  notify_changed();
  return dirty;
}

void Document::notify_invalidated(Rect rect) {
  if (on_invalidated_) {
    on_invalidated_(rect);
  }
}

void Document::notify_changed() {
  if (on_changed_) {
    on_changed_();
  }
}

void Document::notify_selection() {
  if (on_selection_) {
    on_selection_();
  }
}

void Document::notify_blocked(const char* message) {
  if (on_blocked_ && message != nullptr && message[0] != '\0') {
    on_blocked_(message);
  }
}


void Document::select_all() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return;
  }
  selection_.select_all(width_, height_);
  notify_invalidated({0, 0, width_, height_});
  notify_changed();
}

void Document::deselect() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return;
  }
  if (selection_.empty()) {
    return;
  }
  const Rect dirty = selection_.bounds().empty() ? Rect{0, 0, width_, height_}
                                                 : selection_.bounds();
  selection_.clear();
  notify_invalidated(dirty);
  notify_changed();
}

void Document::invert_selection() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return;
  }
  selection_.invert(width_, height_);
  notify_invalidated({0, 0, width_, height_});
  notify_changed();
}

bool Document::try_commit_floating(const char* name) {
  if (!selection_.floating()) {
    return true;
  }
  return commit_floating(name);
}

bool Document::commit_floating(const char* name) {
  if (!selection_.floating()) {
    return false;
  }
  int index = selection_.source_layer();
  if (index < 0 || index >= layers_.count()) {
    index = layers_.active_index();
  }
  if (index < 0 || index >= layers_.count() || layers_.at(index).locked()) {
    return false;
  }
  const SelectionState selection_before = selection_.capture();
  Layer& layer = layers_.at(index);
  const int ox = layer.offset_x();
  const int oy = layer.offset_y();
  const Rect layer_bounds{0, 0, layer.width(), layer.height()};
  Layer before(layer.width(), layer.height(), Color::transparent(), "before");
  before.copy_from(layer);
  before.set_offset(ox, oy);
  Rect dirty{};
  auto note = [&](int lx, int ly) {
    if (dirty.empty()) {
      dirty = {lx, ly, 1, 1};
      return;
    }
    const int x0 = std::min(dirty.x, lx);
    const int y0 = std::min(dirty.y, ly);
    const int x1 = std::max(dirty.x2(), lx + 1);
    const int y1 = std::max(dirty.y2(), ly + 1);
    dirty = {x0, y0, x1 - x0, y1 - y0};
  };
  if (!selection_.copy_mode()) {
    const Rect origin_canvas = selection_.origin_rect();
    for (int y = 0; y < origin_canvas.h; ++y) {
      for (int x = 0; x < origin_canvas.w; ++x) {
        if (!selection_.float_covers(x, y)) {
          continue;
        }
        const int lx = origin_canvas.x - ox + x;
        const int ly = origin_canvas.y - oy + y;
        if (!layer_bounds.contains(lx, ly)) {
          continue;
        }
        layer.set_pixel(lx, ly, Color::transparent());
        note(lx, ly);
      }
    }
  }
  const Rect float_layer{selection_.float_x() - ox, selection_.float_y() - oy, selection_.float_w(),
                         selection_.float_h()};
  blit_rgba(layer, float_layer.x, float_layer.y, selection_.float_pixels(), selection_.float_w(),
            selection_.float_h(), selection_.float_w() * 4, selection_.transparent_move(),
            selection_.float_coverage());
  dirty = rect_union(dirty, rect_intersect(float_layer, layer_bounds));
  const Rect kept = selection_.float_rect();
  const bool transparent = selection_.transparent_move();
  std::vector<std::uint8_t> kept_mask;
  if (selection_.has_float_coverage() && selection_.float_coverage() != nullptr) {
    const std::size_t n = static_cast<std::size_t>(selection_.float_w()) *
                          static_cast<std::size_t>(selection_.float_h());
    kept_mask.assign(selection_.float_coverage(), selection_.float_coverage() + n);
  }
  selection_.drop_float();
  if (!kept.empty()) {
    if (!kept_mask.empty()) {
      selection_.set_mask(kept, std::move(kept_mask));
    } else {
      selection_.set_rect(kept);
    }
    selection_.set_transparent_move(transparent);
  } else {
    selection_.clear();
  }
  auto cmd = PixelPatchCommand::from_layers(before, layer, dirty, name ? name : "Move selection", index);
  if (cmd && !cmd->empty()) {
    cmd->set_selection_change(selection_before, selection_.capture());
    commit(std::move(cmd));
  } else {
    if (!dirty.empty()) {
      dirty.x += ox;
      dirty.y += oy;
    }
    notify_invalidated(dirty);
    notify_changed();
  }
  return true;
}

void Document::delete_selection() {
  if (selection_.empty()) {
    return;
  }
  int index = selection_.source_layer();
  if (index < 0 || index >= layers_.count()) {
    index = layers_.active_index();
  }
  if (index < 0 || index >= layers_.count() || layers_.at(index).locked()) {
    notify_blocked("Layer is locked");
    return;
  }
  Layer& layer = layers_.at(index);
  const int ox = layer.offset_x();
  const int oy = layer.offset_y();
  Layer before(layer.width(), layer.height(), Color::transparent(), "before");
  before.copy_from(layer);
  before.set_offset(ox, oy);
  Rect dirty{};
  if (selection_.floating()) {
    if (!selection_.copy_mode()) {
      const Rect origin_canvas = selection_.origin_rect();
      const Rect layer_bounds{0, 0, layer.width(), layer.height()};
      for (int y = 0; y < origin_canvas.h; ++y) {
        for (int x = 0; x < origin_canvas.w; ++x) {
          if (!selection_.float_covers(x, y)) {
            continue;
          }
          const int lx = origin_canvas.x - ox + x;
          const int ly = origin_canvas.y - oy + y;
          if (!layer_bounds.contains(lx, ly)) {
            continue;
          }
          layer.set_pixel(lx, ly, Color::transparent());
          if (dirty.empty()) {
            dirty = {lx, ly, 1, 1};
          } else {
            const int x0 = std::min(dirty.x, lx);
            const int y0 = std::min(dirty.y, ly);
            const int x1 = std::max(dirty.x2(), lx + 1);
            const int y1 = std::max(dirty.y2(), ly + 1);
            dirty = {x0, y0, x1 - x0, y1 - y0};
          }
        }
      }
    }
    selection_.clear();
  } else {
    fill_selection(layer, selection_, Color::transparent(), &dirty);
  }
  auto cmd = PixelPatchCommand::from_layers(before, layer, dirty, "Delete", index);
  if (cmd && !cmd->empty()) {
    commit(std::move(cmd));
  } else {
    if (!dirty.empty()) {
      dirty.x += ox;
      dirty.y += oy;
    }
    notify_invalidated(dirty);
    notify_changed();
  }
}

void Document::duplicate_selection() {
  if (selection_.empty()) {
    return;
  }
  if (selection_.floating() && !try_commit_floating("Duplicate")) {
    notify_blocked("Unlock the layer to place the selection");
    return;
  }
  if (selection_.empty() || selection_.inverted()) {
    return;
  }
  if (!selection_.lift(layers_.active_layer(), layers_.active_index())) {
    return;
  }
  selection_.set_copy_mode(true);
  selection_.move_float(selection_.float_x() + 8, selection_.float_y() + 8);
  notify_invalidated(selection_.dirty_union());
  notify_changed();
}

void Document::paste_floating(int x, int y, int w, int h, std::vector<std::uint8_t> rgba) {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return;
  }
  selection_.set_float_pixels(x, y, w, h, std::move(rgba));
  selection_.set_source_layer(layers_.active_index());
  notify_invalidated(selection_.dirty_union());
  notify_changed();
}

void Document::replace_active_buffer(int width, int height, const std::uint8_t* rgba, int stride) {
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  width_ = width;
  height_ = height;
  layers_.replace_active(width_, height_, rgba, stride);
  layers_.resize_scratch(width_, height_);
  selection_.clear();
}

void Document::replace_stack(int width, int height, std::vector<std::unique_ptr<Layer>> layers,
                            int active_index) {
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  width_ = width;
  height_ = height;
  layers_.replace_stack(width_, height_, std::move(layers), active_index);
  selection_.clear();
}

bool Document::active_locked() const {
  return layers_.count() > 0 && layers_.active_layer().locked();
}

std::vector<LayerSnapshot> Document::snapshot_layers() const {
  std::vector<LayerSnapshot> out;
  out.reserve(static_cast<std::size_t>(layers_.count()));
  for (int i = 0; i < layers_.count(); ++i) {
    out.push_back(snapshot_layer(layers_.at(i)));
  }
  return out;
}

bool Document::add_layer() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  LayerSnapshot snap;
  snap.name = layers_.next_layer_name();
  snap.visible = true;
  snap.locked = false;
  snap.opacity = 1.0f;
  snap.blend = BlendMode::Normal;
  snap.width = width_;
  snap.height = height_;
  snap.pixels.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0);
  commit(std::make_unique<AddLayerCommand>(layers_.active_index() + 1, std::move(snap)));
  return true;
}

bool Document::duplicate_layer() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  if (layers_.count() < 1) {
    return false;
  }
  commit(std::make_unique<DuplicateLayerCommand>(layers_.active_index()));
  return true;
}

bool Document::delete_layer() {
  if (layers_.count() <= 1) {
    return false;
  }
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  const int idx = layers_.active_index();
  commit(std::make_unique<DeleteLayerCommand>(idx, snapshot_layer(layers_.at(idx))));
  return true;
}

bool Document::raise_layer() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  const int idx = layers_.active_index();
  if (idx + 1 >= layers_.count()) {
    return false;
  }
  commit(std::make_unique<MoveLayerCommand>(idx, idx + 1, "Raise layer"));
  return true;
}

bool Document::lower_layer() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  const int idx = layers_.active_index();
  if (idx <= 0) {
    return false;
  }
  commit(std::make_unique<MoveLayerCommand>(idx, idx - 1, "Lower layer"));
  return true;
}

bool Document::move_layer(int from, int to) {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  if (from < 0 || to < 0 || from >= layers_.count() || to >= layers_.count() || from == to) {
    return false;
  }
  commit(std::make_unique<MoveLayerCommand>(from, to, "Reorder layer"));
  return true;
}

bool Document::merge_down() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  const int idx = layers_.active_index();
  if (idx <= 0) {
    return false;
  }
  commit(std::make_unique<MergeDownCommand>(idx, snapshot_layer(layers_.at(idx - 1)),
                                            snapshot_layer(layers_.at(idx))));
  return true;
}

bool Document::flatten() {
  if (!try_commit_floating()) {
    notify_blocked("Unlock the layer to place the selection");
    return false;
  }
  clear_ora_stack();
  if (layers_.count() <= 1) {
    return false;
  }
  commit(std::make_unique<FlattenCommand>(snapshot_layers(), layers_.active_index()));
  return true;
}

void Document::set_layer_visible(int index, bool visible) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  Layer& layer = layers_.at(index);
  if (layer.visible() == visible) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.visible = visible;
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             visible ? "Show layer" : "Hide layer"));
}

void Document::set_layer_locked(int index, bool locked) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  Layer& layer = layers_.at(index);
  if (layer.locked() == locked) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.locked = locked;
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             locked ? "Lock layer" : "Unlock layer"));
}

void Document::set_layer_opacity(int index, float opacity) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  if (opacity < 0.0f) {
    opacity = 0.0f;
  }
  if (opacity > 1.0f) {
    opacity = 1.0f;
  }
  Layer& layer = layers_.at(index);
  if (layer.opacity() == opacity) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.opacity = opacity;
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             "Layer opacity"));
}

void Document::set_layer_blend(int index, BlendMode blend) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  Layer& layer = layers_.at(index);
  if (layer.blend() == blend) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.blend = blend;
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             "Layer blend"));
}

void Document::set_layer_offset(int index, int x, int y) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  Layer& layer = layers_.at(index);
  if (layer.offset_x() == x && layer.offset_y() == y) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.offset_x = x;
  after.offset_y = y;
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             "Layer offset"));
}

void Document::rename_layer(int index, std::string name) {
  if (index < 0 || index >= layers_.count()) {
    return;
  }
  Layer& layer = layers_.at(index);
  if (layer.name() == name) {
    return;
  }
  LayerSnapshot before = snapshot_layer_props(layer);
  LayerSnapshot after = before;
  after.name = std::move(name);
  commit(std::make_unique<LayerPropsCommand>(index, std::move(before), std::move(after),
                                             "Rename layer"));
}

}  // namespace lundukepaint
