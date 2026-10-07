# subtitle_raster251

C++17 library for laying out a single line of mixed Arabic / Hebrew /
Latin text for subtitle rendering, and for mapping logical text
selections to visual on-screen intervals.

Built on FriBidi 1.0.8 (bidi analysis) and HarfBuzz 2.7.4 (shaping),
both vendored under `third_party/`. The font is
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

## Public API

See `include/subtitle_raster251/subtitle_raster251.h`.

```cpp
subtitle_raster251::Engine engine("fonts/DejaVuSans.ttf");
subtitle_raster251::Layout layout;
engine.LayoutLine("mixed text", 24.0,
                  subtitle_raster251::BaseDirection::kAuto, &layout);
std::vector<subtitle_raster251::SelectionRange> ranges;
engine.SelectionRanges(layout, 4, 12, &ranges);
```

## Source layout

- `src/utf8.cpp` — strict UTF-8 decoding and bidi-control table.
- `src/segments.cpp` — FriBidi level analysis, script-run
  segmentation, visual reordering.
- `src/engine.cpp` — HarfBuzz shaping and glyph positioning.
- `src/selection.cpp` — logical-to-visual selection mapping.

## Build, test, demo

```sh
make            # builds build/libsubtitle_raster251.a, bin/demo, bin/selftest
make test       # runs the selftest suite
./bin/demo      # mixed-line layout + selection example
```

Executables use `-Wl,-rpath,'$ORIGIN/../third_party/lib'`, so no
`LD_LIBRARY_PATH` setup is needed.
