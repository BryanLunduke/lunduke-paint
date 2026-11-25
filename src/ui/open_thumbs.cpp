// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/open_thumbs.hpp"

#include "io/image_io.hpp"

#include <gdkmm/pixbuf.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm/button.h>
#include <gtkmm/image.h>

#include <algorithm>
#include <sys/stat.h>

namespace lundukepaint {
namespace {

constexpr int kThumb = 72;
constexpr int kMaxThumbs = 180;

bool thumbnable(const std::string& name) {
  const ImageFormat format = format_from_path(name);
  return format == ImageFormat::Png || format == ImageFormat::Jpeg || format == ImageFormat::Bmp ||
         format == ImageFormat::Ora || format == ImageFormat::Gif;
}

std::int64_t file_mtime(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return 0;
  }
  return static_cast<std::int64_t>(st.st_mtime);
}

Glib::RefPtr<Gdk::Pixbuf> pixbuf_from_rgba(const std::uint8_t* rgba, int width, int height) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return {};
  }
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, width, height);
  if (!pixbuf) {
    return {};
  }
  guint8* dst = pixbuf->get_pixels();
  const int stride = pixbuf->get_rowstride();
  const int channels = pixbuf->get_n_channels();
  for (int y = 0; y < height; ++y) {
    guint8* row = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    const std::uint8_t* src = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
    for (int x = 0; x < width; ++x) {
      guint8* p = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
      p[0] = src[static_cast<std::size_t>(x) * 4];
      p[1] = src[static_cast<std::size_t>(x) * 4 + 1];
      p[2] = src[static_cast<std::size_t>(x) * 4 + 2];
      if (channels > 3) {
        p[3] = src[static_cast<std::size_t>(x) * 4 + 3];
      }
    }
  }
  return pixbuf;
}

}  // namespace

std::mutex OpenThumbGrid::cache_mu_;
std::vector<std::pair<std::string, OpenThumbGrid::CacheEntry>> OpenThumbGrid::cache_;

OpenThumbGrid::OpenThumbGrid(Gtk::FileChooserDialog& chooser)
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4), chooser_(chooser), shared_(std::make_shared<Shared>()) {
  set_size_request(280, 210);
  caption_.set_halign(Gtk::ALIGN_START);
  caption_.set_text("Folder thumbnails");
  pack_start(caption_, Gtk::PACK_SHRINK);
  flow_.set_selection_mode(Gtk::SELECTION_NONE);
  flow_.set_homogeneous(true);
  flow_.set_min_children_per_line(4);
  flow_.set_max_children_per_line(12);
  flow_.set_column_spacing(6);
  flow_.set_row_spacing(6);
  scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll_.set_min_content_height(180);
  scroll_.add(flow_);
  pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
  show_all();

  shared_->owner = this;
  worker_ = std::thread([this]() { worker_main(); });
  chooser_.signal_current_folder_changed().connect(
      sigc::mem_fun(*this, &OpenThumbGrid::on_folder_changed));
  on_folder_changed();
}

OpenThumbGrid::~OpenThumbGrid() {
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    shared_->owner = nullptr;
    shared_->stop = true;
  }
  shared_->cv.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

gboolean OpenThumbGrid::on_idle(gpointer data) {
  auto* hold = static_cast<std::shared_ptr<Shared>*>(data);
  OpenThumbGrid* owner = nullptr;
  {
    std::lock_guard<std::mutex> lock((*hold)->mu);
    owner = (*hold)->owner;
  }
  if (owner != nullptr) {
    owner->apply_ready();
  }
  delete hold;
  return G_SOURCE_REMOVE;
}

void OpenThumbGrid::on_folder_changed() {
  rebuild(chooser_.get_current_folder());
}

void OpenThumbGrid::rebuild(const std::string& folder) {
  ++generation_;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    shared_->queue.clear();
    shared_->ready.clear();
  }
  for (auto* child : flow_.get_children()) {
    flow_.remove(*child);
  }
  shown_.clear();
  if (folder.empty()) {
    caption_.set_text("Folder thumbnails");
    return;
  }
  std::vector<std::string> names;
  try {
    Glib::Dir dir(folder);
    for (auto it = dir.begin(); it != dir.end(); ++it) {
      if (thumbnable(*it)) {
        names.push_back(*it);
      }
    }
  } catch (const Glib::FileError&) {
    caption_.set_text("Folder thumbnails");
    return;
  }
  std::sort(names.begin(), names.end());
  const bool truncated = static_cast<int>(names.size()) > kMaxThumbs;
  if (truncated) {
    names.resize(kMaxThumbs);
  }
  caption_.set_text(truncated ? "Folder thumbnails (first 180)" : "Folder thumbnails");
  std::vector<Job> jobs;
  jobs.reserve(names.size());
  for (const std::string& name : names) {
    std::string path = Glib::build_filename(folder, name);
    shown_.push_back(path);
    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    auto* image = Gtk::manage(new Gtk::Image());
    image->set_size_request(kThumb, kThumb);
    auto* label = Gtk::manage(new Gtk::Label(name));
    label->set_ellipsize(Pango::ELLIPSIZE_END);
    label->set_max_width_chars(12);
    label->set_halign(Gtk::ALIGN_CENTER);
    box->pack_start(*image, Gtk::PACK_SHRINK);
    box->pack_start(*label, Gtk::PACK_SHRINK);
    auto* button = Gtk::manage(new Gtk::Button());
    button->add(*box);
    button->set_relief(Gtk::RELIEF_NONE);
    button->set_tooltip_text(name);
    const std::string chosen = path;
    button->signal_clicked().connect([this, chosen]() {
      try {
        chooser_.select_filename(chosen);
      } catch (const Glib::Error&) {
      }
    });
    flow_.add(*button);
    const std::int64_t mtime = file_mtime(path);
    bool cached = false;
    CacheEntry hit;
    {
      std::lock_guard<std::mutex> lock(cache_mu_);
      for (const auto& item : cache_) {
        if (item.first == path && item.second.mtime == mtime && !item.second.rgba.empty()) {
          hit = item.second;
          cached = true;
          break;
        }
      }
    }
    if (cached) {
      if (auto pixbuf = pixbuf_from_rgba(hit.rgba.data(), hit.width, hit.height)) {
        image->set(pixbuf);
      }
    } else {
      jobs.push_back(Job{path, mtime, generation_});
    }
    image->set_data("thumb-path", g_strdup(path.c_str()), g_free);
  }
  show_all();
  if (!jobs.empty()) {
    {
      std::lock_guard<std::mutex> lock(shared_->mu);
      shared_->queue = std::move(jobs);
    }
    shared_->cv.notify_all();
  }
}

void OpenThumbGrid::apply_ready() {
  std::vector<Ready> batch;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    batch.swap(shared_->ready);
  }
  if (batch.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(cache_mu_);
    for (const Ready& item : batch) {
      if (!item.ok) {
        continue;
      }
      cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                  [&](const auto& entry) { return entry.first == item.path; }),
                   cache_.end());
      CacheEntry entry;
      entry.mtime = item.mtime;
      entry.width = item.width;
      entry.height = item.height;
      entry.rgba = item.rgba;
      cache_.insert(cache_.begin(), {item.path, std::move(entry)});
      if (cache_.size() > 256) {
        cache_.resize(256);
      }
    }
  }
  for (const Ready& item : batch) {
    if (item.generation != generation_ || !item.ok) {
      continue;
    }
    for (auto* child : flow_.get_children()) {
      Gtk::Widget* inner = child;
      if (auto* wrapped = dynamic_cast<Gtk::FlowBoxChild*>(child)) {
        inner = wrapped->get_child();
      }
      auto* button = dynamic_cast<Gtk::Button*>(inner);
      if (button == nullptr) {
        continue;
      }
      auto* box = dynamic_cast<Gtk::Box*>(button->get_child());
      if (box == nullptr) {
        continue;
      }
      for (auto* inner : box->get_children()) {
        auto* image = dynamic_cast<Gtk::Image*>(inner);
        if (image == nullptr) {
          continue;
        }
        const char* path = static_cast<const char*>(image->get_data("thumb-path"));
        if (path != nullptr && item.path == path) {
          if (auto pixbuf = pixbuf_from_rgba(item.rgba.data(), item.width, item.height)) {
            image->set(pixbuf);
          }
        }
      }
    }
  }
}

void OpenThumbGrid::worker_main() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(shared_->mu);
      shared_->cv.wait(lock, [&]() { return shared_->stop || !shared_->queue.empty(); });
      if (shared_->stop && shared_->queue.empty()) {
        return;
      }
      job = shared_->queue.back();
      shared_->queue.pop_back();
    }
    LoadedImage image;
    const bool ok = load_image_preview(job.path, kThumb, image);
    Ready ready;
    ready.path = job.path;
    ready.mtime = job.mtime;
    ready.generation = job.generation;
    ready.ok = ok && image.ok();
    if (ready.ok) {
      ready.width = image.width;
      ready.height = image.height;
      ready.rgba = std::move(image.rgba);
    }
    bool post = false;
    {
      std::lock_guard<std::mutex> lock(shared_->mu);
      if (!shared_->stop) {
        shared_->ready.push_back(std::move(ready));
        post = shared_->ready.size() == 1;
      }
    }
    if (post) {
      g_idle_add(&OpenThumbGrid::on_idle, new std::shared_ptr<Shared>(shared_));
    }
  }
}

}  // namespace lundukepaint
