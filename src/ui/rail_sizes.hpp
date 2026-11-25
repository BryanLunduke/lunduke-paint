// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_RAIL_SIZES_HPP
#define LUNDUKEPAINT_UI_RAIL_SIZES_HPP

namespace lundukepaint {

// Left-rail pickers. [ and ] walk these same rows.
inline constexpr int kLineWidthChoices[] = {1, 2, 3, 5, 8};
inline constexpr int kLineWidthChoiceCount = 5;
inline constexpr int kSprayRadiusChoices[] = {4, 8, 12, 16, 24};
inline constexpr int kSprayRadiusChoiceCount = 5;

// Nearest row, then one step. The result stays on a row.
inline int step_choice(int current, const int* choices, int count, int delta) {
  if (choices == nullptr || count < 1) {
    return current;
  }
  int best = 0;
  int best_d = 1 << 30;
  for (int i = 0; i < count; ++i) {
    const int d = current - choices[i];
    const int ad = d < 0 ? -d : d;
    if (ad < best_d) {
      best_d = ad;
      best = i;
    }
  }
  int next = best + delta;
  if (next < 0) {
    next = 0;
  }
  if (next >= count) {
    next = count - 1;
  }
  return choices[next];
}

}  // namespace lundukepaint

#endif
