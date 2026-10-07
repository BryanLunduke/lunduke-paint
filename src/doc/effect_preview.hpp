// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_EFFECT_PREVIEW_HPP
#define LUNDUKEPAINT_DOC_EFFECT_PREVIEW_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace lundukepaint {

class Document;
class Layer;

// Undo-safe scaffolding for the "Live Preview" check button on the adjustment
// dialogs.
//
// A live preview has to repaint the active layer on every slider tick without
// touching the undo stack, and OK still has to land in history exactly once.
// This class snapshots the active layer up front so that:
//   * preview() always re-applies the effect to the *pristine* snapshot, so
//     dragging a slider never stacks effect on top of effect, and never pushes
//     a history entry (the document's dirty flag is left alone too);
//   * restore() puts the pristine pixels back byte for byte (Cancel, or the
//     user switching Live Preview back off);
//   * commit() throws the preview pixels away first and then applies the effect
//     once through PixelPatchCommand, so OK is a single undoable step whether or
//     not a preview was showing.
class EffectPreview {
public:
  using EffectFn = std::function<void(std::uint8_t* rgba, int width, int height, int stride)>;

  explicit EffectPreview(Document& document);

  bool valid() const { return snapshot_ != nullptr; }
  bool previewing() const { return previewing_; }
  int layer_index() const { return layer_index_; }

  // Repaints the layer with the effect applied to the snapshot at full
  // resolution. No history. Returns the changed rectangle (clipped to the
  // selection, if any). Autosave must not run while previewing() is true.
  Rect preview(const EffectFn& fn);

  // Puts the snapshot back. Returns false when there was nothing to undo.
  bool restore();

  // Restores, then applies the effect once as a single undoable command.
  // Returns true when a history entry was pushed.
  bool commit(const std::string& name, const EffectFn& fn);

private:
  Layer* target();
  Rect render(Layer& out, const EffectFn& fn) const;

  Document* document_ = nullptr;
  int layer_index_ = 0;
  std::unique_ptr<Layer> snapshot_;
  bool previewing_ = false;
};

}  // namespace lundukepaint

#endif
