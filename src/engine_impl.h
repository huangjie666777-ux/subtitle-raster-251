#ifndef SUBTITLE_RASTER251_ENGINE_IMPL_H
#define SUBTITLE_RASTER251_ENGINE_IMPL_H

#include <hb.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "subtitle_raster251/subtitle_raster251.h"

namespace subtitle_raster251 {

struct Engine::Impl {
  hb_blob_t* blob = nullptr;
  hb_face_t* face = nullptr;
  hb_font_t* font = nullptr;
  FT_Library ft_library = nullptr;
  FT_Face ft_face = nullptr;
  Error error = Error::kOk;
};

}  // namespace subtitle_raster251

#endif  // SUBTITLE_RASTER251_ENGINE_IMPL_H
