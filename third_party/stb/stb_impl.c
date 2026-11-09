/* Single translation unit that compiles the stb header-only implementations. */

/* Decodes the user-chosen picture and the room model's textures. (Album art
   from media apps is untrusted and goes through Windows' own decoder instead,
   see nowplaying.cpp.) Only build the formats we need and cap image size. */
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
