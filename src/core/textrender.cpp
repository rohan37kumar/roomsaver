#include "core/textrender.h"
#include "core/log.h"
#include "core/utf8.h"

#include <stb_truetype.h>

#include <cmath>

namespace {

// Decode UTF-8 to codepoints (invalid bytes become U+FFFD) so user-supplied text
// such as the idle-screen wordmark can contain non-ASCII characters.
std::vector<int> decode_utf8(const std::string& s) {
    std::vector<int> cps;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int cp = 0xFFFD, len = 1;
        if (c < 0x80)               { cp = c; }
        else if ((c >> 5) == 0x6)   { cp = c & 0x1F; len = 2; }
        else if ((c >> 4) == 0xE)   { cp = c & 0x0F; len = 3; }
        else if ((c >> 3) == 0x1E)  { cp = c & 0x07; len = 4; }
        else                        { cps.push_back(0xFFFD); ++i; continue; }
        if (i + len > s.size()) { cps.push_back(0xFFFD); break; }
        for (int k = 1; k < len; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc >> 6) != 0x2) { cp = 0xFFFD; len = k; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        cps.push_back(cp);
        i += len;
    }
    return cps;
}

} // namespace

bool TextRenderer::init(const std::string& ttfPath) {
    if (!read_file_utf8(ttfPath, fontData)) {
        LOGI("TextRenderer: cannot open font %s", ttfPath.c_str());
        return false;
    }
    if (fontData.empty()) { LOGI("TextRenderer: empty font file"); return false; }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fontData.data(), stbtt_GetFontOffsetForIndex(fontData.data(), 0))) {
        LOGI("TextRenderer: stbtt_InitFont failed");
        fontData.clear();
        return false;
    }
    ok = true;
    LOGI("TextRenderer: loaded font %s (%zu bytes)", ttfPath.c_str(), fontData.size());
    return true;
}

std::vector<unsigned char> TextRenderer::render(const std::string& text, int W, int H,
                                                unsigned char r, unsigned char g, unsigned char b,
                                                float heightFrac) const {
    std::vector<unsigned char> canvas((size_t)W * H * 4);
    // Dark LCD-ish background.
    for (size_t i = 0; i < canvas.size(); i += 4) {
        canvas[i + 0] = 6; canvas[i + 1] = 8; canvas[i + 2] = 10; canvas[i + 3] = 255;
    }
    if (!ok || text.empty()) return canvas;

    stbtt_fontinfo info;
    stbtt_InitFont(&info, fontData.data(), stbtt_GetFontOffsetForIndex(fontData.data(), 0));

    const std::vector<int> cps = decode_utf8(text);

    // Total advance width at a given scale (for centring and fit-to-width).
    auto measure = [&](float scale) {
        float w = 0.0f;
        for (size_t i = 0; i < cps.size(); ++i) {
            int adv, lsb; stbtt_GetCodepointHMetrics(&info, cps[i], &adv, &lsb);
            w += adv * scale;
            if (i + 1 < cps.size())
                w += stbtt_GetCodepointKernAdvance(&info, cps[i], cps[i + 1]) * scale;
        }
        return w;
    };

    float scale = stbtt_ScaleForPixelHeight(&info, H * heightFrac);
    float totalW = measure(scale);
    const float maxW = W * 0.9f;                 // long text shrinks to fit
    if (totalW > maxW && totalW > 0.0f) {
        scale *= maxW / totalW;
        totalW = maxW;
    }

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const int baseline = (int)std::lround((H + (ascent + descent) * scale) * 0.5f);

    float xpos = (W - totalW) * 0.5f;
    for (size_t i = 0; i < cps.size(); ++i) {
        int adv, lsb; stbtt_GetCodepointHMetrics(&info, cps[i], &adv, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&info, cps[i], scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> glyph((size_t)gw * gh);
            stbtt_MakeCodepointBitmap(&info, glyph.data(), gw, gh, gw, scale, scale, cps[i]);

            // x0 (from GetCodepointBitmapBox) already includes the left side bearing.
            int destX = (int)std::lround(xpos) + x0;
            int destY = baseline + y0;
            for (int yy = 0; yy < gh; ++yy) {
                int cy = destY + yy;
                if (cy < 0 || cy >= H) continue;
                for (int xx = 0; xx < gw; ++xx) {
                    int cx = destX + xx;
                    if (cx < 0 || cx >= W) continue;
                    float a = glyph[(size_t)yy * gw + xx] / 255.0f;
                    if (a <= 0.0f) continue;
                    size_t di = ((size_t)cy * W + cx) * 4;
                    canvas[di + 0] = (unsigned char)(canvas[di + 0] * (1 - a) + r * a);
                    canvas[di + 1] = (unsigned char)(canvas[di + 1] * (1 - a) + g * a);
                    canvas[di + 2] = (unsigned char)(canvas[di + 2] * (1 - a) + b * a);
                }
            }
        }
        xpos += adv * scale;
        if (i + 1 < cps.size())
            xpos += stbtt_GetCodepointKernAdvance(&info, cps[i], cps[i + 1]) * scale;
    }
    return canvas;
}
