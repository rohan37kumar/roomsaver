#pragma once

#include <glad/gl.h>
#include <string>
#include "core/shader.h"

// Offscreen HDR render target + bloom post-processing.
//
// The scene (backdrop + room) is rendered into a 4x-MSAA HDR buffer to keep the
// crisp low-poly edges, then resolved to a single-sample texture. A bright-pass
// extracts the glowing bits at half resolution, a separable gaussian blurs them,
// and a composite pass adds them back over the scene into the default
// framebuffer. Draw the scene between beginScene() and endScene().
struct PostFX {
    int width = 0, height = 0;     // full render resolution
    int bw = 0, bh = 0;            // bloom (half) resolution
    int samples = 4;

    // Multisampled HDR scene target (renderbuffers).
    GLuint msFbo = 0, msColor = 0, msDepth = 0;
    // Single-sample resolved scene colour.
    GLuint sceneFbo = 0, sceneTex = 0;
    // Bloom ping-pong buffers (half res).
    GLuint bloomFbo[2] = {0, 0}, bloomTex[2] = {0, 0};

    GLuint vao = 0;                // empty VAO for the fullscreen triangle
    Shader bright, blur, composite;

    // Tunables.
    float threshold     = 1.0f;
    float knee          = 0.6f;
    float bloomStrength = 1.1f;
    int   blurIters     = 5;       // number of H+V blur iterations

    bool init(int w, int h, const std::string& shaderDir);
    void destroy();
    void resize(int w, int h);

    void beginScene();             // bind MSAA scene FBO + full viewport
    // Resolve -> bloom -> composite to FBO 0, into the width x height rectangle
    // whose bottom-left corner is (dstX, dstY) in window pixels (GL origin).
    void endScene(int dstX = 0, int dstY = 0);
    void drawFullscreen();         // bind VAO, draw the fullscreen triangle

private:
    void createTargets();
    void destroyTargets();
};
