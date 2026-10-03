// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_DOCUMENT_HPP
#define LUNDUKEPAINT_DOC_DOCUMENT_HPP

#include "doc/history.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "io/ora.hpp"
#include "raster/types.hpp"

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

  bool dirty() const { return dirty_; }
  void set_dirty(bool dirty);
  void mark_clean();

  // Nested OpenRaster stacks. Null when the file has no layer groups.
  // Cleared when layers are added, removed, or reordered.
  void set_ora_stack(OraNode node);
  void clear_ora_stack();
  const OraNode* ora_stack() const;

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
  void delete_selection();
  void duplicate_selection();
  void paste_floating(int x, int y, int w, int h, std::vector<std::uint8_t> rgba);
  void replace_active_buffer(int width, int height, const std::uint8_t* rgba, int stride);
  void replace_stack(int width, int height, std::vector<std::unique_ptr<Layer>> layers,
                     int active_index);

  bool active_locked() const;
  std::vector<LayerSnapshot> snapshot_layers() const;

  void add_layer();
  void duplicate_layer();
  bool delete_layer();
  bool raise_layer();
  bool lower_layer();
  bool move_layer(int from, int to);
  bool merge_down();
  void flatten();
  void set_layer_visible(int index, bool visible);
  void set_layer_locked(int index, bool locked);
  void set_layer_opacity(int index, float opacity);
  void set_layer_blend(int index, BlendMode blend);
  void rename_layer(int index, std::string name);
  void set_layer_offset(int index, int x, int y);

  using ChangedFn = std::function<void()>;
  using InvalidatedFn = std::function<void(Rect)>;

  void set_on_changed(ChangedFn fn) { on_changed_ = std::move(fn); }
  void set_on_invalidated(InvalidatedFn fn) { on_invalidated_ = std::move(fn); }

  void notify_invalidated(Rect rect);
  void notify_changed();

private:
  Document(int width, int height, Color background, std::string layer_name);
  void note_history_dirty();

  int width_ = kDefaultWidth;
  int height_ = kDefaultHeight;
  int dpi_ = 96;
  std::string path_;
  double view_zoom_ = 1.0;
  bool dirty_ = false;
  bool has_ora_stack_ = false;
  OraNode ora_stack_{};
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  Color canvas_bg_ = Color::white();
  LayerStack layers_;
  History history_;
  Selection selection_;
  ChangedFn on_changed_;
  InvalidatedFn on_invalidated_;
};

}  // namespace lundukepaint

#endif
