#ifndef SUBTITLE_RASTER251_SUBTITLE_RASTER251_H
#define SUBTITLE_RASTER251_SUBTITLE_RASTER251_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace subtitle_raster251 {

enum class BaseDirection {
  kAuto,  // First strong directional character wins; LTR if none.
  kLtr,
  kRtl,
};

enum class Error {
  kOk = 0,
  kInvalidUtf8,    // Malformed UTF-8 byte sequence.
  kNewline,        // Input contains a line break character.
  kTooLong,        // More than 4096 code points.
  kMissingGlyph,   // Font has no glyph for a printable character.
  kBadFont,        // Font file could not be opened or parsed.
  kBadFontSize,    // Font size is not a positive finite number.
  kBadSelection,   // Selection range is inverted, out of bounds, or
                   // does not fall on code point boundaries.
};

const char* ErrorMessage(Error error);

// One shaped glyph in visual (left-to-right display) order.
struct Glyph {
  uint32_t glyph_id = 0;
  // Half-open UTF-8 byte range of the source cluster. A cluster covers
  // every code point that merged into it (ligatures, combining marks).
  size_t cluster_begin = 0;
  size_t cluster_end = 0;
  // Pen position of this glyph on the line baseline. Origin is the left
  // end of the line baseline; x grows right, y grows up; unit is pixel.
  double x = 0.0;
  double y = 0.0;
  double advance_x = 0.0;
  double advance_y = 0.0;
  double offset_x = 0.0;
  double offset_y = 0.0;
};

struct Layout {
  std::vector<Glyph> glyphs;  // Visual order.
  double width = 0.0;         // Total advance of the line in pixels.
  double ascender = 0.0;
  double descender = 0.0;
  BaseDirection resolved_direction = BaseDirection::kLtr;
  size_t text_size = 0;  // UTF-8 byte length of the source text.
  // Byte offset of every code point start, plus text_size as sentinel.
  std::vector<size_t> codepoint_offsets;
};

// Half-open horizontal pixel interval of a logical selection.
struct SelectionRange {
  double x0 = 0.0;
  double x1 = 0.0;
};

class Engine {
 public:
  // Loads the font file. Check GetError() after construction.
  explicit Engine(const std::string& font_path);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  Error GetError() const;

  // Shapes a single line of UTF-8 text. font_size must be a positive
  // finite pixel size. Empty text succeeds with an empty layout.
  Error LayoutLine(const std::string& utf8_text, double font_size,
                   BaseDirection base_direction, Layout* out);

  // Maps a logical selection (half-open UTF-8 byte range over the text
  // passed to LayoutLine) to visual horizontal intervals. Any cluster
  // partially covered is selected as a whole; adjacent intervals are
  // merged. Mixed-direction selections may yield several ranges.
  Error SelectionRanges(const Layout& layout, size_t byte_begin,
                        size_t byte_end,
                        std::vector<SelectionRange>* out) const;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace subtitle_raster251

#endif  // SUBTITLE_RASTER251_SUBTITLE_RASTER251_H
