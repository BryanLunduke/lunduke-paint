// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_COMMANDS_LAYERS_HPP
#define LUNDUKEPAINT_DOC_COMMANDS_LAYERS_HPP

#include "doc/command.hpp"
#include "doc/layer.hpp"
#include "raster/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

inline std::size_t snaps_bytes(const std::vector<LayerSnapshot>& snaps) {
  std::size_t total = 64;
  for (const LayerSnapshot& snap : snaps) {
    total += snapshot_bytes(snap);
  }
  return total;
}

class AddLayerCommand : public Command {
public:
  AddLayerCommand(int index, LayerSnapshot layer, std::string name = "Add layer");
  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }
  std::size_t memory_bytes() const override { return snapshot_bytes(layer_); }

private:
  int index_ = 0;
  LayerSnapshot layer_;
  std::string name_;
  Rect dirty_;
};

class DeleteLayerCommand : public Command {
public:
  DeleteLayerCommand(int index, LayerSnapshot layer);
  std::string name() const override { return "Delete layer"; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }
  std::size_t memory_bytes() const override { return snapshot_bytes(layer_); }

private:
  int index_ = 0;
  LayerSnapshot layer_;
  Rect dirty_;
};

class DuplicateLayerCommand : public Command {
public:
  explicit DuplicateLayerCommand(int source_index);
  std::string name() const override { return "Duplicate layer"; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }

private:
  int source_ = 0;
  int dest_ = 0;
  Rect dirty_;
};

class MoveLayerCommand : public Command {
public:
  MoveLayerCommand(int from, int to, std::string name);
  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }

private:
  int from_ = 0;
  int to_ = 0;
  std::string name_;
  Rect dirty_;
};

class MergeDownCommand : public Command {
public:
  MergeDownCommand(int upper_index, LayerSnapshot lower, LayerSnapshot upper);
  std::string name() const override { return "Merge down"; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }
  std::size_t memory_bytes() const override { return snapshot_bytes(lower_) + snapshot_bytes(upper_layer_); }

private:
  int upper_ = 0;
  LayerSnapshot lower_;
  LayerSnapshot upper_layer_;
  Rect dirty_;
};

class FlattenCommand : public Command {
public:
  FlattenCommand(std::vector<LayerSnapshot> layers, int active);
  std::string name() const override { return "Flatten"; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }
  std::size_t memory_bytes() const override { return snaps_bytes(layers_); }

private:
  std::vector<LayerSnapshot> layers_;
  int active_ = 0;
  Rect dirty_;
};

class LayerPropsCommand : public Command {
public:
  LayerPropsCommand(int index, LayerSnapshot before, LayerSnapshot after, std::string name);
  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return dirty_; }
  std::size_t memory_bytes() const override { return snapshot_bytes(before_) + snapshot_bytes(after_); }

private:
  int index_ = 0;
  LayerSnapshot before_;
  LayerSnapshot after_;
  std::string name_;
  Rect dirty_;
};

enum class StackXformKind {
  ResizeCanvas,
  Scale,
  Crop,
  Rotate90,
  Rotate180,
  Rotate270,
  FlipH,
  FlipV
};

// Redo reruns this on the stored pre-image. The transformed pixels are not kept.
struct StackXform {
  StackXformKind kind = StackXformKind::FlipH;
  int old_w = 0;
  int old_h = 0;
  int new_w = 0;
  int new_h = 0;
  bool nearest = false;
  Color fill{};
  Rect crop{};
  // Crop-local coverage. Empty means the whole crop rectangle is kept.
  // A zero byte clears that pixel so a lasso is not promoted to its box.
  std::vector<std::uint8_t> crop_mask;
};

class AllLayersBufferCommand : public Command {
public:
  AllLayersBufferCommand(std::string name, std::vector<LayerSnapshot> old_layers, int old_active,
                         StackXform xform);
  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override;
  std::size_t memory_bytes() const override { return snaps_bytes(old_layers_) + xform_.crop_mask.size() + 128; }

private:
  std::string name_;
  int old_active_ = 0;
  StackXform xform_{};
  std::vector<LayerSnapshot> old_layers_;
};

}  // namespace lundukepaint

#endif
