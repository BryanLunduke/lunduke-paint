// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_TEXT_BOX_HPP
#define LUNDUKEPAINT_RASTER_TEXT_BOX_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lundukepaint {

class Document;

// Inset between the box border and the glyphs. Wrap width is the box width
// minus this padding on both sides.
constexpr int kTextBoxPad = 4;
constexpr int kTextBoxMinSize = 16;

// The text string stores only what the user typed, including hard breaks from
// paste. Word wrap is a layout property of the current box width: resizing
// reflows and does not rewrite the string.
struct TextBoxState {
  Rect box{0, 0, 160, 32};
  std::string text;
  int cursor = 0;
  std::string family{"Sans"};
  int size_pt = 16;
  bool bold = false;
  bool italic = false;
  Color color = Color::black();
};

struct TextBoxMetrics {
  int line_count = 0;
  int layout_width = 0;
  int layout_height = 0;
  int cursor_x = 0;
  int cursor_y = 0;
  int cursor_h = 0;
};

enum class TextBoxHit {
  Outside,
  Inside,
  Border,
  NorthWest,
  North,
  NorthEast,
  East,
  SouthEast,
  South,
  SouthWest,
  West
};

enum class TextBoxPress { Commit, PlaceCursor, Move, Resize };

int text_box_handle_radius(double zoom);
int text_box_border_slop(double zoom);

TextBoxHit hit_test_text_box(const Rect& box, int x, int y, double zoom);
TextBoxPress text_box_press_action(TextBoxHit hit);

void move_text_box(Rect& box, int dx, int dy);
// `start` is the box at pointer-down. The edge follows the pointer delta from
// (grab_x, grab_y) so a handle hit a few pixels off the corner does not jump.
void resize_text_box(Rect& box, TextBoxHit handle, int pointer_x, int pointer_y, const Rect& start,
                     int grab_x, int grab_y);

int text_box_content_width(const TextBoxState& state);
int text_box_content_height(const TextBoxState& state);

// Layout only. Does not modify `state.text`.
void layout_text_box(const TextBoxState& state, TextBoxMetrics& metrics);

// Rasterize the wrapped text into straight-alpha RGBA, clipped to the content
// box. Empty text leaves `rgba` empty and returns false; metrics are still
// filled so an empty box can show a caret. Does not modify `state.text`.
bool render_text_box(const TextBoxState& state, std::vector<std::uint8_t>& rgba, int& width,
                     int& height, TextBoxMetrics* metrics);

int text_box_line_count(const TextBoxState& state);
int text_box_index_at(const TextBoxState& state, int canvas_x, int canvas_y);

void text_box_insert(TextBoxState& state, const std::string& utf8);
void text_box_backspace(TextBoxState& state);
void text_box_delete_forward(TextBoxState& state);
void text_box_move_left(TextBoxState& state);
void text_box_move_right(TextBoxState& state);
void text_box_move_up(TextBoxState& state);
void text_box_move_down(TextBoxState& state);
void text_box_move_line_start(TextBoxState& state);
void text_box_move_line_end(TextBoxState& state);

// Stamp the wrapped text onto the active layer as one "Text" history entry.
// Returns false when nothing was written (empty text, locked layer, or no
// covered pixels). Does not modify `state`.
bool commit_text_box(Document& document, const TextBoxState& state);

}  // namespace lundukepaint

#endif
