// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_layers.hpp"

#include "doc/document.hpp"
#include "raster/transform.hpp"

#include <algorithm>
#include <memory>

namespace lundukepaint {
namespace {

Rect canvas_rect(const Document& document) {
  return {0, 0, document.width(), document.height()};
}

void apply_props(Layer& layer, const LayerSnapshot& snap) {
  layer.set_name(snap.name);
  layer.set_visible(snap.visible);
  layer.set_locked(snap.locked);
  layer.set_opacity(snap.opacity);
  layer.set_blend(snap.blend);
  layer.set_offset(snap.offset_x, snap.offset_y);
}

std::vector<std::unique_ptr<Layer>> layers_from_snaps(const std::vector<LayerSnapshot>& snaps) {
  std::vector<std::unique_ptr<Layer>> out;
  out.reserve(snaps.size());
  for (const auto& snap : snaps) {
    out.push_back(layer_from_snapshot(snap));
  }
  return out;
}

}  // namespace

AddLayerCommand::AddLayerCommand(int index, LayerSnapshot layer, std::string name)
    : index_(index), layer_(std::move(layer)), name_(std::move(name)) {}

void AddLayerCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  const int idx = document.layers().insert(index_, layer_from_snapshot(layer_));
  document.layers().set_active_index(idx);
}

void AddLayerCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().take(index_);
}

DeleteLayerCommand::DeleteLayerCommand(int index, LayerSnapshot layer)
    : index_(index), layer_(std::move(layer)) {}

void DeleteLayerCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().take(index_);
}

void DeleteLayerCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().insert(index_, layer_from_snapshot(layer_));
  document.layers().set_active_index(index_);
}

DuplicateLayerCommand::DuplicateLayerCommand(int source_index) : source_(source_index) {
  dest_ = source_index + 1;
}

void DuplicateLayerCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  auto copy = document.layers().at(source_).clone();
  copy->set_name(document.layers().at(source_).name() + " copy");
  dest_ = document.layers().insert(source_ + 1, std::move(copy));
  document.layers().set_active_index(dest_);
}

void DuplicateLayerCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().take(dest_);
  document.layers().set_active_index(source_);
}

MoveLayerCommand::MoveLayerCommand(int from, int to, std::string name)
    : from_(from), to_(to), name_(std::move(name)) {}

void MoveLayerCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().move_layer(from_, to_);
}

void MoveLayerCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().move_layer(to_, from_);
}

MergeDownCommand::MergeDownCommand(int upper_index, LayerSnapshot lower, LayerSnapshot upper)
    : upper_(upper_index), lower_(std::move(lower)), upper_layer_(std::move(upper)) {}

void MergeDownCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().merge_down(upper_);
}

void MergeDownCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().replace_at(upper_ - 1, layer_from_snapshot(lower_));
  document.layers().insert(upper_, layer_from_snapshot(upper_layer_));
  document.layers().set_active_index(upper_);
}

FlattenCommand::FlattenCommand(std::vector<LayerSnapshot> layers, int active)
    : layers_(std::move(layers)), active_(active) {}

void FlattenCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().flatten_visible();
}

void FlattenCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  document.layers().replace_stack(document.width(), document.height(), layers_from_snaps(layers_),
                                  active_);
}

LayerPropsCommand::LayerPropsCommand(int index, LayerSnapshot before, LayerSnapshot after,
                                     std::string name)
    : index_(index), before_(std::move(before)), after_(std::move(after)), name_(std::move(name)) {}

void LayerPropsCommand::apply(Document& document) {
  dirty_ = canvas_rect(document);
  apply_props(document.layers().at(index_), after_);
}

void LayerPropsCommand::undo(Document& document) {
  dirty_ = canvas_rect(document);
  apply_props(document.layers().at(index_), before_);
}

PlacedPixels run_stack_xform(const StackXform& xform, const Layer& layer) {
  const std::uint8_t* src = layer.pixels();
  const int w = layer.width();
  const int h = layer.height();
  const int stride = layer.stride();
  const int ox = layer.offset_x();
  const int oy = layer.offset_y();
  switch (xform.kind) {
    case StackXformKind::ResizeCanvas:
      return place_resize_canvas(src, w, h, stride, ox, oy, xform.old_w, xform.old_h, xform.new_w,
                                 xform.new_h, xform.fill);
    case StackXformKind::Scale:
      return place_scale(src, w, h, stride, ox, oy, xform.old_w, xform.old_h, xform.new_w,
                         xform.new_h, xform.nearest);
    case StackXformKind::Crop:
      return place_crop(src, w, h, stride, ox, oy, xform.crop);
    case StackXformKind::Rotate90:
      return place_rotate_90_cw(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
    case StackXformKind::Rotate180:
      return place_rotate_180(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
    case StackXformKind::Rotate270:
      return place_rotate_90_ccw(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
    case StackXformKind::FlipH:
      return place_flip_h(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
    case StackXformKind::FlipV:
      return place_flip_v(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
  }
  return place_flip_h(src, w, h, stride, ox, oy, xform.old_w, xform.old_h);
}

AllLayersBufferCommand::AllLayersBufferCommand(std::string name, std::vector<LayerSnapshot> old_layers,
                                               int old_active, StackXform xform)
    : name_(std::move(name)),
      old_active_(old_active),
      xform_(xform),
      old_layers_(std::move(old_layers)) {}

void AllLayersBufferCommand::apply(Document& document) {
  std::vector<std::unique_ptr<Layer>> next;
  next.reserve(static_cast<std::size_t>(document.layers().count()));
  for (int i = 0; i < document.layers().count(); ++i) {
    const Layer& layer = document.layers().at(i);
    PlacedPixels placed = run_stack_xform(xform_, layer);
    const int pw = std::max(1, placed.width);
    const int ph = std::max(1, placed.height);
    auto out = std::make_unique<Layer>(pw, ph, Color::transparent(), layer.name());
    out->set_visible(layer.visible());
    out->set_locked(layer.locked());
    out->set_opacity(layer.opacity());
    out->set_blend(layer.blend());
    out->set_offset(placed.offset_x, placed.offset_y);
    if (!placed.pixels.empty()) {
      out->set_pixels(pw, ph, placed.pixels.data(), pw * 4);
    }
    next.push_back(std::move(out));
  }
  document.replace_stack(xform_.new_w, xform_.new_h, std::move(next), old_active_);
}

void AllLayersBufferCommand::undo(Document& document) {
  document.replace_stack(xform_.old_w, xform_.old_h, layers_from_snaps(old_layers_), old_active_);
}

Rect AllLayersBufferCommand::dirty_rect() const {
  return {0, 0, std::max(xform_.old_w, xform_.new_w), std::max(xform_.old_h, xform_.new_h)};
}

}  // namespace lundukepaint
