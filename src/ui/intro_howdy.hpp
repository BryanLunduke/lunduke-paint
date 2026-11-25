// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_INTRO_HOWDY_HPP
#define LUNDUKEPAINT_UI_INTRO_HOWDY_HPP

#include <cairo.h>

// Cursive howdy: cubics from data/howdy.svg matched to refs/howdy-reference-exact.jpeg.
// The overlay is revealed by travelled length over exactly one second.
// Nothing here touches the document or any font.
//
// The greeting is a preference (default off), not a compile-time launch gate.
// Sources + data/howdy.svg (+ jpeg twin) stay in-tree.

namespace lundukepaint {
namespace intro {

inline const char* text() { return "howdy"; }

constexpr long kDurationUs = 1000000;

double progress(long elapsed_us);
bool finished(long elapsed_us);
double path_length();
double revealed_length(double progress_value);
int curve_count();

struct Ink {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

bool measure(int size_px, Ink& ink);
void draw(cairo_t* cr, double x, double y, int size_px, double progress_value);

}  // namespace intro
}  // namespace lundukepaint

#endif
