// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_TOOLBOX_CATALOG_HPP
#define LUNDUKEPAINT_UI_TOOLBOX_CATALOG_HPP

namespace lundukepaint {

struct ToolboxTool {
  const char* id;
  const char* tooltip;
  const char* icon;
};

// MacPaint grid, then the tools that used to be keyboard-only. Icons are the
// app's own symbolic SVGs (gresource), not a system icon theme.
inline const ToolboxTool* toolbox_tools(int& count) {
  static const ToolboxTool kTools[] = {
      {"lasso", "Freeform select (M)", "tool-select-lasso-freeform-symbolic"},
      {"rect-select", "Rectangle select (S)", "tool-select-rectangle-symbolic"},
      {"hand", "Hand / pan (H)", "tool-hand-symbolic"},
      {"text", "Text (T)", "tool-text-symbolic"},
      {"fill", "Flood fill (F)", "tool-paintbucket-symbolic"},
      {"spray", "Spraycan (Y)", "tool-spray-symbolic"},
      {"brush", "Brush (B)", "tool-paintbrush-symbolic"},
      {"pencil", "Pencil (P)", "tool-pencil-symbolic"},
      {"line", "Line (L)", "tool-line-symbolic"},
      {"eraser", "Eraser (A)", "tool-eraser-symbolic"},
      {"rectangle", "Rectangle outline (R)", "tool-rectangle-symbolic"},
      {"rectangle-fill", "Rectangle filled (J)", "tool-rectangle-filled-symbolic"},
      {"rounded-rect", "Rounded rectangle outline (U)", "tool-rectangle-rounded-symbolic"},
      {"rounded-rect-fill", "Rounded rectangle filled (U)", "tool-rectangle-rounded-filled-symbolic"},
      {"ellipse", "Ellipse outline (E)", "tool-ellipse-symbolic"},
      {"ellipse-fill", "Ellipse filled (Z)", "tool-ellipse-filled-symbolic"},
      {"freeform", "Freeform outline (K)", "tool-freeformshape-symbolic"},
      {"freeform-fill", "Freeform filled (O)", "tool-freeformshape-filled-symbolic"},
      {"polygon", "Polygon outline (G)", "tool-select-lasso-polygon-symbolic"},
      {"polygon-fill", "Polygon filled (Q)", "tool-polygon-filled-symbolic"},
      {"picker", "Color picker (C)", "tool-colorpicker-symbolic"},
      {"magic-wand", "Magic wand (W)", "tool-magicwand-symbolic"},
      {"ellipse-select", "Ellipse select (I)", "tool-select-ellipse-symbolic"},
      {"curve", "Curve (V)", "tool-curve-symbolic"},
      {"polyline", "Polyline (N)", "tool-polyline-symbolic"},
  };
  count = static_cast<int>(sizeof(kTools) / sizeof(kTools[0]));
  return kTools;
}

inline bool toolbox_has_tool(const char* id) {
  int count = 0;
  const ToolboxTool* tools = toolbox_tools(count);
  if (id == nullptr) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    const char* have = tools[i].id;
    if (have == nullptr) {
      continue;
    }
    const char* a = have;
    const char* b = id;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      return true;
    }
  }
  return false;
}

}  // namespace lundukepaint

#endif
