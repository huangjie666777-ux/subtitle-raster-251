// Renders a mixed Arabic/Hebrew/Latin subtitle line to a transparent
// PNG ready to overlay on video frames.
#include <cstdio>

#include "subtitle_raster251/subtitle_raster251.h"

using namespace subtitle_raster251;

int main() {
  Engine engine("fonts/DejaVuSans.ttf");
  if (engine.GetError() != Error::kOk) {
    std::fprintf(stderr, "font error: %s\n", ErrorMessage(engine.GetError()));
    return 1;
  }

  const std::string text =
      "\u0645\u0631\u062D\u0628\u0627 \u05E9\u05DC\u05D5\u05DD Hello (v2)!";
  const Color fill{255, 255, 255, 255};   // White text.
  const Color stroke{0, 0, 0, 255};       // Black outline.

  RasterImage image;
  Error err = engine.RenderLine(text, 32.0, BaseDirection::kAuto, fill,
                                stroke, 2.0, 4, &image);
  if (err != Error::kOk) {
    std::fprintf(stderr, "render failed: %s\n", ErrorMessage(err));
    return 1;
  }
  std::printf("rendered %dx%d, line width %.2f px, origin (%.0f, %.0f)\n",
              image.width, image.height, image.line_width, image.origin_x,
              image.origin_y);

  const char* path = "subtitle.png";
  err = Engine::SavePng(image, path);
  if (err != Error::kOk) {
    std::fprintf(stderr, "save failed: %s\n", ErrorMessage(err));
    return 1;
  }
  std::printf("wrote %s\n", path);
  return 0;
}

