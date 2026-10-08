// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/image_io.hpp"

#include "io/ora.hpp"
#include "raster/transform.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lundukepaint {
namespace {

std::string lower_ext(const std::string& path) {
  auto slash = path.find_last_of("/\\");
  auto name = slash == std::string::npos ? path : path.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos) {
    return {};
  }
  std::string ext = name.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

std::string basename_no_ext(const std::string& path) {
  auto slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name.resize(dot);
  }
  return name.empty() ? "Background" : name;
}

void copy_pixbuf_to_rgba(const GdkPixbuf* pixbuf, std::vector<std::uint8_t>& rgba, int width,
                         int height) {
  const int n = gdk_pixbuf_get_n_channels(pixbuf);
  const int stride = gdk_pixbuf_get_rowstride(pixbuf);
  const guint8* src = gdk_pixbuf_get_pixels(pixbuf);
  const gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
  rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
  for (int y = 0; y < height; ++y) {
    const guint8* row = src + static_cast<std::size_t>(y) * stride;
    std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * width * 4;
    for (int x = 0; x < width; ++x) {
      const guint8* p = row + static_cast<std::size_t>(x) * n;
      dst[0] = p[0];
      dst[1] = p[1];
      dst[2] = p[2];
      dst[3] = (has_alpha && n >= 4) ? p[3] : 255;
      dst += 4;
    }
  }
}

GdkPixbuf* pixbuf_from_rgba(const std::uint8_t* rgba, int width, int height, int stride,
                            bool flatten_white) {
  GdkPixbuf* pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
  if (pixbuf == nullptr) {
    return nullptr;
  }
  guint8* dst = gdk_pixbuf_get_pixels(pixbuf);
  const int dst_stride = gdk_pixbuf_get_rowstride(pixbuf);
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* srow = rgba + static_cast<std::size_t>(y) * stride;
    guint8* drow = dst + static_cast<std::size_t>(y) * dst_stride;
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* s = srow + static_cast<std::size_t>(x) * 4;
      guint8* d = drow + static_cast<std::size_t>(x) * 4;
      if (flatten_white && s[3] != 255) {
        const int a = s[3];
        const int ia = 255 - a;
        d[0] = static_cast<guint8>((s[0] * a + 255 * ia + 127) / 255);
        d[1] = static_cast<guint8>((s[1] * a + 255 * ia + 127) / 255);
        d[2] = static_cast<guint8>((s[2] * a + 255 * ia + 127) / 255);
        d[3] = 255;
      } else {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    }
  }
  return pixbuf;
}

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
}

void png_chunk(std::vector<std::uint8_t>& out, const char type[4], const std::uint8_t* data,
               std::size_t size) {
  append_be32(out, static_cast<std::uint32_t>(size));
  const std::size_t type_at = out.size();
  out.insert(out.end(), type, type + 4);
  if (size > 0 && data != nullptr) {
    out.insert(out.end(), data, data + size);
  }
  const uLong crc = crc32(0L, out.data() + type_at, static_cast<uInt>(4 + size));
  append_be32(out, static_cast<std::uint32_t>(crc));
}

std::string with_errno(const char* prefix) {
  const int err = errno;
  std::string out = prefix != nullptr ? prefix : "Save failed";
  if (err != 0) {
    const char* text = g_strerror(err);
    if (text != nullptr && text[0] != '\0') {
      out += ": ";
      out += text;
    }
  }
  return out;
}

bool write_all_fd(int fd, const void* data, std::size_t size) {
  const char* bytes = static_cast<const char*>(data);
  std::size_t off = 0;
  while (off < size) {
    const ssize_t wrote = ::write(fd, bytes + off, size - off);
    if (wrote < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    off += static_cast<std::size_t>(wrote);
  }
  return true;
}

}  // namespace

std::string replace_path_extension(const std::string& path, const std::string& extension) {
  std::string ext = extension;
  if (ext.empty()) {
    ext = ".png";
  } else if (ext[0] != '.') {
    ext.insert(ext.begin(), '.');
  }
  const auto slash = path.find_last_of("/\\");
  const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = name.find_last_of('.');
  if (dot != std::string::npos && dot != 0) {
    name.resize(dot);
  }
  return dir + name + ext;
}

bool image_has_multiple_frames(const std::string& path) {
  GError* error = nullptr;
  GdkPixbufAnimation* anim = gdk_pixbuf_animation_new_from_file(path.c_str(), &error);
  if (anim == nullptr) {
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }
  const bool multi = gdk_pixbuf_animation_is_static_image(anim) == FALSE;
  g_object_unref(anim);
  return multi;
}

bool atomic_create(const std::string& path, AtomicFile& out, std::string& error) {
  out = {};
  if (path.empty()) {
    error = "No path";
    return false;
  }
  struct stat st;
  if (lstat(path.c_str(), &st) == 0) {
    if (S_ISLNK(st.st_mode)) {
      error = "Refusing to follow a symbolic link";
      return false;
    }
    if (!S_ISREG(st.st_mode)) {
      error = "Destination is not a regular file";
      return false;
    }
  }
  const auto slash = path.find_last_of('/');
  const std::string dir = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
  const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  unsigned char rnd[8] = {};
  const int urandom = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (urandom >= 0) {
    const ssize_t got = ::read(urandom, rnd, sizeof(rnd));
    ::close(urandom);
    if (got != static_cast<ssize_t>(sizeof(rnd))) {
      const unsigned mix = static_cast<unsigned>(::getpid());
      std::memcpy(rnd, &mix, sizeof(mix));
    }
  }
  char suffix[40];
  std::snprintf(suffix, sizeof(suffix), ".%d.%02x%02x%02x%02x%02x%02x%02x%02x", ::getpid(), rnd[0],
                rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], rnd[6], rnd[7]);
  out.dest_path = path;
  out.tmp_path = dir + "/." + base + ".tmp" + suffix;
  out.fd = ::open(out.tmp_path.c_str(), O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY | O_CLOEXEC, 0600);
  if (out.fd < 0) {
    error = with_errno("Could not create a temporary file");
    out.tmp_path.clear();
    return false;
  }
  return true;
}

bool atomic_commit(AtomicFile& file, std::string& error) {
  if (file.fd < 0) {
    error = "No temporary file";
    return false;
  }
  if (::fsync(file.fd) != 0) {
    error = with_errno("Could not flush the temporary file");
    atomic_abort(file);
    return false;
  }
  if (::close(file.fd) != 0) {
    file.fd = -1;
    error = with_errno("Could not close the temporary file");
    if (!file.tmp_path.empty()) {
      ::unlink(file.tmp_path.c_str());
      file.tmp_path.clear();
    }
    return false;
  }
  file.fd = -1;
  if (::rename(file.tmp_path.c_str(), file.dest_path.c_str()) != 0) {
    error = with_errno("Could not replace the destination file");
    ::unlink(file.tmp_path.c_str());
    file.tmp_path.clear();
    return false;
  }
  file.tmp_path.clear();
  return true;
}

void atomic_abort(AtomicFile& file) {
  if (file.fd >= 0) {
    ::close(file.fd);
    file.fd = -1;
  }
  if (!file.tmp_path.empty()) {
    ::unlink(file.tmp_path.c_str());
    file.tmp_path.clear();
  }
}

ImageFormat format_from_path(const std::string& path) {
  const std::string ext = lower_ext(path);
  if (ext == ".png") {
    return ImageFormat::Png;
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return ImageFormat::Jpeg;
  }
  if (ext == ".bmp") {
    return ImageFormat::Bmp;
  }
  if (ext == ".ora") {
    return ImageFormat::Ora;
  }
  if (ext == ".gif") {
    return ImageFormat::Gif;
  }
  return ImageFormat::Unknown;
}

std::string format_extension(ImageFormat format) {
  switch (format) {
    case ImageFormat::Png:
      return ".png";
    case ImageFormat::Jpeg:
      return ".jpg";
    case ImageFormat::Bmp:
      return ".bmp";
    case ImageFormat::Ora:
      return ".ora";
    case ImageFormat::Gif:
      return ".gif";
    default:
      return ".png";
  }
}

LoadedImage load_flat_image(const std::string& path) {
  LoadedImage out;
  GError* error = nullptr;
  GdkPixbufAnimation* anim = gdk_pixbuf_animation_new_from_file(path.c_str(), &error);
  if (anim == nullptr) {
    out.error = error != nullptr ? error->message : "Could not open image";
    if (error != nullptr) {
      g_error_free(error);
    }
    return out;
  }
  out.animated = gdk_pixbuf_animation_is_static_image(anim) == FALSE;
  GdkPixbuf* pixbuf = gdk_pixbuf_animation_get_static_image(anim);
  if (pixbuf == nullptr) {
    out.error = "Could not read the first frame";
    g_object_unref(anim);
    return out;
  }
  g_object_ref(pixbuf);
  g_object_unref(anim);
  out.width = gdk_pixbuf_get_width(pixbuf);
  out.height = gdk_pixbuf_get_height(pixbuf);
  if (out.width > kHardMaxSide || out.height > kHardMaxSide) {
    out.error = "Image is larger than 16384 on a side";
    g_object_unref(pixbuf);
    return out;
  }
  copy_pixbuf_to_rgba(pixbuf, out.rgba, out.width, out.height);
  out.layer_name = basename_no_ext(path);
  g_object_unref(pixbuf);
  return out;
}

bool save_flat_image(const std::string& path, ImageFormat format, const std::uint8_t* rgba,
                     int width, int height, int stride, int jpeg_quality, std::string& error) {
  if (rgba == nullptr || width < 1 || height < 1) {
    error = "Nothing to save";
    return false;
  }
  std::vector<std::uint8_t> encoded;
  if (format == ImageFormat::Png || format == ImageFormat::Ora || format == ImageFormat::Unknown ||
      format == ImageFormat::Gif) {
    if (format != ImageFormat::Png) {
      error = "Unsupported flat image format";
      return false;
    }
    if (!encode_png_memory(rgba, width, height, stride, encoded, error)) {
      return false;
    }
  } else {
    const bool flatten = format == ImageFormat::Jpeg || format == ImageFormat::Bmp;
    GdkPixbuf* pixbuf = pixbuf_from_rgba(rgba, width, height, stride, flatten);
    if (pixbuf == nullptr) {
      error = "Could not allocate image buffer";
      return false;
    }
    gchar* buf = nullptr;
    gsize size = 0;
    GError* gerror = nullptr;
    gboolean ok = FALSE;
    if (format == ImageFormat::Jpeg) {
      if (jpeg_quality < 1) {
        jpeg_quality = 1;
      }
      if (jpeg_quality > 100) {
        jpeg_quality = 100;
      }
      char quality[8];
      std::snprintf(quality, sizeof(quality), "%d", jpeg_quality);
      ok = gdk_pixbuf_save_to_buffer(pixbuf, &buf, &size, "jpeg", &gerror, "quality", quality, nullptr);
    } else {
      ok = gdk_pixbuf_save_to_buffer(pixbuf, &buf, &size, "bmp", &gerror, nullptr);
    }
    g_object_unref(pixbuf);
    if (!ok || buf == nullptr) {
      error = gerror != nullptr ? gerror->message : "Save failed";
      if (gerror != nullptr) {
        g_error_free(gerror);
      }
      g_free(buf);
      return false;
    }
    encoded.assign(reinterpret_cast<std::uint8_t*>(buf), reinterpret_cast<std::uint8_t*>(buf) + size);
    g_free(buf);
  }

  AtomicFile file;
  if (!atomic_create(path, file, error)) {
    return false;
  }
  if (!write_all_fd(file.fd, encoded.data(), encoded.size())) {
    error = with_errno("Could not write the temporary file");
    atomic_abort(file);
    return false;
  }
  return atomic_commit(file, error);
}

bool encode_png_memory(const std::uint8_t* rgba, int width, int height, int stride,
                       std::vector<std::uint8_t>& out, std::string& error) {
  out.clear();
  if (rgba == nullptr || width < 1 || height < 1 || stride < width * 4) {
    error = "Could not allocate PNG buffer";
    return false;
  }
  std::vector<std::uint8_t> raw(static_cast<std::size_t>(height) *
                                (static_cast<std::size_t>(width) * 4 + 1));
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = raw.data() + static_cast<std::size_t>(y) * (static_cast<std::size_t>(width) * 4 + 1);
    row[0] = 0;
    std::memcpy(row + 1, rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                static_cast<std::size_t>(width) * 4);
  }
  uLongf bound = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> compressed(bound);
  const int z = compress2(compressed.data(), &bound, raw.data(), static_cast<uLong>(raw.size()),
                          Z_DEFAULT_COMPRESSION);
  if (z != Z_OK) {
    error = "PNG encode failed";
    return false;
  }
  compressed.resize(bound);

  std::uint8_t ihdr[13];
  ihdr[0] = static_cast<std::uint8_t>((static_cast<unsigned>(width) >> 24) & 0xff);
  ihdr[1] = static_cast<std::uint8_t>((static_cast<unsigned>(width) >> 16) & 0xff);
  ihdr[2] = static_cast<std::uint8_t>((static_cast<unsigned>(width) >> 8) & 0xff);
  ihdr[3] = static_cast<std::uint8_t>(static_cast<unsigned>(width) & 0xff);
  ihdr[4] = static_cast<std::uint8_t>((static_cast<unsigned>(height) >> 24) & 0xff);
  ihdr[5] = static_cast<std::uint8_t>((static_cast<unsigned>(height) >> 16) & 0xff);
  ihdr[6] = static_cast<std::uint8_t>((static_cast<unsigned>(height) >> 8) & 0xff);
  ihdr[7] = static_cast<std::uint8_t>(static_cast<unsigned>(height) & 0xff);
  ihdr[8] = 8;
  ihdr[9] = 6;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;

  static const std::uint8_t kSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  out.assign(kSignature, kSignature + 8);
  png_chunk(out, "IHDR", ihdr, sizeof(ihdr));
  png_chunk(out, "IDAT", compressed.data(), compressed.size());
  png_chunk(out, "IEND", nullptr, 0);
  return true;
}

bool decode_png_memory(const std::uint8_t* data, std::size_t size, LoadedImage& out) {
  out = {};
  if (data == nullptr || size == 0) {
    out.error = "Empty PNG";
    return false;
  }
  GError* error = nullptr;
  GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("png", &error);
  if (loader == nullptr) {
    out.error = error != nullptr ? error->message : "PNG loader failed";
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }
  if (!gdk_pixbuf_loader_write(loader, data, size, &error)) {
    out.error = error != nullptr ? error->message : "Truncated or corrupt PNG";
    if (error != nullptr) {
      g_error_free(error);
    }
    gdk_pixbuf_loader_close(loader, nullptr);
    g_object_unref(loader);
    return false;
  }
  if (!gdk_pixbuf_loader_close(loader, &error)) {
    out.error = error != nullptr ? error->message : "Truncated or corrupt PNG";
    if (error != nullptr) {
      g_error_free(error);
    }
    g_object_unref(loader);
    return false;
  }
  GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
  if (pixbuf == nullptr) {
    out.error = "PNG contained no image";
    g_object_unref(loader);
    return false;
  }
  g_object_ref(pixbuf);
  g_object_unref(loader);
  out.width = gdk_pixbuf_get_width(pixbuf);
  out.height = gdk_pixbuf_get_height(pixbuf);
  if (out.width > kHardMaxSide || out.height > kHardMaxSide) {
    out.error = "Image is larger than 16384 on a side";
    g_object_unref(pixbuf);
    return false;
  }
  copy_pixbuf_to_rgba(pixbuf, out.rgba, out.width, out.height);
  g_object_unref(pixbuf);
  return out.ok();
}

namespace {

void fit_preview(LoadedImage& image, int max_edge) {
  if (max_edge < 1 || (image.width <= max_edge && image.height <= max_edge)) {
    return;
  }
  const double scale = std::min(static_cast<double>(max_edge) / image.width,
                                 static_cast<double>(max_edge) / image.height);
  const int nw = std::max(1, static_cast<int>(std::lround(image.width * scale)));
  const int nh = std::max(1, static_cast<int>(std::lround(image.height * scale)));
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4);
  scale_bilinear(image.rgba.data(), image.width, image.height, image.width * 4, dest.data(), nw, nh,
                 nw * 4);
  image.rgba = std::move(dest);
  image.width = nw;
  image.height = nh;
}

bool preview_from_pixbuf(GdkPixbuf* pixbuf, int max_edge, LoadedImage& out) {
  if (pixbuf == nullptr) {
    return false;
  }
  out.width = gdk_pixbuf_get_width(pixbuf);
  out.height = gdk_pixbuf_get_height(pixbuf);
  if (out.width < 1 || out.height < 1) {
    g_object_unref(pixbuf);
    out = {};
    out.error = "Empty image";
    return false;
  }
  copy_pixbuf_to_rgba(pixbuf, out.rgba, out.width, out.height);
  g_object_unref(pixbuf);
  fit_preview(out, max_edge);
  return out.ok();
}

}  // namespace

bool load_image_preview(const std::string& path, int max_edge, LoadedImage& out) {
  out = {};
  if (path.empty()) {
    out.error = "No file";
    return false;
  }
  const ImageFormat format = format_from_path(path);
  if (format == ImageFormat::Ora) {
    std::vector<std::uint8_t> png;
    if (!load_ora_preview_png(path, png) || !decode_png_memory(png.data(), png.size(), out)) {
      out = {};
      out.error = "Unreadable OpenRaster preview";
      return false;
    }
    fit_preview(out, max_edge);
    return out.ok();
  }
  if (format != ImageFormat::Png && format != ImageFormat::Jpeg && format != ImageFormat::Bmp &&
      format != ImageFormat::Gif) {
    out.error = "Not an image";
    return false;
  }
  int info_w = 0;
  int info_h = 0;
  if (gdk_pixbuf_get_file_info(path.c_str(), &info_w, &info_h) == nullptr || info_w < 1 ||
      info_h < 1) {
    out.error = "Unreadable image";
    return false;
  }
  int target_w = info_w;
  int target_h = info_h;
  if (max_edge > 0 && (info_w > max_edge || info_h > max_edge)) {
    const double scale = std::min(static_cast<double>(max_edge) / info_w,
                                   static_cast<double>(max_edge) / info_h);
    target_w = std::max(1, static_cast<int>(std::lround(info_w * scale)));
    target_h = std::max(1, static_cast<int>(std::lround(info_h * scale)));
  }
  GError* error = nullptr;
  GdkPixbuf* pixbuf =
      gdk_pixbuf_new_from_file_at_scale(path.c_str(), target_w, target_h, TRUE, &error);
  if (pixbuf == nullptr) {
    out.error = error != nullptr ? error->message : "Unreadable image";
    if (error != nullptr) {
      g_error_free(error);
    }
    return false;
  }
  return preview_from_pixbuf(pixbuf, max_edge, out);
}

}  // namespace lundukepaint
