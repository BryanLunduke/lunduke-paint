// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_COMMANDS_PIXELS_HPP
#define LUNDUKEPAINT_DOC_COMMANDS_PIXELS_HPP

#include "doc/command.hpp"
#include "doc/selection.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class Layer;

// One history item for a stroke or fill. Stores only changed tiles of the
// dirty rectangle (not the whole layer).
class PixelPatchCommand : public Command {
public:
  static std::unique_ptr<PixelPatchCommand> from_layers(const Layer& before, const Layer& after,
                                                        Rect bounds, std::string name,
                                                        int layer_index = 0);

  std::string name() const override { return name_; }
  void apply(Document& document) override;
  void undo(Document& document) override;
  Rect dirty_rect() const override { return bounds_; }

  bool empty() const { return tiles_.empty(); }
  std::size_t memory_bytes() const override;

  // Move selection: restore the ants (and the float, on undo) with the pixels.
  void set_selection_change(SelectionState before, SelectionState after);

private:
  struct Tile {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool before_solid = false;
    bool after_solid = false;
    Color before_color{};
    Color after_color{};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
  };

  std::string name_;
  int layer_index_ = 0;
  Rect bounds_;
  std::vector<Tile> tiles_;
  bool has_selection_ = false;
  SelectionState selection_before_{};
  SelectionState selection_after_{};
};

}  // namespace lundukepaint

#endif
