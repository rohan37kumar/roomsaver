#include "app.h"
#include "core/log.h"
#include "core/settings.h"
#include "core/utf8.h"

#include <shlobj.h>
#include <knownfolders.h>

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>
#include <algorithm>

#ifndef PROJECT_ROOT
#define PROJECT_ROOT "."
#endif

// Where shaders/ and assets/ live. A shipped .scr carries them next to the exe;
// during development they load straight from the source tree (PROJECT_ROOT).
static const std::string& asset_base() {
    static std::string base = [] {
        std::string dir = exe_dir_utf8();
        if (FILE* f = std::fopen((dir + "/shaders/forward.vert").c_str(), "rb")) {
            std::fclose(f);
            return dir;                       // packaged: assets sit beside the exe
        }
        return std::string(PROJECT_ROOT);     // dev: load from the source tree
    }();
    return base;
}

static std::string asset(const char* rel) {
    return asset_base() + "/" + rel;
}

// The system Fonts folder (not always C:\Windows\Fonts), UTF-8.
static std::string fonts_dir() {
    std::string dir = "C:/Windows/Fonts";
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &p)) && p)
        dir = wide_to_utf8(p);
    CoTaskMemFree(p);
    return dir;
}

// Upload an RGBA image as a GL texture (mipmapped, clamped — dynamic surfaces
// should not wrap). Returns the texture handle.
static GLuint upload_rgba(const unsigned char* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

// Load an image file to RGBA (row 0 = top, matching our UV convention). Empty on
// failure. stb's default (no vertical flip) puts image-top at row 0.
static std::vector<unsigned char> load_image_file(const std::string& path, int& w, int& h) {
    w = h = 0;
    int comp = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px) return {};
    std::vector<unsigned char> out(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return out;
}

// A warm procedural "photo" used when no picture file is present: a dusk sky
// gradient, a soft sun, and a dark hill silhouette. 4:3-ish, row 0 = top.
static std::vector<unsigned char> make_picture_placeholder(int W, int H) {
    std::vector<unsigned char> px((size_t)W * H * 4, 255);
    auto set = [&](int x, int y, float r, float g, float b) {
        size_t i = ((size_t)y * W + x) * 4;
        px[i+0] = (unsigned char)(glm::clamp(r,0.0f,1.0f)*255);
        px[i+1] = (unsigned char)(glm::clamp(g,0.0f,1.0f)*255);
        px[i+2] = (unsigned char)(glm::clamp(b,0.0f,1.0f)*255);
    };
    const float sunX = W*0.68f, sunY = H*0.34f, sunR = H*0.12f;
    const float hill = H*0.72f;
    for (int y = 0; y < H; ++y) {
        float t = (float)y / (float)H;                    // 0 top -> 1 bottom
        // sky: deep indigo up top easing to warm amber near horizon
        glm::vec3 sky = glm::mix(glm::vec3(0.20f,0.16f,0.34f),
                                 glm::vec3(0.98f,0.55f,0.28f), powf(t,1.6f));
        for (int x = 0; x < W; ++x) {
            glm::vec3 c = sky;
            float d = std::sqrt((x-sunX)*(x-sunX) + (y-sunY)*(y-sunY));
            if (d < sunR*3.0f) {
                float glow = glm::clamp(1.0f - d/(sunR*3.0f), 0.0f, 1.0f);
                c = glm::mix(c, glm::vec3(1.0f,0.92f,0.65f), glow*glow);
            }
            if (y > (int)hill) c = glm::vec3(0.06f,0.05f,0.09f);   // hill silhouette
            set(x, y, c.r, c.g, c.b);
        }
    }
    return px;
}

// Bilinear stretch-resize of an RGBA image (sw x sh) -> (dw x dh).
static std::vector<unsigned char> resize_bilinear(const unsigned char* s, int sw, int sh,
                                                  int dw, int dh) {
    std::vector<unsigned char> o((size_t)dw * dh * 4, 255);
    if (!s || sw <= 0 || sh <= 0) return o;
    for (int y = 0; y < dh; ++y) {
        float sy = (y + 0.5f) * sh / dh - 0.5f;
        int y0 = (int)std::floor(sy); float fy = sy - y0;
        int y0c = glm::clamp(y0, 0, sh-1), y1c = glm::clamp(y0+1, 0, sh-1);
        for (int x = 0; x < dw; ++x) {
            float sx = (x + 0.5f) * sw / dw - 0.5f;
            int x0 = (int)std::floor(sx); float fx = sx - x0;
            int x0c = glm::clamp(x0, 0, sw-1), x1c = glm::clamp(x0+1, 0, sw-1);
            size_t di = ((size_t)y * dw + x) * 4;
            for (int c = 0; c < 3; ++c) {
                float a = s[((size_t)y0c*sw + x0c)*4 + c];
                float b = s[((size_t)y0c*sw + x1c)*4 + c];
                float cc= s[((size_t)y1c*sw + x0c)*4 + c];
                float d = s[((size_t)y1c*sw + x1c)*4 + c];
                float top = a + (b-a)*fx, bot = cc + (d-cc)*fx;
                o[di + c] = (unsigned char)(top + (bot-top)*fy);
            }
        }
    }
    return o;
}

// Box-average downsample (sw x sh) -> (dw x dh). Used to build the blurred fill.
static std::vector<unsigned char> downsample_box(const unsigned char* s, int sw, int sh,
                                                 int dw, int dh) {
    std::vector<unsigned char> o((size_t)dw * dh * 4, 255);
    if (!s || sw <= 0 || sh <= 0) return o;
    for (int y = 0; y < dh; ++y) {
        int sy0 = y * sh / dh, sy1 = std::max(sy0 + 1, (y + 1) * sh / dh);
        for (int x = 0; x < dw; ++x) {
            int sx0 = x * sw / dw, sx1 = std::max(sx0 + 1, (x + 1) * sw / dw);
            float acc[3] = {0, 0, 0}; int cnt = 0;
            for (int yy = sy0; yy < sy1; ++yy)
                for (int xx = sx0; xx < sx1; ++xx) {
                    const unsigned char* p = &s[((size_t)yy*sw + xx)*4];
                    acc[0]+=p[0]; acc[1]+=p[1]; acc[2]+=p[2]; ++cnt;
                }
            size_t di = ((size_t)y*dw + x)*4;
            o[di]   = (unsigned char)(acc[0]/cnt);
            o[di+1] = (unsigned char)(acc[1]/cnt);
            o[di+2] = (unsigned char)(acc[2]/cnt);
        }
    }
    return o;
}

// Cover-fit: scale `src` to completely fill (cw x ch) preserving aspect ratio,
// center-cropping whatever overflows (no distortion, no bars). Row 0 = top.
static std::vector<unsigned char> cover_fit(const unsigned char* src, int w, int h,
                                            int cw, int ch) {
    if (!src || w <= 0 || h <= 0) return std::vector<unsigned char>((size_t)cw * ch * 4, 0);
    float coverScale = std::max((float)cw / w, (float)ch / h);
    int cropW = std::min(w, std::max(1, (int)std::lround(cw / coverScale)));
    int cropH = std::min(h, std::max(1, (int)std::lround(ch / coverScale)));
    int cx = (w - cropW) / 2, cy = (h - cropH) / 2;
    std::vector<unsigned char> crop((size_t)cropW * cropH * 4);
    for (int y = 0; y < cropH; ++y)
        std::memcpy(&crop[(size_t)y*cropW*4], &src[((size_t)(cy+y)*w + cx)*4], (size_t)cropW*4);
    return resize_bilinear(crop.data(), cropW, cropH, cw, ch);
}

// Build the TV canvas (cw x ch, 16:9): a blurred, darkened, cover-cropped version
// of the (usually square) art fills the whole screen, with the sharp art centered
// on top. No black bars — the sides get an ambient blur of the album colours.
static std::vector<unsigned char> build_tv_canvas(const unsigned char* src, int w, int h,
                                                  int cw, int ch) {
    std::vector<unsigned char> canvas((size_t)cw * ch * 4, 255);
    if (!src || w <= 0 || h <= 0) return canvas;

    // --- background: cover-fill the source to the canvas, then blur + darken ---
    std::vector<unsigned char> coverFull = cover_fit(src, w, h, cw, ch);
    std::vector<unsigned char> lowres = downsample_box(coverFull.data(), cw, ch, 42, 24);
    canvas = resize_bilinear(lowres.data(), 42, 24, cw, ch);        // soft blur
    for (size_t i = 0; i < canvas.size(); i += 4) {               // darken the fill
        canvas[i+0] = (unsigned char)(canvas[i+0] * 0.45f);
        canvas[i+1] = (unsigned char)(canvas[i+1] * 0.45f);
        canvas[i+2] = (unsigned char)(canvas[i+2] * 0.45f);
    }

    // --- foreground: sharp, aspect-preserved art centered over the blur ---
    float fitScale = std::min((float)cw / w, (float)ch / h);
    int dw = std::max(1, (int)std::lround(w * fitScale));
    int dh = std::max(1, (int)std::lround(h * fitScale));
    int ox = (cw - dw) / 2, oy = (ch - dh) / 2;
    std::vector<unsigned char> fg = resize_bilinear(src, w, h, dw, dh);
    for (int y = 0; y < dh; ++y)
        std::memcpy(&canvas[((size_t)(oy+y)*cw + ox)*4], &fg[(size_t)y*dw*4], (size_t)dw*4);

    return canvas;
}

bool App::init(int w, int h) {
    width = w; height = h;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    if (!shader.loadFromFiles(asset("shaders/forward.vert"), asset("shaders/forward.frag"))) {
        LOGI("App::init: forward shader failed to load");
        return false;
    }
    if (!backdrop.loadFromFiles(asset("shaders/fullscreen.vert"), asset("shaders/backdrop.frag"))) {
        LOGI("App::init: backdrop shader failed to load");
        return false;
    }
    if (!postfx.init(w, h, asset("shaders"))) {
        LOGI("App::init: post-processing init failed");
        return false;
    }

    // Prefer the real room model; fall back to the generated placeholder.
    modelLoaded = room.load(asset("assets/room.glb"));
    if (!modelLoaded)
        modelLoaded = room.load(asset("assets/room_placeholder.glb"));
    if (modelLoaded) {
        camera.fitBounds(room.center(), room.radius());
        LOGI("App::init: camera fit center(%.2f,%.2f,%.2f) radius %.2f",
             room.center().x, room.center().y, room.center().z, room.radius());
        setupDynamicSurfaces();

        // Clock font: try a couple of always-present Windows fonts (bold monospace
        // reads best as a clock); fall back gracefully if none load.
        const std::string fonts = fonts_dir();
        if (!clockFont.init(fonts + "/consolab.ttf"))
            clockFont.init(fonts + "/segoeui.ttf");
        updateClock();

        // Configured picture path wins; else fall back to assets/picture.*
        picturePath = picturePathOverride;
        loadPicture();

        makeTVPlaceholder();
        tvShowingPlaceholder = true;   // idle text until a track's art arrives

        // Now-playing album art (background SMTC thread).
        nowPlaying.start();

        // Reactive color + beat. Start from the warm defaults.
        glowColor = defGlow; glowTarget = defGlow;
        audio.sensitivity = beatSensitivity;              // from settings
        if (const char* e = std::getenv("ROOMSAVER_BEAT_SENS"))
            audio.sensitivity = (float)std::atof(e);      // env override for dev
        audio.start();   // WASAPI loopback beat-detection thread
    } else {
        LOGI("App::init: no model loaded (will render empty backdrop)");
    }

    LOGI("App::init %dx%d", w, h);
    return true;
}

// Each dynamic role is found by its docs/ASSET_CONTRACT.md name, falling back to
// the name used inside the bundled assets/room.glb. `bundled` reports which one
// matched: a few fix-ups below are tuned for that specific model only.
struct NodeMatch { ModelNode* node = nullptr; bool bundled = false; };

static NodeMatch find_role(Model& room, const char* contractName, const char* bundledName) {
    if (ModelNode* n = room.findNode(contractName)) return { n, false };
    if (ModelNode* n = room.findNode(bundledName))  return { n, true };
    return {};
}

// Resolve the named dynamic nodes and give each texture quad its own Material so
// runtime textures can be bound by setting that material's baseTex.
void App::setupDynamicSurfaces() {
    const NodeMatch mTV      = find_role(room, "TV_Screen",     "TV_SCREEN");
    const NodeMatch mPicture = find_role(room, "Picture_Image", "PICTURE_FRAME");
    const NodeMatch mClock   = find_role(room, "Clock_Display", "CLOCK_SCREEN");
    const NodeMatch mSound   = find_role(room, "Soundbar_LED",  "SOUNDBAR_LED");
    const NodeMatch mBiasTV  = find_role(room, "TV_BiasLight",  "Television.001");
    const NodeMatch mTVBody  = find_role(room, "TV_Body",       "Television");
    const NodeMatch mLamp    = find_role(room, "Lamp_Wall",     "Plane.001");

    tvScreen     = mTV.node;
    picture      = mPicture.node;
    clock        = mClock.node;
    biasSoundbar = mSound.node;
    biasTV       = mBiasTV.node;

    // Allocate a dedicated Material for a texture quad and point its primitives at
    // it. `tint` is the "screen off" color until a runtime texture is bound.
    auto bindDynamic = [&](ModelNode* n, const char* label, glm::vec4 tint, bool unlit) -> int {
        if (!n) { LOGI("setupDynamicSurfaces: MISSING node for %s", label); return -1; }
        Material dyn;
        dyn.baseColor = tint;
        dyn.unlit     = unlit;
        room.materials.push_back(dyn);
        int idx = (int)room.materials.size() - 1;
        for (Primitive& p : n->primitives) p.material = idx;
        LOGI("setupDynamicSurfaces: %-13s -> node '%s' (%zu prims), dyn material slot %d (unlit=%d)",
             label, n->name.c_str(), n->primitives.size(), idx, (int)unlit);
        return idx;
    };

    // TV + clock are self-lit screens; the picture is a lit print on the wall.
    tvMatIdx      = bindDynamic(tvScreen, "TV_SCREEN",     glm::vec4(0.02f, 0.02f, 0.02f, 1.0f), true);
    pictureMatIdx = bindDynamic(picture,  "PICTURE_FRAME", glm::vec4(0.50f, 0.50f, 0.50f, 1.0f), false);
    clockMatIdx   = bindDynamic(clock,    "CLOCK_SCREEN",  glm::vec4(0.01f, 0.01f, 0.01f, 1.0f), true);

    // A Blender plane rotated onto the side wall (both the bundled room and the
    // placeholder) ends up with UVs rotated 90° (U runs along world-Y), so an image
    // would show sideways; correct it with a per-node UV rotation about the quad
    // centre. ROOMSAVER_PICROT=<deg> overrides it for models authored differently.
    float picRotDeg = 90.0f;
    if (const char* e = std::getenv("ROOMSAVER_PICROT")) picRotDeg = (float)std::atof(e);
    if (picture) {
        float a = glm::radians(picRotDeg);
        picture->uvTransform = glm::mat2(std::cos(a), std::sin(a), -std::sin(a), std::cos(a));
        LOGI("setupDynamicSurfaces: PICTURE_FRAME uv rotation = %.1f deg", picRotDeg);
    }

    // Resolve the bias-light materials (their emissive is driven each frame) and
    // remember the authored emissive so we can ease back to it when idle.
    if (biasSoundbar && !biasSoundbar->primitives.empty()) {
        biasSoundbarMat = biasSoundbar->primitives[0].material;
        if (biasSoundbarMat >= 0) defSoundbarEmissive = room.materials[biasSoundbarMat].emissive;
    }
    if (biasTV && !biasTV->primitives.empty()) {
        biasTVMat = biasTV->primitives[0].material;
        if (biasTVMat >= 0) defTVEmissive = room.materials[biasTVMat].emissive;
    }

    // Bundled model only: its backlight plane (Television.001) ships smaller than
    // the TV and fully hidden behind it, so it's invisible. Recenter it on the TV
    // and enlarge it just enough to peek a thin bias-light halo around the panel
    // edges — NOT a wall-sized glow. A contract TV_BiasLight is used as authored.
    if (biasTV && mBiasTV.bundled) {
        glm::vec3 cb = (biasTV->boundsMin + biasTV->boundsMax) * 0.5f;   // backlight center
        glm::vec3 target = cb;
        if (ModelNode* body = mTVBody.node) {
            glm::vec3 bc = (body->boundsMin + body->boundsMax) * 0.5f;
            target.x = bc.x; target.y = bc.y;
        }
        // Sit just behind the TV's front face (not deep behind it): the TV occludes
        // the plane's center, leaving an EVEN thin frame of glow around all 4 edges.
        // Deep behind (its authored z) the body occludes the near edges -> lopsided.
        if (tvScreen) target.z = tvScreen->boundsMax.z - 0.05f;
        glm::vec3 shift = target - cb;
        glm::mat4 s = glm::translate(glm::mat4(1.0f), target)
                    * glm::scale(glm::mat4(1.0f), glm::vec3(tvBacklightScale, tvBacklightScale, 1.0f))
                    * glm::translate(glm::mat4(1.0f), -cb);
        biasTV->world     = s * biasTV->world;
        biasTV->normalMat = glm::mat3(glm::transpose(glm::inverse(biasTV->world)));
        biasTV->boundsMin = cb + (biasTV->boundsMin - cb) * tvBacklightScale + shift;
        biasTV->boundsMax = cb + (biasTV->boundsMax - cb) * tvBacklightScale + shift;
        LOGI("setupDynamicSurfaces: TV backlight recentered on TV, scale x%.2f", tvBacklightScale);
    }
    soundbarCol = defSoundbarEmissive;
    tvBiasCol   = defTVEmissive;

    // Wall sconce: its center drives the warm point light. The bundled model's
    // sconce ('Plane.001') ships with a very strong emissive that blows out to a
    // white disc, so dial that one down to a soft warm glow.
    if (ModelNode* lamp = mLamp.node) {
        haveLamp = true;
        lampPos  = (lamp->boundsMin + lamp->boundsMax) * 0.5f;
        int lm = lamp->primitives.empty() ? -1 : lamp->primitives[0].material;
        if (mLamp.bundled && lm >= 0) {
            room.materials[lm].emissiveStrength = 0.75f;
            room.materials[lm].emissive *= glm::vec3(1.0f, 0.92f, 0.78f); // warm it slightly
        }
        LOGI("setupDynamicSurfaces: wall lamp '%s', point light at (%.2f,%.2f,%.2f)",
             lamp->name.c_str(), lampPos.x, lampPos.y, lampPos.z);
    }

    LOGI("setupDynamicSurfaces: bias lights -> soundbar=%s (mat %d) tvBacklight=%s (mat %d)",
         biasSoundbar ? "ok" : "MISSING", biasSoundbarMat,
         biasTV ? "ok" : "MISSING", biasTVMat);

    // Diagnostic: list emissive nodes + their AABBs (find the Lamp; check TV backlight
    // vs TV_SCREEN to see whether the backlight peeks out around the panel).
    if (std::getenv("ROOMSAVER_DUMP")) {
        for (const ModelNode& n : room.nodes)
            for (const Primitive& p : n.primitives) {
                if (p.material < 0 || p.material >= (int)room.materials.size()) continue;
                const Material& m = room.materials[p.material];
                if (m.emissive.r + m.emissive.g + m.emissive.b > 0.001f)
                    LOGI("  EMISSIVE '%s' mat %d (%.2f,%.2f,%.2f)x%.2f  bounds[%.2f,%.2f,%.2f .. %.2f,%.2f,%.2f]",
                         n.name.c_str(), p.material, m.emissive.r, m.emissive.g, m.emissive.b,
                         m.emissiveStrength,
                         n.boundsMin.x, n.boundsMin.y, n.boundsMin.z,
                         n.boundsMax.x, n.boundsMax.y, n.boundsMax.z);
            }
        for (const char* nm : { "TV_SCREEN", "Television", "Television.001" })
            if (ModelNode* s = room.findNode(nm))
                LOGI("  NODE '%s' bounds[%.2f,%.2f,%.2f .. %.2f,%.2f,%.2f]", nm,
                     s->boundsMin.x, s->boundsMin.y, s->boundsMin.z,
                     s->boundsMax.x, s->boundsMax.y, s->boundsMax.z);
    }
}

// Derive a vivid accent color from the album art: a saturation/brightness-
// weighted average (so muddy backgrounds don't dominate), normalized so the
// brightest channel ~= 1. Falls back to a plain average, then the warm default.
glm::vec3 App::computeAccent(const std::vector<unsigned char>& rgba, int w, int h) const {
    if (rgba.empty() || w <= 0 || h <= 0) return defGlow;
    double ar = 0, ag = 0, ab = 0, wsum = 0;
    double gr = 0, gg = 0, gb = 0; long gcount = 0;
    int step = std::max(1, (int)std::sqrt((double)(w * h) / 4096.0));
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            size_t i = ((size_t)y * w + x) * 4;
            float r = rgba[i] / 255.0f, g = rgba[i+1] / 255.0f, b = rgba[i+2] / 255.0f;
            gr += r; gg += g; gb += b; ++gcount;
            float mx = std::max(r, std::max(g, b));
            float mn = std::min(r, std::min(g, b));
            float sat = mx - mn, val = mx;
            if (val < 0.12f) continue;                 // ignore near-black
            float wgt = sat * sat * val;               // favor vivid & bright
            ar += r * wgt; ag += g * wgt; ab += b * wgt; wsum += wgt;
        }
    }
    glm::vec3 c;
    if (wsum > 1e-4)      c = glm::vec3((float)(ar/wsum), (float)(ag/wsum), (float)(ab/wsum));
    else if (gcount > 0)  c = glm::vec3((float)(gr/gcount), (float)(gg/gcount), (float)(gb/gcount));
    else                  return defGlow;
    float mx = std::max(c.r, std::max(c.g, c.b));
    if (mx > 1e-4f) c /= mx;                            // normalize to a vivid tone
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

// Re-rasterize the clock only when the wall-clock minute changes.
void App::updateClock() {
    if (clockMatIdx < 0) return;
    time_t now = time(nullptr);
    struct tm lt;
    localtime_s(&lt, &now);
    if (lt.tm_min == lastClockMin && clockTex != 0) return;
    lastClockMin = lt.tm_min;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);

    const int W = 384, H = 176;
    auto rgba = clockFont.render(buf, W, H, 120, 235, 220);   // soft cyan digits
    if (clockTex) glDeleteTextures(1, &clockTex);
    clockTex = upload_rgba(rgba.data(), W, H);
    room.materials[clockMatIdx].baseTex   = clockTex;
    room.materials[clockMatIdx].baseColor = glm::vec4(1.0f);
}

// Load the framed picture from picturePath; if it's absent, synthesize a warm
// placeholder so the frame always shows something.
void App::loadPicture() {
    if (pictureMatIdx < 0) return;
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
    // Accept the common formats/names so the user can just drop a file in assets/.
    const char* candidates[] = { "assets/picture.png", "assets/picture.jpg",
                                 "assets/picture.jpeg", "assets/picture.bmp" };
    std::string used;
    if (!picturePath.empty()) { rgba = load_image_file(picturePath, w, h); used = picturePath; }
    for (const char* rel : candidates) {
        if (!rgba.empty()) break;
        used = asset(rel);
        rgba = load_image_file(used, w, h);
    }
    if (rgba.empty()) {
        w = 320; h = 240;
        rgba = make_picture_placeholder(w, h);
        LOGI("App::loadPicture: no assets/picture.* found, using placeholder");
    } else {
        LOGI("App::loadPicture: loaded '%s' (%dx%d)", used.c_str(), w, h);
    }

    // Cover-fit onto the frame's true aspect so the image keeps its proportions
    // (centered, cropped as needed) instead of being squished to the quad. The
    // frame hangs on a wall, so its height is world-Y and its width is whichever
    // horizontal extent (X or Z) is larger — works for either wall.
    float frameAspect = 3.0f / 4.0f;   // fallback: portrait
    if (picture) {
        glm::vec3 d = picture->boundsMax - picture->boundsMin;
        float wdt = std::max(d.x, d.z);
        if (d.y > 1e-4f && wdt > 1e-4f) frameAspect = wdt / d.y;   // width / height
    }
    int CW, CH;
    if (frameAspect >= 1.0f) { CH = 512; CW = std::max(1, (int)std::lround(512 * frameAspect)); }
    else                     { CW = 512; CH = std::max(1, (int)std::lround(512 / frameAspect)); }
    std::vector<unsigned char> fitted = cover_fit(rgba.data(), w, h, CW, CH);
    LOGI("App::loadPicture: frame aspect %.3f -> canvas %dx%d (cover-fit)", frameAspect, CW, CH);

    if (pictureTex) glDeleteTextures(1, &pictureTex);
    pictureTex = upload_rgba(fitted.data(), CW, CH);
    room.materials[pictureMatIdx].baseTex   = pictureTex;
    room.materials[pictureMatIdx].baseColor = glm::vec4(1.0f);
}

// Idle TV screen, shown when nothing is playing (no media session at all — a
// paused track still shows its art). A dark panel with the configurable idle
// text (default "kc") at a medium size (heightFrac 0.34); empty text = blank
// panel. Callers decide when to show it.
void App::makeTVPlaceholder() {
    if (tvMatIdx < 0) return;
    const int W = 512, H = 288;
    std::vector<unsigned char> rgba;
    if (clockFont.ok) rgba = clockFont.render(idleText, W, H, 165, 172, 180, 0.34f);
    else { rgba.assign((size_t)W*H*4, 20); for (size_t i=3;i<rgba.size();i+=4) rgba[i]=255; }
    if (tvTex) glDeleteTextures(1, &tvTex);
    tvTex = upload_rgba(rgba.data(), W, H);
    room.materials[tvMatIdx].baseTex   = tvTex;
    room.materials[tvMatIdx].baseColor = glm::vec4(1.0f);
}

// Compose the album art onto a 16:9 TV canvas (blurred fill + sharp art) & upload.
void App::setTVTexture(const unsigned char* rgba, int w, int h) {
    if (tvMatIdx < 0) return;
    const int CW = 512, CH = 288;   // 16:9
    auto canvas = build_tv_canvas(rgba, w, h, CW, CH);
    if (tvTex) glDeleteTextures(1, &tvTex);
    tvTex = upload_rgba(canvas.data(), CW, CH);
    room.materials[tvMatIdx].baseTex   = tvTex;
    room.materials[tvMatIdx].baseColor = glm::vec4(1.0f);
}

void App::resize(int w, int h) { width = w; height = h; postfx.resize(w, h); }

void App::update(double dt) {
    timeSec += dt;
    updateClock();

    // Dev override: force the idle screen even while a track is playing, so the
    // no-music state can be verified without closing the media app.
    static const bool forceIdle = std::getenv("ROOMSAVER_TV_IDLE") != nullptr;
    static const bool beatLog   = std::getenv("ROOMSAVER_BEAT_LOG") != nullptr;

    // Pull freshly-decoded album art from the SMTC worker: update the TV and
    // derive the accent color from it.
    std::vector<unsigned char> art;
    int aw = 0, ah = 0;
    if (!forceIdle && nowPlaying.takeArtwork(art, aw, ah) && aw > 0 && ah > 0) {
        setTVTexture(art.data(), aw, ah);
        tvHasArt = true;
        tvShowingPlaceholder = false;
        accentColor = computeAccent(art, aw, ah);
        haveAccent = true;
        LOGI("App::update: album art %dx%d '%s' -> accent (%.2f,%.2f,%.2f)",
             aw, ah, nowPlaying.title().c_str(), accentColor.r, accentColor.g, accentColor.b);
    }

    // When nothing is actually playing — no SMTC session at all (a *paused* track
    // still has a session, so its art stays) — show the idle-text screen instead
    // of stale artwork. Re-uploaded only on the transition, not every frame.
    if ((forceIdle || !nowPlaying.hasSession()) && !tvShowingPlaceholder) {
        makeTVPlaceholder();
        tvHasArt = false;
        tvShowingPlaceholder = true;
        LOGI("App::update: no media session -> TV idle screen");
    }

    // Pick targets — album accent when a track is playing, else the warm
    // defaults — and ease everything toward them (frame-rate independent).
    const bool useAlbum = nowPlaying.hasSession() && haveAccent;
    glowTarget = useAlbum ? accentColor * 0.75f : defGlow;
    glm::vec3 sbTarget = useAlbum ? accentColor : defSoundbarEmissive;
    glm::vec3 tvTarget = useAlbum ? accentColor : defTVEmissive;

    float k = 1.0f - std::exp(-(float)dt * 3.5f);
    glowColor   += (glowTarget - glowColor) * k;
    soundbarCol += (sbTarget   - soundbarCol) * k;
    tvBiasCol   += (tvTarget   - tvBiasCol) * k;

    // Modulate bias-light intensity with the beat envelope. A gentle
    // gamma lifts smaller beats so the pulsing reads clearly (more "alive").
    float beat = std::pow(audio.level(), 0.72f);
    float intensity = biasBaseIntensity + beat * biasFlicker;
    if (biasSoundbarMat >= 0) {
        room.materials[biasSoundbarMat].emissive = soundbarCol * intensity;
        room.materials[biasSoundbarMat].emissiveStrength = 1.0f;
    }
    if (biasTVMat >= 0) {
        // The backlight sits behind the panel, so push it harder to spill a colored
        // glow (and beat flicker) around the TV edges.
        room.materials[biasTVMat].emissive = tvBiasCol * intensity * tvBacklightGain;
        room.materials[biasTVMat].emissiveStrength = 1.0f;
    }

    if (beatLog && timeSec - lastBeatLog >= 0.5) {
        lastBeatLog = timeSec;
        LOGI("beat=%.2f (audio %s) intensity=%.2f accent(%.2f,%.2f,%.2f)",
             beat, audio.active() ? "on" : "off", intensity,
             accentColor.r, accentColor.g, accentColor.b);
    }
}

void App::render() {
    if (!modelLoaded) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.06f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return;
    }

    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;

    // ---- Scene pass: draw into the HDR MSAA target ----
    postfx.beginScene();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Backdrop gradient + radial glow (fills colour, leaves depth untouched so
    // the room draws over it).
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    // Global brightness setting scales the lit terms and the backdrop.
    const float b = brightness;

    backdrop.use();
    backdrop.set("uTopColor",    glm::vec3(0.045f, 0.040f, 0.060f) * b);
    backdrop.set("uBottomColor", glm::vec3(0.110f, 0.075f, 0.060f) * b);
    backdrop.set("uGlowColor",   glowColor * b);
    backdrop.set("uGlowCenter",  glm::vec2(0.50f, 0.52f));
    backdrop.set("uGlowRadius",  0.80f);
    backdrop.set("uAspect",      aspect);
    postfx.drawFullscreen();

    // Room.
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    const glm::mat4 viewProj = camera.viewProj(aspect);

    shader.use();
    shader.set("uViewProj", viewProj);
    shader.set("uLightDir",   glm::normalize(glm::vec3(0.35f, 0.9f, 0.45f)));
    shader.set("uLightColor", glm::vec3(0.72f, 0.68f, 0.62f) * b);   // warm key (dimmed: top surfaces were too hot)
    shader.set("uAmbient",    glm::vec3(0.34f, 0.30f, 0.30f) * b);    // lifted, warm fill
    shader.set("uPointPos",    lampPos);
    shader.set("uPointColor",  haveLamp ? lampColor * lampIntensity * b  // warm sconce pool
                                        : glm::vec3(0.0f));
    shader.set("uPointRadius", lampRadius);
    shader.set("uEmissiveBoost", 1.5f * b);
    shader.set("uTex", 0);

    for (const ModelNode& node : room.nodes) {
        shader.set("uModel", node.world);
        shader.set("uNormalMat", node.normalMat);
        shader.set("uUV", node.uvTransform);

        // Feather only the TV backlight so its glow edges are soft, not sharp.
        if (&node == biasTV) {
            glm::vec3 mn = node.boundsMin, mx = node.boundsMax;
            shader.set("uFeatherCenter", glm::vec2((mn.x+mx.x)*0.5f, (mn.y+mx.y)*0.5f));
            shader.set("uFeatherHalf",   glm::vec2((mx.x-mn.x)*0.5f, (mx.y-mn.y)*0.5f));
            shader.set("uFeatherAmt", 0.12f);
        } else {
            shader.set("uFeatherAmt", 0.0f);
        }

        for (const Primitive& prim : node.primitives) {
            Material mat;
            if (prim.material >= 0 && prim.material < (int)room.materials.size())
                mat = room.materials[prim.material];

            shader.set("uBaseColor", mat.baseColor);
            shader.set("uEmissive", mat.emissive);
            shader.set("uEmissiveStrength", mat.emissiveStrength);
            shader.set("uUnlit", mat.unlit ? 1 : 0);

            if (mat.baseTex) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mat.baseTex);
                shader.set("uHasTex", 1);
            } else {
                shader.set("uHasTex", 0);
            }

            glBindVertexArray(prim.vao);
            glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, nullptr);
        }
    }
    glBindVertexArray(0);

    // ---- Resolve + bloom + composite to the window back buffer ----
    postfx.endScene();

    // Force the back-buffer ALPHA to fully opaque. The HDR composite can leave
    // alpha < 1 in places; clearing only the alpha channel to 1.0 (RGB untouched via
    // the colour mask) makes the --selftest screenshot come out solid rather than
    // partly transparent.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void App::shutdown() {
    audio.stop();
    nowPlaying.stop();
    if (clockTex)   glDeleteTextures(1, &clockTex);
    if (pictureTex) glDeleteTextures(1, &pictureTex);
    if (tvTex)      glDeleteTextures(1, &tvTex);
    postfx.destroy();
    backdrop.destroy();
    room.destroy();
    shader.destroy();
    LOGI("App::shutdown");
}
