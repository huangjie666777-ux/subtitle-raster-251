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

  if (failures == 0) {
    std::printf("all selftests passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
