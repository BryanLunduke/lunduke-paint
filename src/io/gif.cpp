// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/image_io.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lundukepaint {
namespace {

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Box {
  int begin = 0;
  int end = 0;
};

struct Counted {
  Rgb color;
  int count = 0;
};

constexpr int kQuantBits = 5;
constexpr int kQuantShift = 8 - kQuantBits;
constexpr int kQuantLevels = 1 << kQuantBits;
constexpr int kHistSize = kQuantLevels * kQuantLevels * kQuantLevels;

int quant_index(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  const int rq = r >> kQuantShift;
  const int gq = g >> kQuantShift;
  const int bq = b >> kQuantShift;
  return (rq * kQuantLevels + gq) * kQuantLevels + bq;
}

std::uint8_t expand_quant(int value) {
  const int shifted = value << kQuantShift;
  return static_cast<std::uint8_t>(shifted | (value >> (kQuantBits - kQuantShift)));
}

int channel_range(const std::vector<Counted>& colors, int begin, int end, int channel) {
  int lo = 255;
  int hi = 0;
  for (int i = begin; i < end; ++i) {
    const int v = channel == 0 ? colors[i].color.r : (channel == 1 ? colors[i].color.g : colors[i].color.b);
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  return hi - lo;
}

void median_cut(std::vector<Counted>& colors, int limit) {
  if (static_cast<int>(colors.size()) <= limit) {
    return;
  }
  std::vector<Box> boxes{{0, static_cast<int>(colors.size())}};
  while (static_cast<int>(boxes.size()) < limit) {
    int best = -1;
    int best_range = -1;
    int best_channel = 0;
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
      if (boxes[i].end - boxes[i].begin < 2) {
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        const int range = channel_range(colors, boxes[i].begin, boxes[i].end, c);
        if (range > best_range) {
          best_range = range;
          best = i;
          best_channel = c;
        }
      }
    }
    if (best < 0) {
      break;
    }
    Box box = boxes[best];
    std::sort(colors.begin() + box.begin, colors.begin() + box.end,
              [best_channel](const Counted& a, const Counted& b) {
                const int av = best_channel == 0 ? a.color.r : (best_channel == 1 ? a.color.g : a.color.b);
                const int bv = best_channel == 0 ? b.color.r : (best_channel == 1 ? b.color.g : b.color.b);
                return av < bv;
              });
    int total = 0;
    for (int i = box.begin; i < box.end; ++i) {
      total += colors[i].count;
    }
    int acc = 0;
    int mid = box.begin;
    for (int i = box.begin; i < box.end - 1; ++i) {
      acc += colors[i].count;
      mid = i + 1;
      if (acc >= total / 2) {
        break;
      }
    }
    if (mid <= box.begin || mid >= box.end) {
      mid = box.begin + (box.end - box.begin) / 2;
    }
    boxes[best].end = mid;
    boxes.push_back(Box{mid, box.end});
  }
  std::vector<Counted> reduced;
  reduced.reserve(boxes.size());
  for (const Box& box : boxes) {
    long r = 0;
    long g = 0;
    long b = 0;
    int count = 0;
    for (int i = box.begin; i < box.end; ++i) {
      r += static_cast<long>(colors[i].color.r) * colors[i].count;
      g += static_cast<long>(colors[i].color.g) * colors[i].count;
      b += static_cast<long>(colors[i].color.b) * colors[i].count;
      count += colors[i].count;
    }
    if (count < 1) {
      continue;
    }
    reduced.push_back(Counted{Rgb{static_cast<std::uint8_t>(r / count),
                                  static_cast<std::uint8_t>(g / count),
                                  static_cast<std::uint8_t>(b / count)},
                              count});
  }
  colors.swap(reduced);
}

int nearest_rgb(Rgb color, const std::vector<Rgb>& palette, int first, int last) {
  int best = first;
  int best_d = 1 << 30;
  for (int i = first; i < last; ++i) {
    const int dr = static_cast<int>(color.r) - palette[i].r;
    const int dg = static_cast<int>(color.g) - palette[i].g;
    const int db = static_cast<int>(color.b) - palette[i].b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

// Open-addressed exact-color set. Stops at 257 distinct colors so a photo
// does not build a multi-million-entry tree.
struct ExactColors {
  static constexpr int kSlots = 1024;
  std::uint32_t key[kSlots]{};
  int count[kSlots]{};
  std::uint8_t used[kSlots]{};
  int distinct = 0;
  bool overflow = false;

  void add(std::uint32_t color) {
    if (overflow) {
      return;
    }
    std::uint32_t slot = (color * 2654435761u) & (kSlots - 1);
    for (int n = 0; n < kSlots; ++n) {
      if (used[slot] == 0) {
        if (distinct >= 256) {
          overflow = true;
          return;
        }
        used[slot] = 1;
        key[slot] = color;
        count[slot] = 1;
        ++distinct;
        return;
      }
      if (key[slot] == color) {
        ++count[slot];
        return;
      }
      slot = (slot + 1) & (kSlots - 1);
    }
    overflow = true;
  }

  int find(std::uint32_t color) const {
    std::uint32_t slot = (color * 2654435761u) & (kSlots - 1);
    for (int n = 0; n < kSlots; ++n) {
      if (used[slot] == 0) {
        return -1;
      }
      if (key[slot] == color) {
        return static_cast<int>(slot);
      }
      slot = (slot + 1) & (kSlots - 1);
    }
    return -1;
  }
};

struct BitPacker {
  int acc = 0;
  int bits = 0;
  std::vector<std::uint8_t> bytes;

  void write(int code, int size) {
    acc |= code << bits;
    bits += size;
    while (bits >= 8) {
      bytes.push_back(static_cast<std::uint8_t>(acc & 0xff));
      acc >>= 8;
      bits -= 8;
    }
  }

  void flush() {
    if (bits > 0) {
      bytes.push_back(static_cast<std::uint8_t>(acc & 0xff));
      acc = 0;
      bits = 0;
    }
  }
};

// Fixed open-address table. GIF codes stay under 4096, so a 8192-slot table
// stays sparse and does not rehash.
struct LzwDict {
  static constexpr int kSize = 8192;
  static constexpr int kMask = kSize - 1;
  std::vector<int> code;
  std::vector<std::uint32_t> key;

  LzwDict() : code(kSize, -1), key(kSize, 0) {}

  void clear() { std::fill(code.begin(), code.end(), -1); }

  int find(std::uint32_t packed) const {
    int slot = static_cast<int>((packed * 2654435761u) & kMask);
    for (int n = 0; n < kSize; ++n) {
      if (code[slot] < 0) {
        return -1;
      }
      if (key[slot] == packed) {
        return code[slot];
      }
      slot = (slot + 1) & kMask;
    }
    return -1;
  }

  void add(std::uint32_t packed, int value) {
    int slot = static_cast<int>((packed * 2654435761u) & kMask);
    while (code[slot] >= 0) {
      slot = (slot + 1) & kMask;
    }
    code[slot] = value;
    key[slot] = packed;
  }
};

void lzw_gif(const std::uint8_t* indices, int count, int width, int min_code_size,
             std::vector<std::uint8_t>& out, const GifProgress& progress, int row_base, int row_span) {
  const int clear_code = 1 << min_code_size;
  const int eoi = clear_code + 1;
  int code_size = min_code_size + 1;
  int next_code = eoi + 1;
  LzwDict dict;
  auto reset = [&]() {
    dict.clear();
    code_size = min_code_size + 1;
    next_code = eoi + 1;
  };
  BitPacker bits;
  bits.write(clear_code, code_size);
  if (count < 1) {
    bits.write(eoi, code_size);
    bits.flush();
  } else {
    int prefix = indices[0];
    for (int i = 1; i < count; ++i) {
      if (progress && width > 0 && (i & 65535) == 0) {
        progress(row_base + i / width, row_span);
      }
      const int k = indices[i];
      const std::uint32_t key = (static_cast<std::uint32_t>(prefix) << 8) | static_cast<std::uint32_t>(k);
      const int found = dict.find(key);
      if (found >= 0) {
        prefix = found;
        continue;
      }
      bits.write(prefix, code_size);
      if (next_code < 4096) {
        dict.add(key, next_code);
        ++next_code;
        // Bump after the code that fills the current width has been assigned.
        // `>=` grows one code early and decoders reject the stream.
        if (next_code > (1 << code_size) && code_size < 12) {
          ++code_size;
        }
      } else {
        bits.write(clear_code, code_size);
        reset();
      }
      prefix = k;
    }
    bits.write(prefix, code_size);
    bits.write(eoi, code_size);
    bits.flush();
  }
  const std::uint8_t* data = bits.bytes.data();
  int left = static_cast<int>(bits.bytes.size());
  while (left > 0) {
    const int n = std::min(255, left);
    out.push_back(static_cast<std::uint8_t>(n));
    out.insert(out.end(), data, data + n);
    data += n;
    left -= n;
  }
  out.push_back(0);
}

void append_u16(std::vector<std::uint8_t>& out, int value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

}  // namespace

bool encode_gif_memory(const std::uint8_t* rgba, int width, int height, int stride,
                       std::vector<std::uint8_t>& out, std::string& error, const GifProgress& progress) {
  out.clear();
  if (rgba == nullptr || width < 1 || height < 1 || stride < width * 4) {
    error = "Nothing to save";
    return false;
  }
  std::vector<int> hist(kHistSize, 0);
  ExactColors exact;
  bool any_clear = false;
  for (int y = 0; y < height; ++y) {
    if (progress && (y & 31) == 0) {
      progress(y, height * 2);
    }
    const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      if (p[3] < 128) {
        any_clear = true;
        continue;
      }
      ++hist[quant_index(p[0], p[1], p[2])];
      const std::uint32_t key = (static_cast<std::uint32_t>(p[0]) << 16) |
                                (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
      exact.add(key);
    }
  }
  const int color_limit = any_clear ? 255 : 256;
  const bool use_exact = !exact.overflow && exact.distinct > 0 && exact.distinct <= color_limit;
  std::vector<Counted> counted;
  if (use_exact) {
    counted.reserve(static_cast<std::size_t>(exact.distinct));
    for (int i = 0; i < ExactColors::kSlots; ++i) {
      if (exact.used[i] == 0) {
        continue;
      }
      Rgb color{static_cast<std::uint8_t>((exact.key[i] >> 16) & 0xff),
                static_cast<std::uint8_t>((exact.key[i] >> 8) & 0xff),
                static_cast<std::uint8_t>(exact.key[i] & 0xff)};
      counted.push_back(Counted{color, exact.count[i]});
    }
  } else {
    counted.reserve(1024);
    for (int i = 0; i < kHistSize; ++i) {
      if (hist[i] == 0) {
        continue;
      }
      const int rq = i / (kQuantLevels * kQuantLevels);
      const int gq = (i / kQuantLevels) % kQuantLevels;
      const int bq = i % kQuantLevels;
      counted.push_back(Counted{Rgb{expand_quant(rq), expand_quant(gq), expand_quant(bq)}, hist[i]});
    }
    if (static_cast<int>(counted.size()) > color_limit) {
      median_cut(counted, color_limit);
    }
  }
  std::vector<Rgb> palette;
  palette.push_back(Rgb{0, 0, 0});
  if (!any_clear && !counted.empty()) {
    palette.clear();
  }
  for (const Counted& entry : counted) {
    palette.push_back(entry.color);
  }
  if (palette.empty()) {
    palette.push_back(Rgb{0, 0, 0});
  }
  int bits = 2;
  while ((1 << bits) < static_cast<int>(palette.size())) {
    ++bits;
  }
  if (bits > 8) {
    error = "GIF palette is too large";
    return false;
  }
  const int used = static_cast<int>(palette.size());
  const int table = 1 << bits;
  while (static_cast<int>(palette.size()) < table) {
    palette.push_back(Rgb{0, 0, 0});
  }
  const int first = any_clear ? 1 : 0;
  std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  std::vector<std::uint8_t> lut;
  std::vector<int> exact_index(ExactColors::kSlots, 0);
  if (!use_exact) {
    lut.assign(kHistSize, static_cast<std::uint8_t>(first));
    for (int i = 0; i < kHistSize; ++i) {
      if (hist[i] == 0) {
        continue;
      }
      const int rq = i / (kQuantLevels * kQuantLevels);
      const int gq = (i / kQuantLevels) % kQuantLevels;
      const int bq = i % kQuantLevels;
      lut[i] = static_cast<std::uint8_t>(
          nearest_rgb(Rgb{expand_quant(rq), expand_quant(gq), expand_quant(bq)}, palette, first, used));
    }
  } else {
    for (int i = 0; i < ExactColors::kSlots; ++i) {
      if (exact.used[i] == 0) {
        continue;
      }
      Rgb color{static_cast<std::uint8_t>((exact.key[i] >> 16) & 0xff),
                static_cast<std::uint8_t>((exact.key[i] >> 8) & 0xff),
                static_cast<std::uint8_t>(exact.key[i] & 0xff)};
      exact_index[i] = nearest_rgb(color, palette, first, used);
    }
  }
  for (int y = 0; y < height; ++y) {
    if (progress && (y & 31) == 0) {
      progress(height + y, height * 2);
    }
    const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      if (p[3] < 128) {
        indices[i] = 0;
        continue;
      }
      if (use_exact) {
        const std::uint32_t key = (static_cast<std::uint32_t>(p[0]) << 16) |
                                  (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
        const int slot = exact.find(key);
        indices[i] = slot < 0 ? static_cast<std::uint8_t>(first)
                              : static_cast<std::uint8_t>(exact_index[slot]);
      } else {
        indices[i] = lut[quant_index(p[0], p[1], p[2])];
      }
    }
  }

  out = {'G', 'I', 'F', '8', '9', 'a'};
  append_u16(out, width);
  append_u16(out, height);
  out.push_back(static_cast<std::uint8_t>(0x80 | ((bits - 1) << 4) | (bits - 1)));
  out.push_back(0);
  out.push_back(0);
  for (const Rgb& color : palette) {
    out.push_back(color.r);
    out.push_back(color.g);
    out.push_back(color.b);
  }
  if (any_clear) {
    out.push_back(0x21);
    out.push_back(0xf9);
    out.push_back(4);
    out.push_back(0x01);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
  }
  out.push_back(0x2c);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u16(out, width);
  append_u16(out, height);
  out.push_back(0);
  out.push_back(static_cast<std::uint8_t>(bits));
  lzw_gif(indices.data(), width * height, width, bits, out, progress, height, height * 2);
  out.push_back(0x3b);
  if (progress) {
    progress(height * 2, height * 2);
  }
  return true;
}

}  // namespace lundukepaint
