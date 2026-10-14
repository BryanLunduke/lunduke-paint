// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_COMMANDS_IMAGE_HPP
#define LUNDUKEPAINT_DOC_COMMANDS_IMAGE_HPP

#include "doc/command.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class Layer;

// Full-buffer snapshot used when the whole canvas (or its size) changes.
class LayerBufferCommand : public Command {
public:
  static std::unique_ptr<LayerBufferCommand> from_buffers(std::string name, int old_w, int old_h,
                                                          const std::uint8_t* old_px, int old_stride,
                                                          int new_w, int new_h,
                                                          const std::uint8_t* new_px, int new_stride,
                                                          int layer_index = 0);

  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override;
  std::size_t memory_bytes() const override { return 128 + old_px_.size() + new_px_.size(); }

private:
  std::string name_;
  int layer_index_ = 0;
  int old_w_ = 0;
  int old_h_ = 0;
  int new_w_ = 0;
  int new_h_ = 0;
  std::vector<std::uint8_t> old_px_;
  std::vector<std::uint8_t> new_px_;
};

std::vector<std::uint8_t> copy_layer_pixels(const Layer& layer);

}  // namespace lundukepaint

#endif
