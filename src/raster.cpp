// Single-line subtitle rasterization: FreeType grayscale glyph
// rendering, rounded stroke dilation, max-coverage line masks, and
// source-over compositing into a transparent RGBA8 bitmap.
#include <png.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "subtitle_raster251/subtitle_raster251.h"
#include "engine_impl.h"

namespace subtitle_raster251 {

namespace {

constexpr double kMinFontSize = 8.0;
constexpr double kMaxFontSize = 128.0;
constexpr double kMaxStrokeRadius = 8.0;
constexpr int kMaxPadding = 32;
constexpr int64_t kMaxPixels = 4000000;

// One rendered glyph bitmap positioned on the line, in integer device
// pixels. Coordinates follow the layout convention: x grows right,
// y grows up, origin at the left end of the line baseline.
struct GlyphBitmap {
  std::vector<uint8_t> coverage;  // width * rows, top row first.
  int width = 0;
  int rows = 0;
  int left = 0;  // Left edge x.
  int top = 0;   // Top edge y (up-positive).
};

// Renders one glyph with subpixel positioning. The fractional part of
// the pen position is applied through a 26.6 translation delta so the
// antialiasing matches the shaped (fractional) glyph positions.
GlyphBitmap RenderGlyph(FT_Face face, uint32_t glyph_id, double x,
                        double y) {
  GlyphBitmap out;
  double base_x = std::floor(x);
  double base_y = std::floor(y);
  FT_Vector delta;
  delta.x = static_cast<FT_Pos>(std::lround((x - base_x) * 64.0));
  delta.y = static_cast<FT_Pos>(std::lround((y - base_y) * 64.0));
  if (delta.x == 64) { delta.x = 0; base_x += 1.0; }
  if (delta.y == 64) { delta.y = 0; base_y += 1.0; }

  FT_Set_Transform(face, nullptr, &delta);
  if (FT_Load_Glyph(face, glyph_id,
                    FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
    FT_Set_Transform(face, nullptr, nullptr);
    return out;
  }
  FT_GlyphSlot slot = face->glyph;
  if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
    FT_Set_Transform(face, nullptr, nullptr);
    return out;
  }
  FT_Set_Transform(face, nullptr, nullptr);

  const FT_Bitmap& bm = slot->bitmap;
  if (bm.width == 0 || bm.rows == 0) return out;
  out.width = static_cast<int>(bm.width);
  out.rows = static_cast<int>(bm.rows);
  // bitmap_left may be negative (bearing left of the pen); bitmap_top
  // is the distance from the baseline to the top row, y up.
  out.left = static_cast<int>(base_x) + slot->bitmap_left;
  out.top = static_cast<int>(base_y) + slot->bitmap_top;
  out.coverage.resize(static_cast<size_t>(out.width) * out.rows);
  for (int r = 0; r < out.rows; ++r) {
    const uint8_t* src =
        bm.buffer + static_cast<size_t>(r) * std::abs(bm.pitch);
    std::copy_n(src, out.width,
                out.coverage.data() + static_cast<size_t>(r) * out.width);
  }
  return out;
}

// Dilates a coverage mask with a rounded (disk) kernel of the given
// radius. The outermost pixel ring is weighted by partial coverage so
// the stroke edge stays antialiased.
std::vector<uint8_t> DilateMask(const std::vector<uint8_t>& src, int w,
                                int h, double radius) {
  const int k = static_cast<int>(std::ceil(radius));
  std::vector<uint8_t> weights;
  std::vector<int> off_x;
  std::vector<int> off_y;
  for (int dy = -k; dy <= k; ++dy) {
    for (int dx = -k; dx <= k; ++dx) {
      double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
      double cov = radius + 0.5 - dist;  // Linear AA fringe at the rim.
      if (cov <= 0.0) continue;
      int weight = cov >= 1.0 ? 255 : static_cast<int>(cov * 255.0 + 0.5);
      weights.push_back(static_cast<uint8_t>(weight));
      off_x.push_back(dx);
      off_y.push_back(dy);
    }
  }
  std::vector<uint8_t> dst(src.size(), 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t i = static_cast<size_t>(y) * w + x;
      if (src[i] == 0) continue;
      for (size_t t = 0; t < weights.size(); ++t) {
        int nx = x + off_x[t];
        int ny = y + off_y[t];
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
        size_t j = static_cast<size_t>(ny) * w + nx;
        int v = src[i] * weights[t] / 255;
        if (v > dst[j]) dst[j] = static_cast<uint8_t>(v);
      }
    }
  }
  return dst;
}

}  // namespace

Error Engine::RenderLine(const std::string& utf8_text, double font_size,
                         BaseDirection base_direction, Color fill,
                         Color stroke, double stroke_radius, int padding,
                         RasterImage* out) {
  *out = RasterImage();
  if (GetError() != Error::kOk) return GetError();
  if (!std::isfinite(font_size) || font_size < kMinFontSize ||
      font_size > kMaxFontSize) {
    return Error::kBadFontSize;
  }
  if (!std::isfinite(stroke_radius) || stroke_radius < 0.0 ||
      stroke_radius > kMaxStrokeRadius) {
    return Error::kBadStrokeRadius;
  }
  if (padding < 0 || padding > kMaxPadding) return Error::kBadPadding;

  Layout layout;
  Error err = LayoutLine(utf8_text, font_size, base_direction, &layout);
  if (err != Error::kOk) return err;
  out->line_width = layout.width;

  // Rasterize every visible glyph at its shaped position.
  std::vector<GlyphBitmap> bitmaps;
  bitmaps.reserve(layout.glyphs.size());
  FT_Set_Char_Size(impl_->ft_face, 0,
                   static_cast<FT_F26Dot6>(font_size * 64.0), 72, 72);
  for (const Glyph& g : layout.glyphs) {
    if (g.glyph_id == 0) continue;  // Invisible bidi controls.
    GlyphBitmap bm = RenderGlyph(impl_->ft_face, g.glyph_id, g.x, g.y);
    if (bm.width > 0) bitmaps.push_back(std::move(bm));
  }
  if (bitmaps.empty()) return Error::kOk;  // No ink: 0x0 image.

  // Ink bounding box in y-up device pixels, expanded by the stroke
  // reach (plus its antialias fringe) and the requested padding.
  int min_x = bitmaps[0].left, max_x = bitmaps[0].left + bitmaps[0].width;
  int max_y = bitmaps[0].top, min_y = bitmaps[0].top - bitmaps[0].rows;
  for (const GlyphBitmap& bm : bitmaps) {
    min_x = std::min(min_x, bm.left);
    max_x = std::max(max_x, bm.left + bm.width);
    max_y = std::max(max_y, bm.top);
    min_y = std::min(min_y, bm.top - bm.rows);
  }
  const int grow =
      padding + (stroke_radius > 0.0
                     ? static_cast<int>(std::ceil(stroke_radius)) + 1
                     : 0);
  min_x -= grow;
  max_x += grow;
  min_y -= grow;
  max_y += grow;

  const int64_t w = static_cast<int64_t>(max_x) - min_x;
  const int64_t h = static_cast<int64_t>(max_y) - min_y;
  if (w * h > kMaxPixels) return Error::kImageTooLarge;
  out->width = static_cast<int>(w);
  out->height = static_cast<int>(h);
  out->origin_x = min_x;   // Top-left corner relative to the baseline
  out->origin_y = -max_y;  // origin, x right / y down.

  // Merge all glyph bitmaps into a single line fill mask by maximum
  // coverage, so overlapping glyphs never darken each other.
  std::vector<uint8_t> fill_mask(static_cast<size_t>(w) * h, 0);
  for (const GlyphBitmap& bm : bitmaps) {
    const int ox = bm.left - min_x;
    const int oy = max_y - bm.top;  // Flip y: canvas rows grow down.
    for (int r = 0; r < bm.rows; ++r) {
      uint8_t* dst =
          fill_mask.data() + static_cast<size_t>(oy + r) * w + ox;
      const uint8_t* src =
          bm.coverage.data() + static_cast<size_t>(r) * bm.width;
      for (int c = 0; c < bm.width; ++c) {
        if (src[c] > dst[c]) dst[c] = src[c];
      }
    }
  }

  // The stroke mask is a rounded dilation of the merged fill mask, so
  // it also merges by maximum coverage and never overlaps itself.
  std::vector<uint8_t> stroke_mask;
  if (stroke_radius > 0.0 && stroke.a > 0) {
    stroke_mask =
        DilateMask(fill_mask, out->width, out->height, stroke_radius);
  }

  // Composite stroke first, then fill, both source-over. Work in
  // non-premultiplied form; fully transparent pixels keep zero RGB.
  out->pixels.resize(static_cast<size_t>(w) * h * 4, 0);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    double sa = stroke_mask.empty()
                    ? 0.0
                    : stroke_mask[i] / 255.0 * (stroke.a / 255.0);
    double fa = fill_mask[i] / 255.0 * (fill.a / 255.0);
    double out_a = fa + sa * (1.0 - fa);
    uint8_t* px = out->pixels.data() + i * 4;
    if (out_a <= 0.0) continue;  // RGB stays 0.
    double r = (fill.r * fa + stroke.r * sa * (1.0 - fa)) / out_a;
    double g = (fill.g * fa + stroke.g * sa * (1.0 - fa)) / out_a;
    double b = (fill.b * fa + stroke.b * sa * (1.0 - fa)) / out_a;
    px[0] = static_cast<uint8_t>(r + 0.5);
    px[1] = static_cast<uint8_t>(g + 0.5);
    px[2] = static_cast<uint8_t>(b + 0.5);
    px[3] = static_cast<uint8_t>(out_a * 255.0 + 0.5);
  }
  return Error::kOk;
}

Error Engine::SavePng(const RasterImage& image, const std::string& path) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) return Error::kIoError;
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                            nullptr, nullptr, nullptr);
  png_infop info = png ? png_create_info_struct(png) : nullptr;
  Error result = Error::kOk;
  if (!info || setjmp(png_jmpbuf(png))) {
    result = Error::kIoError;
  } else {
    png_init_io(png, fp);
    png_set_IHDR(png, info, image.width, image.height, 8,
                 PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    std::vector<png_bytep> rows(image.height);
    for (int y = 0; y < image.height; ++y) {
      rows[y] = const_cast<png_bytep>(
          image.pixels.data() + static_cast<size_t>(y) * image.width * 4);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
  }
  if (info) png_destroy_write_struct(&png, &info);
  std::fclose(fp);
  return result;
}

}  // namespace subtitle_raster251

