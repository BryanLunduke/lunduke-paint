// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_SAVE_TARGET_HPP
#define LUNDUKEPAINT_IO_SAVE_TARGET_HPP

#include "io/image_io.hpp"

#include <string>

namespace lundukepaint {

// How a typed save/export name combines with the file-type filter.
//
// Save, Save As (including Save of a document that has no path yet, and the
// close prompt, which calls Save), and flat export all use resolve_save_target.
// There is no separate Export command: exporting is Save As to PNG, JPEG, or
// BMP. Crash recovery always writes its own .ora and does not come through
// here. A multi-layer document saved to a flat format still hits the existing
// flatten / "keep .ora" warnings in MainWindow after the name is resolved.
//
// Rule (final path component only; a dot in a directory is not an extension):
//
// * The candidate suffix is the text after the last '.'.
// * If that suffix matches a format the app knows, case-insensitively
//   (png, jpg, jpeg, bmp, ora, gif), the typed extension wins. The path is
//   kept exactly, including the user's capitalization, and the filter is
//   ignored. Only the last suffix counts, so "a.ora.png" is PNG and
//   "file.jpeg.BMP" is BMP. jpg and jpeg are the same format; a typed
//   ".jpeg" or ".JPG" is not rewritten to ".jpg".
// * GIF is recognized, so a typed ".gif" is not rewritten. Saving GIF is
//   still refused later ("GIF save is not supported").
// * There is no typed extension when the name has no '.', the only '.' is
//   a leading dot and the name is not itself a known extension (".hidden"),
//   the suffix is empty ("bft."), or the suffix is longer than 5 characters
//   ("my.drawing", "notes.backup"). The filter then adds its canonical
//   extension: "my.drawing" + OpenRaster -> "my.drawing.ora". A trailing
//   dot is replaced rather than doubled ("bft." -> "bft.ora"). JPEG's
//   canonical extension is ".jpg". The filter does not replace a suffix
//   the user typed.
// * A suffix of 1 to 5 ASCII letters or digits that is not a known format
//   ("bft.xyz", "notes.txt", "file.webp") is an unknown extension. It is
//   not silently rewritten to the filter's extension. The chooser asks to
//   save in the filter's format with that suffix kept and the filter
//   extension appended ("bft.xyz" -> "bft.xyz.ora"), or to cancel.
// * Leading and trailing spaces on the file name are removed.
//
// A suffix longer than 5 is treated as part of the title, not as an
// attempt to name a format. The longest format the app knows is "jpeg"
// (4). Five leaves room for a short unknown attempt such as "webp" or
// "tiff" without treating "drawing" as one.

enum class SaveResolveKind {
  Ready,          // path and format are final
  PromptAppend,   // unknown extension; offered_path appends the filter
  Unresolved,     // no usable type (empty name, or no extension and no filter)
};

struct SaveTarget {
  SaveResolveKind kind = SaveResolveKind::Unresolved;
  std::string path;
  ImageFormat format = ImageFormat::Unknown;
  std::string offered_path;
  ImageFormat offered_format = ImageFormat::Unknown;
  std::string message;
};

inline bool save_filter_writable(ImageFormat format) {
  return format == ImageFormat::Png || format == ImageFormat::Jpeg || format == ImageFormat::Bmp ||
         format == ImageFormat::Ora;
}

inline const char* save_format_label(ImageFormat format) {
  switch (format) {
    case ImageFormat::Png:
      return "PNG";
    case ImageFormat::Jpeg:
      return "JPEG";
    case ImageFormat::Bmp:
      return "BMP";
    case ImageFormat::Ora:
      return "OpenRaster";
    case ImageFormat::Gif:
      return "GIF";
    default:
      return "that format";
  }
}

namespace save_target_detail {

inline constexpr std::size_t kMaxTypedExtension = 5;

inline bool ascii_alnum(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline void split_name(const std::string& path, std::string& dir, std::string& base) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    dir.clear();
    base = path;
  } else {
    dir = path.substr(0, slash + 1);
    base = path.substr(slash + 1);
  }
  while (!base.empty() && (base.back() == ' ' || base.back() == '\t')) {
    base.pop_back();
  }
  std::size_t start = 0;
  while (start < base.size() && (base[start] == ' ' || base[start] == '\t')) {
    ++start;
  }
  if (start > 0) {
    base.erase(0, start);
  }
}

enum class SuffixKind { None, Known, Unknown };

struct Suffix {
  SuffixKind kind = SuffixKind::None;
  ImageFormat format = ImageFormat::Unknown;
  std::string text;
};

inline Suffix classify(const std::string& base) {
  Suffix out;
  const auto dot = base.find_last_of('.');
  if (dot == std::string::npos) {
    return out;
  }
  out.text = base.substr(dot + 1);
  const ImageFormat known = format_from_path(base);
  if (dot == 0) {
    if (known != ImageFormat::Unknown) {
      out.kind = SuffixKind::Known;
      out.format = known;
    }
    return out;
  }
  if (out.text.empty()) {
    return out;
  }
  if (known != ImageFormat::Unknown) {
    out.kind = SuffixKind::Known;
    out.format = known;
    return out;
  }
  if (out.text.size() <= kMaxTypedExtension) {
    bool alnum = true;
    for (unsigned char c : out.text) {
      if (!ascii_alnum(c)) {
        alnum = false;
        break;
      }
    }
    if (alnum) {
      out.kind = SuffixKind::Unknown;
    }
  }
  return out;
}

inline std::string append_filter_extension(const std::string& base, ImageFormat filter) {
  const std::string ext = format_extension(filter);
  if (!base.empty() && base.back() == '.') {
    return base.substr(0, base.size() - 1) + ext;
  }
  return base + ext;
}

inline std::string replace_known_suffix(const std::string& base, ImageFormat filter) {
  const std::string ext = format_extension(filter);
  const auto dot = base.find_last_of('.');
  if (dot == std::string::npos || dot == 0) {
    return base + ext;
  }
  return base.substr(0, dot) + ext;
}

inline std::string unsupported_suffix_sentence(const std::string& base, const std::string& suffix) {
  return "\"" + base + "\" ends with \"." + suffix +
         "\", which Lunduke Paint cannot save. Saved files are PNG, JPEG (.jpg or .jpeg), BMP, or "
         "OpenRaster (.ora).";
}

}  // namespace save_target_detail

inline SaveTarget resolve_save_target(const std::string& path, ImageFormat filter) {
  using namespace save_target_detail;
  SaveTarget target;
  std::string dir;
  std::string base;
  split_name(path, dir, base);
  if (base.empty()) {
    target.kind = SaveResolveKind::Unresolved;
    target.message = "Choose a file name.";
    return target;
  }
  const Suffix suffix = classify(base);
  if (suffix.kind == SuffixKind::Known) {
    target.kind = SaveResolveKind::Ready;
    target.path = dir + base;
    target.format = suffix.format;
    return target;
  }
  if (suffix.kind == SuffixKind::Unknown) {
    target.message = unsupported_suffix_sentence(base, suffix.text);
    if (!save_filter_writable(filter)) {
      target.kind = SaveResolveKind::Unresolved;
      target.message += " Choose Save As and pick PNG, JPEG, BMP, or OpenRaster.";
      return target;
    }
    target.kind = SaveResolveKind::PromptAppend;
    target.offered_format = filter;
    target.offered_path = dir + base + format_extension(filter);
    const auto slash = target.offered_path.find_last_of("/\\");
    const std::string offered_base =
        slash == std::string::npos ? target.offered_path : target.offered_path.substr(slash + 1);
    target.message += " Save it as " + std::string(save_format_label(filter)) +
                      ", keeping the typed name and adding " + format_extension(filter) + " (\"" +
                      offered_base + "\")?";
    return target;
  }
  if (!save_filter_writable(filter)) {
    target.kind = SaveResolveKind::Unresolved;
    target.message = "Choose a file type for \"" + base + "\".";
    return target;
  }
  target.kind = SaveResolveKind::Ready;
  target.format = filter;
  target.path = dir + append_filter_extension(base, filter);
  return target;
}

// When the file-type filter changes, rewrite the name only if the current
// suffix belongs to `previous` (the filter last wrote it, or the extension
// the dialog opened with) or the name has no typed extension. A known
// extension the user typed, and a short unknown extension, stay put.
inline std::string name_for_filter_change(const std::string& current_name, ImageFormat previous,
                                          ImageFormat next) {
  using namespace save_target_detail;
  if (!save_filter_writable(next) || next == previous) {
    return current_name;
  }
  std::string dir;
  std::string base;
  split_name(current_name, dir, base);
  if (base.empty()) {
    base = "untitled";
  }
  const Suffix suffix = classify(base);
  if (suffix.kind == SuffixKind::Known && suffix.format == previous) {
    return dir + replace_known_suffix(base, next);
  }
  if (suffix.kind == SuffixKind::Known || suffix.kind == SuffixKind::Unknown) {
    return current_name.empty() ? base : current_name;
  }
  return dir + append_filter_extension(base, next);
}

// The layered copy that sits beside a flat Save As. Uses the same suffix
// rules as Save As: a known image extension is replaced, a long title such
// as "my.drawing" is kept, and a short unknown suffix is kept and ".ora"
// is added ("archive.tar.png" -> "archive.tar.ora").
inline std::string companion_ora_path(const std::string& flat_path) {
  using namespace save_target_detail;
  std::string dir;
  std::string base;
  split_name(flat_path, dir, base);
  if (base.empty()) {
    return dir + "untitled.ora";
  }
  const Suffix suffix = classify(base);
  std::string stem = base;
  if (suffix.kind == SuffixKind::Known && suffix.format != ImageFormat::Ora) {
    const auto dot = base.find_last_of('.');
    if (dot != std::string::npos) {
      stem = base.substr(0, dot);
    }
  }
  const SaveTarget target = resolve_save_target(dir + stem, ImageFormat::Ora);
  if (target.kind == SaveResolveKind::Ready && !target.path.empty()) {
    return target.path;
  }
  if (target.kind == SaveResolveKind::PromptAppend && !target.offered_path.empty()) {
    return target.offered_path;
  }
  if (!stem.empty() && stem.back() == '.') {
    stem.pop_back();
  }
  if (stem.empty()) {
    stem = "untitled";
  }
  return dir + stem + ".ora";
}

// True when an existing companion is some other file, not this document's
// own project or the companion it already updates.
inline bool ora_companion_needs_confirm(bool exists, bool is_own_file) {
  return exists && !is_own_file;
}

inline bool flat_save_needs_warning(ImageFormat format, bool multi, bool transparent) {
  if (format == ImageFormat::Ora || format == ImageFormat::Gif || format == ImageFormat::Unknown) {
    return false;
  }
  if (multi) {
    return true;
  }
  return transparent && (format == ImageFormat::Jpeg || format == ImageFormat::Bmp);
}

// One sentence for the single flatten warning. PNG, JPEG, and BMP each say
// what that format actually does to layers and alpha.
inline std::string flat_save_message(ImageFormat format, bool multi, bool transparent) {
  std::string message;
  if (multi) {
    message = "This document has multiple layers. Saving a flat file will flatten visible layers.";
  }
  if (format == ImageFormat::Png && multi) {
    message += " PNG keeps the alpha of the flattened image.";
  } else if (format == ImageFormat::Jpeg && (multi || transparent)) {
    if (!message.empty()) {
      message += " ";
    }
    message += "JPEG cannot store transparency or layers. The image will be flattened onto white.";
  } else if (format == ImageFormat::Bmp && (multi || transparent)) {
    if (!message.empty()) {
      message += " ";
    }
    message += "BMP cannot store transparency or layers. The image will be flattened onto white.";
  }
  return message;
}

// Save As asks once. A later save of the same document to the same target
// (same path, format, and whether layers or transparency are involved) does not.
inline std::string flat_save_ack_key(const std::string& path, ImageFormat format, bool multi,
                                    bool transparent) {
  std::string key = path;
  key.push_back('\n');
  key += format_extension(format);
  key.push_back(multi ? 'M' : 's');
  key.push_back(transparent ? 'T' : 'o');
  return key;
}

inline bool should_warn_flat_save(ImageFormat format, bool multi, bool transparent,
                                 bool already_confirmed, bool acked) {
  if (already_confirmed || acked) {
    return false;
  }
  return flat_save_needs_warning(format, multi, transparent);
}

// JPEG quality is shown only for the format that will actually be written.
// A typed extension wins over the file-type filter, matching Save As.
inline bool save_shows_jpeg_quality(const std::string& typed_name, ImageFormat filter) {
  if (typed_name.empty()) {
    return filter == ImageFormat::Jpeg;
  }
  const SaveTarget target = resolve_save_target(typed_name, filter);
  if (target.kind == SaveResolveKind::Ready) {
    return target.format == ImageFormat::Jpeg;
  }
  if (target.kind == SaveResolveKind::PromptAppend) {
    return target.offered_format == ImageFormat::Jpeg;
  }
  return filter == ImageFormat::Jpeg;
}

}  // namespace lundukepaint

#endif
