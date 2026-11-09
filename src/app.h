#pragma once

#include "core/shader.h"
#include "core/model.h"
#include "core/camera.h"
#include "core/postfx.h"
#include "core/textrender.h"
#include "core/nowplaying.h"
#include "core/audiobeat.h"

#include <memory>
#include <string>
#include <vector>

// A rectangle of the window that shows its own copy of the room (one per
// screen in fullscreen mode). Window client pixels, top-left origin.
struct SceneRect { int x = 0, y = 0, w = 0, h = 0; };

// The renderer/scene: loads the room glTF, binds the dynamic surfaces (TV album
// art, framed picture, clock, bias lights) and draws it with a forward
// lambert+emissive shader + HDR bloom under a fixed isometric camera.
struct App {
    bool init(int width, int height);
    void resize(int width, int height);
    void update(double dt);
    void render();
    void shutdown();

    int    width  = 0;
    int    height = 0;
    double timeSec = 0.0;

    // Where the room is drawn (set before init()). Empty = fill the whole window.
    // Everything outside these rectangles is black.
    std::vector<SceneRect> sceneRects;

    // User settings (from the registry via Settings). Set before init().
    std::string picturePathOverride;   // configured framed-picture path (UTF-8); empty = default
    std::string idleText = "kc";       // TV wordmark when nothing is playing (UTF-8; "" = blank)
    float beatSensitivity = 1.0f;      // -> audio.sensitivity
    float brightness      = 1.0f;      // global scene brightness multiplier

    Shader     shader;
    Model      room;
    IsoCamera  camera;
    bool       modelLoaded = false;

    // Post-processing (HDR + bloom): one PostFX per distinct room size, shared
    // by same-sized screens. `rects` is the active layout, `rectFx[i]` the
    // PostFX that rects[i] renders through.
    std::string                          shaderDir;
    std::vector<std::unique_ptr<PostFX>> fx;
    std::vector<SceneRect>               rects;
    std::vector<int>                     rectFx;
    bool rebuildLayout();          // (re)create rects/fx for width x height
    void renderRoom(PostFX& p, const SceneRect& r);

    Shader     backdrop;           // gradient-glow backdrop
    glm::vec3  glowColor{0.60f, 0.30f, 0.12f};   // eased backdrop glow (album-driven)

    // Resolved dynamic surfaces (see docs/ASSET_CONTRACT.md for the node names).
    // The three texture quads get an engine-allocated Material (the glTF leaves
    // them material-less) so runtime textures bind by setting its baseTex. The
    // bias-light meshes own unique emissive materials whose color is driven.
    ModelNode* tvScreen     = nullptr;   // TV_Screen      <- album art
    ModelNode* picture      = nullptr;   // Picture_Image  <- local image
    ModelNode* clock        = nullptr;   // Clock_Display  <- clock text
    ModelNode* biasSoundbar = nullptr;   // Soundbar_LED   <- album color + beat (optional)
    ModelNode* biasTV       = nullptr;   // TV_BiasLight   <- album color + beat

    // Indices into room.materials for the three dynamic quads (set in setup).
    int tvMatIdx      = -1;
    int pictureMatIdx = -1;
    int clockMatIdx   = -1;

    // Local dynamic surfaces.
    TextRenderer clockFont;
    GLuint  clockTex   = 0;
    GLuint  pictureTex = 0;
    GLuint  tvTex      = 0;
    int     lastClockMin = -1;
    std::string picturePath;   // local image for the framed picture

    // Now-playing album art on the TV.
    NowPlaying nowPlaying;
    bool tvHasArt = false;
    bool tvShowingPlaceholder = false;   // TV is showing the idle text screen

    // Album-art driven color. glowColor (above) is the eased current backdrop
    // glow; the bias-light meshes get their emissive set each frame.
    int  biasSoundbarMat = -1;                 // material index of the soundbar LED
    int  biasTVMat       = -1;                 // material index of the TV backlight
    glm::vec3 defGlow{0.60f, 0.30f, 0.12f};    // warm default backdrop glow
    glm::vec3 glowTarget{0.60f, 0.30f, 0.12f};
    glm::vec3 defSoundbarEmissive{1.0f};       // captured original emissives
    glm::vec3 defTVEmissive{0.0f, 0.386f, 1.0f};

    // Wall sconce: a point light so it casts a warm pool on the room. Disabled
    // (zero color) unless the model has a wall-lamp node.
    bool      haveLamp = false;
    glm::vec3 lampPos{0.0f};
    glm::vec3 lampColor{1.0f, 0.82f, 0.60f};
    float     lampRadius    = 4.6f;
    float     lampIntensity = 1.55f;
    glm::vec3 soundbarCol{1.0f};               // eased current bias colors
    glm::vec3 tvBiasCol{0.0f, 0.386f, 1.0f};
    glm::vec3 accentColor{0.60f, 0.30f, 0.12f};// latest album accent
    bool haveAccent = false;

    // Beat flicker.
    AudioBeat audio;
    float biasBaseIntensity = 0.45f;           // steady bias-light level (soft bar)
    float biasFlicker       = 1.9f;            // extra level added on a beat (punchy)
    float tvBacklightGain   = 1.6f;            // TV backlight glows stronger than soundbar
    float tvBacklightScale  = 1.42f;           // bundled room: enlarge the backlight plane
    double lastBeatLog = 0.0;

    glm::vec3 computeAccent(const std::vector<unsigned char>& rgba, int w, int h) const;

    void setupDynamicSurfaces();
    void updateClock();       // re-raster HH:MM when the minute changes
    void loadPicture();       // load picturePath (or a procedural placeholder)
    void makeTVPlaceholder(); // idle TV screen shown when nothing is playing
    void setTVTexture(const unsigned char* rgba, int w, int h);  // compose + upload album art
};
