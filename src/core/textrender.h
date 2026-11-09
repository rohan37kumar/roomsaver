#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Minimal stb_truetype wrapper used to rasterize the clock display and the idle
// TV wordmark. Loads a TTF once, then renders short strings (e.g. "13:45")
// centred on an RGBA canvas with a dark screen background and glowing glyphs.
struct TextRenderer {
    std::vector<unsigned char> fontData;
    bool ok = false;

    bool init(const std::string& ttfPath);   // UTF-8 path

    // Render UTF-8 `text` centred on a WxH RGBA canvas. Background is near-black;
    // glyph coverage is blended toward (r,g,b). `heightFrac` sets the glyph height
    // as a fraction of H (0.62 = clock-sized; smaller = a more modest wordmark).
    // Text wider than 90% of W is scaled down to fit. Returns W*H*4 bytes (row 0 = top).
    std::vector<unsigned char> render(const std::string& text, int W, int H,
                                      unsigned char r, unsigned char g, unsigned char b,
                                      float heightFrac = 0.62f) const;
};
