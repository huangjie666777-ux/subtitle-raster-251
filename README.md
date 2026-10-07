# subtitle_raster251

C++17 library for laying out and rasterizing a single line of mixed
Arabic / Hebrew / Latin text for subtitle rendering, and for mapping
logical text selections to visual on-screen intervals.

Built on FriBidi 1.0.8 (bidi analysis) and HarfBuzz 2.7.4 (shaping),
plus FreeType 2.11.1 (rasterization) and libpng 1.6.37 (PNG output),
all vendored under `third_party/`. The font is
`fonts/DejaVuSans.ttf`.

## Features

- UTF-8 single-line input; base direction `kAuto` (first strong
  character, LTR when none), `kLtr`, or `kRtl`.
- Digits, punctuation, combining marks, and bidi isolates
  (LRI/RLI/FSI/PDI and friends) are supported; directional controls
  produce no visible glyphs.
- FriBidi computes embedding levels; the text is split into
  level + script segments; HarfBuzz shapes each segment in its own
  direction; segments are then reordered visually. Strings are never
  reversed before shaping, so joining, ligatures, marks, and bracket
  mirroring (via the OpenType `rtlm` feature) come out right.
- Input validation: empty text succeeds; invalid UTF-8, line breaks,
  missing glyphs, and inputs over 4096 code points are rejected.
- Output per glyph, in visual order: glyph id, half-open UTF-8 byte
  cluster range (clusters cover merged combining marks), baseline
  position, advance, and offset, plus total line width. Origin is the
  left end of the line baseline, x grows right, y grows up, unit is
  pixel. Spaces keep their real advance.
- Logical selections (half-open byte ranges on code point boundaries)
  map to visual horizontal intervals: partially covered clusters are
  selected whole, intervals are positioned by cluster advance spans,
  adjacent intervals merge, and mixed-direction selections can yield
  several disjoint ranges. Out-of-range, inverted, or
  non-code-point-boundary selections are rejected; empty selections
  return empty.
- Single-line rasterization to a transparent, non-premultiplied RGBA8
  bitmap ready for video overlay: grayscale antialiased glyphs via
  FreeType at the shaped positions (subpixel deltas, negative
  bearings, and y-up to y-down conversion handled), a rounded
  antialiased stroke, and integer padding. Fill and stroke are merged
  line-wide by maximum coverage and composited stroke-then-fill with
  source-over, so overlapping glyphs never double-darken and strokes
  never cover neighboring fill. Font size is limited to [8, 128] px,
  stroke radius to [0, 8] px, padding to [0, 32] px; canvases over
  4,000,000 pixels are rejected. The canvas is the ink bounding box
  expanded by stroke and padding, so marks and strokes are never
  clipped; inkless text returns a 0x0 image with the line width kept.
  Zero-alpha pixels carry zero RGB. `SavePng` writes the identical
  pixels as RGBA8 PNG.

## Public API

See `include/subtitle_raster251/subtitle_raster251.h`.

```cpp
subtitle_raster251::Engine engine("fonts/DejaVuSans.ttf");
subtitle_raster251::Layout layout;
engine.LayoutLine("mixed text", 24.0,
                  subtitle_raster251::BaseDirection::kAuto, &layout);
std::vector<subtitle_raster251::SelectionRange> ranges;
engine.SelectionRanges(layout, 4, 12, &ranges);

subtitle_raster251::RasterImage image;
engine.RenderLine("mixed text", 32.0,
                  subtitle_raster251::BaseDirection::kAuto,
                  {255, 255, 255, 255},  // fill: white
                  {0, 0, 0, 255},        // stroke: black
                  2.0,                   // stroke radius (px)
                  4,                     // padding (px)
                  &image);
// image.pixels: RGBA8, top-left row-major, non-premultiplied.
// image.origin_x / origin_y: top-left corner relative to the line
// baseline origin (x right, y down); image.line_width: line advance.
subtitle_raster251::Engine::SavePng(image, "subtitle.png");
```

## Rasterization API

`Engine::RenderLine(text, font_size, direction, fill, stroke,
stroke_radius, padding, &image)` shapes with the same code path as
`LayoutLine` (glyph ids and positions are identical; joining,
ligatures, and mark placement are preserved) and renders the line.
New error codes: `kBadStrokeRadius`, `kBadPadding`,
`kImageTooLarge`, `kIoError`; out-of-range font sizes report
`kBadFontSize`.

## Source layout

- `src/utf8.cpp` — strict UTF-8 decoding and bidi-control table.
- `src/segments.cpp` — FriBidi level analysis, script-run
  segmentation, visual reordering.
- `src/engine.cpp` — HarfBuzz shaping and glyph positioning.
- `src/selection.cpp` — logical-to-visual selection mapping.
- `src/raster.cpp` — FreeType glyph rasterization, line mask merge,
  stroke dilation, source-over compositing, PNG output.

## Build, test, demo

```sh
make            # builds build/libsubtitle_raster251.a, bin/demo,
                # bin/selftest, bin/subtitle_png
make test       # runs the selftest suite
./bin/demo      # mixed-line layout + selection example
./bin/subtitle_png  # renders a mixed transparent subtitle PNG
```

Executables use `-Wl,-rpath,'$ORIGIN/../third_party/lib'`, so no
`LD_LIBRARY_PATH` setup is needed.
