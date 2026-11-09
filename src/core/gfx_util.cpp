#include "core/gfx_util.h"
#include "core/log.h"

#include <glad/gl.h>
#include <stb_image_write.h>

#include <cstdio>
#include <vector>

// stb writes via this callback so we can own the FILE* (and use _wfopen for
// Unicode paths).
static void write_cb(void* ctx, void* data, int size) {
    FILE* f = (FILE*)ctx;
    fwrite(data, 1, (size_t)size, f);
}

bool save_screenshot(const wchar_t* path, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    std::vector<unsigned char> pixels((size_t)width * height * 4);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically: GL origin is bottom-left, image origin is top-left.
    const int stride = width * 4;
    std::vector<unsigned char> flipped((size_t)stride * height);
    for (int y = 0; y < height; ++y)
        memcpy(&flipped[(size_t)(height - 1 - y) * stride],
               &pixels[(size_t)y * stride], stride);

    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) {
        LOGI("save_screenshot: cannot open output file");
        return false;
    }
    int ok = stbi_write_png_to_func(write_cb, f, width, height, 4,
                                    flipped.data(), stride);
    fclose(f);
    if (!ok) { LOGI("save_screenshot: stbi_write_png failed"); return false; }
    LOGI("save_screenshot: wrote %dx%d screenshot", width, height);
    return true;
}
