// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_ACTIONS_HPP
#define LUNDUKEPAINT_APP_ACTIONS_HPP

namespace lundukepaint {
namespace actions {

constexpr const char* kAppId = "org.lunduke.LundukePaint";
constexpr const char* kVersion = "0.8";
constexpr const char* kProductName = "Lunduke Paint";


constexpr const char* kNew = "new";
constexpr const char* kOpen = "open";
constexpr const char* kSave = "save";
constexpr const char* kSaveAs = "save-as";
constexpr const char* kQuit = "quit";
constexpr const char* kCloseTab = "close-tab";

constexpr const char* kUndo = "undo";
constexpr const char* kRedo = "redo";
constexpr const char* kToggleRightDock = "toggle-right-dock";
constexpr const char* kZoomIn = "zoom-in";
constexpr const char* kZoomOut = "zoom-out";
constexpr const char* kZoom100 = "zoom-100";
constexpr const char* kZoomFit = "zoom-fit";
constexpr const char* kToggleGrid = "toggle-grid";

constexpr const char* kCut = "cut";
constexpr const char* kCopy = "copy";
constexpr const char* kCopyMerged = "copy-merged";
constexpr const char* kPaste = "paste";
constexpr const char* kDelete = "delete";
constexpr const char* kDuplicate = "duplicate";
constexpr const char* kSelectAll = "select-all";
constexpr const char* kDeselect = "deselect";
constexpr const char* kInvertSelection = "invert-selection";

constexpr const char* kCanvasSize = "canvas-size";
constexpr const char* kScale = "scale";
constexpr const char* kCrop = "crop";
constexpr const char* kAutocrop = "autocrop";
constexpr const char* kRotate90 = "rotate-90";
constexpr const char* kRotate180 = "rotate-180";
constexpr const char* kRotateCcw = "rotate-ccw";
constexpr const char* kFlipH = "flip-h";
constexpr const char* kFlipV = "flip-v";
constexpr const char* kClear = "clear";

constexpr const char* kLayerNew = "layer-new";
constexpr const char* kLayerDuplicate = "layer-duplicate";
constexpr const char* kLayerDelete = "layer-delete";
constexpr const char* kLayerRaise = "layer-raise";
constexpr const char* kLayerLower = "layer-lower";
constexpr const char* kLayerMergeDown = "layer-merge-down";
constexpr const char* kLayerFlatten = "layer-flatten";
constexpr const char* kLayerProperties = "layer-properties";

constexpr const char* kAdjustBrightness = "adjust-brightness";
constexpr const char* kAdjustInvert = "adjust-invert";
constexpr const char* kAdjustGrayscale = "adjust-grayscale";
constexpr const char* kAdjustHue = "adjust-hue";
constexpr const char* kAdjustPosterize = "adjust-posterize";

constexpr const char* kEffectBlur = "effect-blur";
constexpr const char* kEffectSharpen = "effect-sharpen";
constexpr const char* kEffectEmboss = "effect-emboss";

constexpr const char* kPreferences = "preferences";
constexpr const char* kShortcuts = "shortcuts";
constexpr const char* kAbout = "about";
constexpr const char* kPrint = "print";
constexpr const char* kFullscreen = "fullscreen";
constexpr const char* kRevert = "revert";
constexpr const char* kClearRecent = "clear-recent";


}  // namespace actions
}  // namespace lundukepaint

#endif
