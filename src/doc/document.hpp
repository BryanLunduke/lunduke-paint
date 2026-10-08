// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_DOCUMENT_HPP
#define LUNDUKEPAINT_DOC_DOCUMENT_HPP

#include "doc/history.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "io/ora.hpp"
#include "raster/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class Document {
public:
  static std::unique_ptr<Document> create(int width, int height, Color background,
                                          std::string layer_name = "Background");

  int width() const { return width_; }
  int height() const { return height_; }
  int dpi() const { return dpi_; }

  const std::string& path() const { return path_; }
  void set_path(std::string path) { path_ = std::move(path); }

  double view_zoom() const { return view_zoom_; }
  void set_view_zoom(double zoom) { view_zoom_ = zoom; }

  bool dirty() const { return dirty_ || unsaved_overlay_; }
  void set_dirty(bool dirty);
  void mark_clean();
  // An open text box (or any overlay) that is not in the layer stack yet.
  // Save must not mark the document clean while this is set.
  void set_unsaved_overlay(bool on);
  bool unsaved_overlay() const { return unsaved_overlay_; }

  // Stable for this document in this session. Crash recovery writes
  // recovery-<id>.ora so one tab cannot erase another's autosave.
  std::uint64_t recovery_id() const { return recovery_id_; }

  // Nested OpenRaster stacks. Null when the file has no layer groups.
  // Cleared when layers are added, removed, or reordered.
  void set_ora_stack(OraNode node);
  void clear_ora_stack();
  const OraNode* ora_stack() const;

  // Commits a floating selection onto its source layer, then switches.
  // False when the float's layer is locked: the active layer is left alone.
  bool set_active_layer(int index);

  Color foreground() const { return fg_; }
  Color background() const { return bg_; }
  void set_foreground(Color color);
  void set_background(Color color);
  void swap_colors();
  void reset_colors();

  LayerStack& layers() { return layers_; }
  const LayerStack& layers() const { return layers_; }

  History& history() { return history_; }
  const History& history() const { return history_; }

  Selection& selection() { return selection_; }
  const Selection& selection() const { return selection_; }

  Color canvas_background() const { return canvas_bg_; }

  void commit(std::unique_ptr<Command> command);
  Rect undo();
  Rect redo();
  Rect jump_history(int target);

  void select_all();
  void deselect();
  void invert_selection();
  bool commit_floating(const char* name = "Move selection");
  // True when there is no float, or the float was stamped. False when the
  // source layer is locked; the float stays up and nothing else should proceed.
  bool try_commit_floating(const char* name = "Move selection");
  void delete_selection();
  void duplicate_selection();
  void paste_floating(int x, int y, int w, int h, std::vector<std::uint8_t> rgba);
  void replace_active_buffer(int width, int height, const std::uint8_t* rgba, int stride);
  void replace_stack(int width, int height, std::vector<std::unique_ptr<Layer>> layers,
                     int active_index);

  bool active_locked() const;
  std::vector<LayerSnapshot> snapshot_layers() const;

  bool add_layer();
  bool duplicate_layer();
  bool delete_layer();
  bool raise_layer();
  bool lower_layer();
  bool move_layer(int from, int to);
  bool merge_down();
  bool flatten();
  void set_layer_visible(int index, bool visible);
  void set_layer_locked(int index, bool locked);
  void set_layer_opacity(int index, float opacity);
  void set_layer_blend(int index, BlendMode blend);
  void rename_layer(int index, std::string name);
  void set_layer_offset(int index, int x, int y);

  using ChangedFn = std::function<void()>;
  using InvalidatedFn = std::function<void(Rect)>;
  using BlockedFn = std::function<void(const char*)>;
  // False aborts a history or layer change that would drop live edits.
  using DisruptFn = std::function<bool(const char* action)>;

  void set_on_changed(ChangedFn fn) { on_changed_ = std::move(fn); }
  void set_on_invalidated(InvalidatedFn fn) { on_invalidated_ = std::move(fn); }
  void set_on_selection(ChangedFn fn) { on_selection_ = std::move(fn); }
  void set_on_blocked(BlockedFn fn) { on_blocked_ = std::move(fn); }
  void set_on_disrupt(DisruptFn fn) { on_disrupt_ = std::move(fn); }

  void notify_invalidated(Rect rect);
  void notify_changed();
  void notify_blocked(const char* message);
  // Selection geometry during a drag: status bar only, not the layer/history panels.
  void notify_selection();

private:
  Document(int width, int height, Color background, std::string layer_name);
  void note_history_dirty();
  bool allow_disrupt(const char* action);

  int width_ = kDefaultWidth;
  int height_ = kDefaultHeight;
  int dpi_ = 96;
  std::string path_;
  double view_zoom_ = 1.0;
  bool dirty_ = false;
  bool unsaved_overlay_ = false;
  std::uint64_t recovery_id_ = 0;
  bool has_ora_stack_ = false;
  OraNode ora_stack_{};
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  Color canvas_bg_ = Color::white();
  LayerStack layers_;
  History history_;
  Selection selection_;
  ChangedFn on_changed_;
  ChangedFn on_selection_;
  BlockedFn on_blocked_;
  DisruptFn on_disrupt_;
  InvalidatedFn on_invalidated_;
};

}  // namespace lundukepaint

#endif
