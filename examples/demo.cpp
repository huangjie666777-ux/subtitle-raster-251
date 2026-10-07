// Mixed Arabic/Hebrew/Latin line layout and selection mapping demo.
#include <cstdio>
#include <string>
#include <vector>

#include "subtitle_raster251/subtitle_raster251.h"

using subtitle_raster251::BaseDirection;
using subtitle_raster251::Engine;
using subtitle_raster251::Error;
using subtitle_raster251::Layout;
using subtitle_raster251::SelectionRange;

static void Dump(Engine& engine, const std::string& text,
                 BaseDirection dir) {
  Layout layout;
  Error err = engine.LayoutLine(text, 24.0, dir, &layout);
  if (err != Error::kOk) {
    std::printf("layout failed: %s\n", subtitle_raster251::ErrorMessage(err));
    return;
  }
  std::printf("text: %s\n", text.c_str());
  std::printf("  direction: %s  width: %.2f px  glyphs: %zu\n",
              layout.resolved_direction == BaseDirection::kLtr ? "LTR"
                                                               : "RTL",
              layout.width, layout.glyphs.size());
  for (const auto& g : layout.glyphs) {
    std::printf(
        "  gid=%5u cluster=[%2zu,%2zu) x=%7.2f y=%5.2f adv=(%.2f,%.2f) "
        "off=(%.2f,%.2f)\n",
        g.glyph_id, g.cluster_begin, g.cluster_end, g.x, g.y,
        g.advance_x, g.advance_y, g.offset_x, g.offset_y);
  }
}

static void DumpSelection(const Engine& engine, const Layout& layout,
                          size_t begin, size_t end) {
  std::vector<SelectionRange> ranges;
  Error err = engine.SelectionRanges(layout, begin, end, &ranges);
  if (err != Error::kOk) {
    std::printf("  selection [%zu,%zu): %s\n", begin, end,
                subtitle_raster251::ErrorMessage(err));
    return;
  }
  std::printf("  selection [%zu,%zu):", begin, end);
  for (const auto& r : ranges) std::printf("  [%.2f, %.2f)", r.x0, r.x1);
  std::printf("\n");
}

int main() {
  Engine engine("fonts/DejaVuSans.ttf");
  if (engine.GetError() != Error::kOk) {
    std::fprintf(stderr, "font error: %s\n",
                 subtitle_raster251::ErrorMessage(engine.GetError()));
    return 1;
  }

  // Hebrew + Latin + digits.
  const std::string mixed_he =
      "abc \u05E9\u05DC\u05D5\u05DD 123 xyz";
  Layout layout;
  Dump(engine, mixed_he, BaseDirection::kAuto);
  engine.LayoutLine(mixed_he, 24.0, BaseDirection::kAuto, &layout);
  // Select the Hebrew word plus one trailing Latin letter.
  size_t heb_begin = 4;
  size_t heb_end = 4 + 8 + 1;  // 4 Hebrew letters (2 bytes each) + space.
  DumpSelection(engine, layout, heb_begin, heb_end);
  DumpSelection(engine, layout, 0, 3);
  DumpSelection(engine, layout, 0, mixed_he.size());

  // Arabic with combining marks, Latin, and a bidi isolate.
  const std::string mixed_ar =
      "\u0645\u0631\u062D\u0628\u0627 file\u2066"
      "\u05E0\u05D9\u05E1\u05D9\u05D5\u05DF\u2069 (v2)!";
  Dump(engine, mixed_ar, BaseDirection::kAuto);

  // Combining mark cluster: "e" + U+0301.
  Dump(engine, "e\u0301clair", BaseDirection::kLtr);
  return 0;
}
