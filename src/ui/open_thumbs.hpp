// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_OPEN_THUMBS_HPP
#define LUNDUKEPAINT_UI_OPEN_THUMBS_HPP

#include <gtkmm/box.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lundukepaint {

// Folder thumbnail grid for the Open dialog. Thumbs are decoded on a worker
// thread and cached. The chooser's existing side preview is left alone.
class OpenThumbGrid : public Gtk::Box {
public:
  explicit OpenThumbGrid(Gtk::FileChooserDialog& chooser);
  ~OpenThumbGrid() override;

private:
  struct CacheEntry {
    std::int64_t mtime = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
  };
  struct Job {
    std::string path;
    std::int64_t mtime = 0;
    unsigned generation = 0;
  };
  struct Ready {
    std::string path;
    std::int64_t mtime = 0;
    unsigned generation = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool ok = false;
  };
  struct Shared {
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    std::vector<Job> queue;
    std::vector<Ready> ready;
    OpenThumbGrid* owner = nullptr;
  };

  void on_folder_changed();
  void rebuild(const std::string& folder);
  void apply_ready();
  void worker_main();
  static gboolean on_idle(gpointer data);

  Gtk::FileChooserDialog& chooser_;
  Gtk::Label caption_;
  Gtk::ScrolledWindow scroll_;
  Gtk::FlowBox flow_;
  std::shared_ptr<Shared> shared_;
  std::thread worker_;
  unsigned generation_ = 0;
  std::vector<std::string> shown_;
  static std::mutex cache_mu_;
  static std::vector<std::pair<std::string, CacheEntry>> cache_;
};

}  // namespace lundukepaint

#endif
