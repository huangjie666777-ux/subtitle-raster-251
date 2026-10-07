#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "subtitle_raster251/subtitle_raster251.h"

using namespace subtitle_raster251;

static int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                \
    }                                                            \
  } while (0)

static bool LaysOut(Engine* e, const std::string& text,
                    BaseDirection dir, Layout* out) {
  return e->LayoutLine(text, 20.0, dir, out) == Error::kOk;
}

int main() {
  Engine engine("fonts/DejaVuSans.ttf");
  CHECK(engine.GetError() == Error::kOk);

  Layout lay;

  // Empty text succeeds.
  CHECK(LaysOut(&engine, "", BaseDirection::kAuto, &lay));
  CHECK(lay.glyphs.empty() && lay.width == 0.0);

  // Invalid inputs.
  CHECK(engine.LayoutLine("\xff\xfe", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kInvalidUtf8);
  CHECK(engine.LayoutLine("a\xC0\xAF", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kInvalidUtf8);  // Overlong.
  CHECK(engine.LayoutLine("a\nb", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kNewline);
  CHECK(engine.LayoutLine("a", 0.0, BaseDirection::kAuto, &lay) ==
        Error::kBadFontSize);
  CHECK(engine.LayoutLine("a", -3.0, BaseDirection::kAuto, &lay) ==
        Error::kBadFontSize);
  {
    std::string big;
    for (int i = 0; i < 4097; ++i) big += 'a';
    CHECK(engine.LayoutLine(big, 20.0, BaseDirection::kAuto, &lay) ==
          Error::kTooLong);
  }
  // Missing glyph: CJK is not in DejaVuSans.
  CHECK(engine.LayoutLine("\u4E2D", 20.0, BaseDirection::kAuto, &lay) ==
        Error::kMissingGlyph);

  // Pure Latin LTR.
  CHECK(LaysOut(&engine, "Hello", BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kLtr);
  CHECK(lay.glyphs.size() == 5);
  CHECK(lay.width > 0.0);
  for (size_t i = 0; i < 5; ++i) {
    CHECK(lay.glyphs[i].cluster_begin == i);
    CHECK(lay.glyphs[i].cluster_end == i + 1);
  }

  // Pure Hebrew, auto direction resolves RTL.
  const std::string hebrew = "\u05E9\u05DC\u05D5\u05DD";
  CHECK(LaysOut(&engine, hebrew, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kRtl);
  CHECK(lay.glyphs.size() == 4);
  // Visual order is reversed: first glyph is the last character.
  CHECK(lay.glyphs[0].cluster_begin == 6);
  CHECK(lay.glyphs[3].cluster_begin == 0);

  // Mixed Latin + Hebrew + digits.
  const std::string mixed = "ab " + hebrew + " 12";
  CHECK(LaysOut(&engine, mixed, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kLtr);
  CHECK(lay.width > 0.0);
  // First three glyphs are "ab " in logical order.
  CHECK(lay.glyphs[0].cluster_begin == 0);
  CHECK(lay.glyphs[1].cluster_begin == 1);
  CHECK(lay.glyphs[2].cluster_begin == 2);
  // Per UBA, the trailing European digits stay LTR and land visually
  // before the Hebrew word: a b sp 1 2 sp <reversed Hebrew> sp.
  CHECK(lay.glyphs[3].cluster_begin == 12);  // '1'
  CHECK(lay.glyphs[4].cluster_begin == 13);  // '2'
  size_t last = lay.glyphs.size() - 1;
  CHECK(lay.glyphs[last].cluster_begin == 3);  // First Hebrew letter.

  // Arabic shaping: 4 letters join, glyph count stays 4, advances > 0.
  const std::string arabic = "\u0645\u0631\u062D\u0628\u0627";
  CHECK(LaysOut(&engine, arabic, BaseDirection::kAuto, &lay));
  CHECK(lay.resolved_direction == BaseDirection::kRtl);
  CHECK(lay.glyphs.size() == 5);
  for (const auto& g : lay.glyphs) CHECK(g.advance_x > 0.0);

  // Combining mark merges into one cluster covering both code points.
  const std::string accent = "e\u0301";
  CHECK(LaysOut(&engine, accent, BaseDirection::kLtr, &lay));
  CHECK(lay.glyphs.size() >= 1);
  CHECK(lay.glyphs[0].cluster_begin == 0);
  CHECK(lay.glyphs[0].cluster_end == accent.size());

  // Mirroring: "(" in RTL context shapes as its mirror glyph.
  Layout ltr_paren, rtl_paren;
  CHECK(LaysOut(&engine, "(", BaseDirection::kLtr, &ltr_paren));
  CHECK(LaysOut(&engine, "(", BaseDirection::kRtl, &rtl_paren));
  CHECK(ltr_paren.glyphs[0].glyph_id != rtl_paren.glyphs[0].glyph_id);

  // Bidi isolate controls produce no visible glyphs.
  const std::string iso = "a\u2066\u05D1\u2069b";
  CHECK(LaysOut(&engine, iso, BaseDirection::kLtr, &lay));
  for (const auto& g : lay.glyphs) {
    if (g.cluster_begin == 1 || g.cluster_begin == 6) {
      CHECK(g.glyph_id == 0 && g.advance_x == 0.0);
    }
  }

  // Space keeps a real advance.
  CHECK(LaysOut(&engine, "a b", BaseDirection::kLtr, &lay));
  CHECK(lay.glyphs[1].advance_x > 0.0);

  // Selection: full range covers the whole line width.
  std::vector<SelectionRange> ranges;
  CHECK(LaysOut(&engine, mixed, BaseDirection::kAuto, &lay));
  CHECK(engine.SelectionRanges(lay, 0, mixed.size(), &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(std::fabs(ranges[0].x0 - 0.0) < 1e-6);
  CHECK(std::fabs(ranges[0].x1 - lay.width) < 1e-6);

  // Empty selection returns empty.
  CHECK(engine.SelectionRanges(lay, 2, 2, &ranges) == Error::kOk);
  CHECK(ranges.empty());

  // Latin-only prefix selection is a single range at the left edge.
  CHECK(engine.SelectionRanges(lay, 0, 2, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(ranges[0].x0 == 0.0);
  CHECK(ranges[0].x1 > 0.0 && ranges[0].x1 < lay.width);

  // Hebrew word selection lands in the middle of the line.
  CHECK(engine.SelectionRanges(lay, 3, 3 + 8, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(ranges[0].x0 > 0.0);

  // Selection crossing the boundary yields two ranges (gap unselected).
  CHECK(engine.SelectionRanges(lay, 0, 3 + 8, &ranges) == Error::kOk);
  CHECK(ranges.size() == 2);
  CHECK(ranges[1].x0 > ranges[0].x1);

  // Partial cluster coverage selects the whole cluster.
  CHECK(LaysOut(&engine, accent, BaseDirection::kLtr, &lay));
  CHECK(engine.SelectionRanges(lay, 0, 1, &ranges) == Error::kOk);
  CHECK(ranges.size() == 1);
  CHECK(std::fabs(ranges[0].x1 - lay.width) < 1e-6);

  // Bad selections are rejected.
  CHECK(engine.SelectionRanges(lay, 2, 1, &ranges) == Error::kBadSelection);
  CHECK(engine.SelectionRanges(lay, 0, lay.text_size + 1, &ranges) ==
        Error::kBadSelection);
  CHECK(engine.SelectionRanges(lay, 1, 2, &ranges) ==
        Error::kBadSelection);  // Mid-code-point (U+0301 is 2 bytes).

  // --- Rasterization ---
  const Color white{255, 255, 255, 255};
  const Color black{0, 0, 0, 255};
  RasterImage img;

  // Parameter validation.
  CHECK(engine.RenderLine("a", 7.9, BaseDirection::kLtr, white, black, 0.0,
                          0, &img) == Error::kBadFontSize);
  CHECK(engine.RenderLine("a", 128.1, BaseDirection::kLtr, white, black,
                          0.0, 0, &img) == Error::kBadFontSize);
  CHECK(engine.RenderLine("a", 20.0, BaseDirection::kLtr, white, black,
                          -1.0, 0, &img) == Error::kBadStrokeRadius);
  CHECK(engine.RenderLine("a", 20.0, BaseDirection::kLtr, white, black,
                          8.1, 0, &img) == Error::kBadStrokeRadius);
  CHECK(engine.RenderLine("a", 20.0, BaseDirection::kLtr, white, black,
                          0.0, 33, &img) == Error::kBadPadding);
  CHECK(engine.RenderLine("a", 20.0, BaseDirection::kLtr, white, black,
                          0.0, -1, &img) == Error::kBadPadding);
  // Text validation is reused.
  CHECK(engine.RenderLine("a\nb", 20.0, BaseDirection::kLtr, white, black,
                          0.0, 0, &img) == Error::kNewline);

  // Empty text: 0x0 image, zero line width.
  CHECK(engine.RenderLine("", 20.0, BaseDirection::kLtr, white, black, 1.0,
                          4, &img) == Error::kOk);
  CHECK(img.width == 0 && img.height == 0 && img.pixels.empty());
  CHECK(img.line_width == 0.0);

  // Basic render: nonzero canvas, line width matches layout width.
  CHECK(engine.RenderLine("Hello", 24.0, BaseDirection::kLtr, white, black,
                          2.0, 4, &img) == Error::kOk);
  CHECK(img.width > 0 && img.height > 0);
  CHECK(engine.LayoutLine("Hello", 24.0, BaseDirection::kLtr, &lay) ==
        Error::kOk);
  CHECK(std::fabs(img.line_width - lay.width) < 1e-9);
  CHECK(img.origin_x <= 0.0 + 6.0);  // Stroke + padding reach left.
  CHECK(img.origin_y < 0.0);         // Ascenders above the baseline.
  CHECK(img.pixels.size() ==
        static_cast<size_t>(img.width) * img.height * 4);
  // Zero-alpha pixels have zero RGB; some fully opaque fill exists.
  bool saw_opaque = false;
  for (size_t i = 0; i < img.pixels.size(); i += 4) {
    if (img.pixels[i + 3] == 0) {
      CHECK(img.pixels[i] == 0 && img.pixels[i + 1] == 0 &&
            img.pixels[i + 2] == 0);
    }
    if (img.pixels[i + 3] == 255) saw_opaque = true;
  }
  CHECK(saw_opaque);

  // Stroke extends the canvas beyond the fill-only canvas.
  RasterImage fill_only;
  CHECK(engine.RenderLine("Hello", 24.0, BaseDirection::kLtr, white, black,
                          0.0, 0, &fill_only) == Error::kOk);
  RasterImage stroked;
  CHECK(engine.RenderLine("Hello", 24.0, BaseDirection::kLtr, white, black,
                          3.0, 0, &stroked) == Error::kOk);
  CHECK(stroked.width > fill_only.width);
  CHECK(stroked.height > fill_only.height);
  // Padding expands the canvas further, symmetrically.
  RasterImage padded;
  CHECK(engine.RenderLine("Hello", 24.0, BaseDirection::kLtr, white, black,
                          0.0, 5, &padded) == Error::kOk);
  CHECK(padded.width == fill_only.width + 10);
  CHECK(padded.height == fill_only.height + 10);
  CHECK(padded.origin_x == fill_only.origin_x - 5);
  CHECK(padded.origin_y == fill_only.origin_y - 5);

  // Stroke pixels exist outside the fill: with a black stroke and
  // white fill, edge pixels between them are dark.
  {
    bool saw_dark_edge = false;
    for (size_t i = 0; i < stroked.pixels.size(); i += 4) {
      uint8_t a = stroked.pixels[i + 3];
      if (a > 0 && stroked.pixels[i] < 64) saw_dark_edge = true;
    }
    CHECK(saw_dark_edge);
  }

  // Mixed-direction line renders and keeps the layout line width.
  const std::string mixed_rtl =
      "\u0645\u0631\u062D\u0628\u0627 \u05E9\u05DC\u05D5\u05DD Hi";
  CHECK(engine.RenderLine(mixed_rtl, 28.0, BaseDirection::kAuto, white,
                          black, 1.5, 2, &img) == Error::kOk);
  CHECK(engine.LayoutLine(mixed_rtl, 28.0, BaseDirection::kAuto, &lay) ==
        Error::kOk);
  CHECK(std::fabs(img.line_width - lay.width) < 1e-9);

  // Combining mark ink is not clipped: the canvas reaches above the
  // mark-free canvas top by at least the mark's presence.
  RasterImage with_mark, without_mark;
  CHECK(engine.RenderLine("e\u0301", 32.0, BaseDirection::kLtr, white,
                          black, 0.0, 0, &with_mark) == Error::kOk);
  CHECK(engine.RenderLine("e", 32.0, BaseDirection::kLtr, white, black,
                          0.0, 0, &without_mark) == Error::kOk);
  CHECK(with_mark.origin_y <= without_mark.origin_y);

  // Pixel limit: a long line at the maximum size exceeds 4M pixels.
  {
    std::string big(4000, 'W');
    CHECK(engine.RenderLine(big, 128.0, BaseDirection::kLtr, white, black,
                            0.0, 0, &img) == Error::kImageTooLarge);
  }

  // PNG round trip: file exists and starts with the PNG signature.
  CHECK(engine.RenderLine("Hi", 20.0, BaseDirection::kLtr, white, black,
                          1.0, 2, &img) == Error::kOk);
  CHECK(Engine::SavePng(img, "build/selftest.png") == Error::kOk);
  {
    std::FILE* fp = std::fopen("build/selftest.png", "rb");
    CHECK(fp != nullptr);
    if (fp) {
      unsigned char sig[8] = {0};
      CHECK(std::fread(sig, 1, 8, fp) == 8);
      CHECK(sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' &&
            sig[3] == 'G');
      std::fclose(fp);
    }
  }
  CHECK(Engine::SavePng(img, "/nonexistent-dir/x.png") == Error::kIoError);


  if (failures == 0) {
    std::printf("all selftests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
