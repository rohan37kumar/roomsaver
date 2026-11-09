#include "core/postfx.h"
#include "core/log.h"

#include <glm/glm.hpp>
#include <utility>

static GLuint make_color_tex(int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

bool PostFX::init(int w, int h, const std::string& shaderDir) {
    glGenVertexArrays(1, &vao);   // empty VAO; fullscreen tri uses gl_VertexID

    bool ok = true;
    ok &= bright.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/bright.frag");
    ok &= blur.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/blur.frag");
    ok &= composite.loadFromFiles(shaderDir + "/fullscreen.vert", shaderDir + "/composite.frag");
    if (!ok) { LOGI("PostFX::init: post shaders failed to load"); return false; }

    width = w; height = h;
    createTargets();
    LOGI("PostFX::init %dx%d (bloom %dx%d, %dx MSAA)", width, height, bw, bh, samples);
    return true;
}

void PostFX::createTargets() {
    bw = width  > 1 ? width  / 2 : 1;
    bh = height > 1 ? height / 2 : 1;

    // Multisampled HDR scene target.
    glGenFramebuffers(1, &msFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glGenRenderbuffers(1, &msColor);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
    glGenRenderbuffers(1, &msDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        LOGI("PostFX: MSAA scene FBO incomplete");

    // Single-sample resolve target.
    glGenFramebuffers(1, &sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    sceneTex = make_color_tex(width, height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        LOGI("PostFX: resolve FBO incomplete");

    // Bloom ping-pong (half res).
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &bloomFbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[i]);
        bloomTex[i] = make_color_tex(bw, bh);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            LOGI("PostFX: bloom FBO %d incomplete", i);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostFX::destroyTargets() {
    glDeleteFramebuffers(1, &msFbo);    msFbo = 0;
    glDeleteRenderbuffers(1, &msColor); msColor = 0;
    glDeleteRenderbuffers(1, &msDepth); msDepth = 0;
    glDeleteFramebuffers(1, &sceneFbo); sceneFbo = 0;
    glDeleteTextures(1, &sceneTex);     sceneTex = 0;
    glDeleteFramebuffers(2, bloomFbo);  bloomFbo[0] = bloomFbo[1] = 0;
    glDeleteTextures(2, bloomTex);      bloomTex[0] = bloomTex[1] = 0;
}

void PostFX::resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == width && h == height)) return;
    width = w; height = h;
    destroyTargets();
    createTargets();
}

void PostFX::destroy() {
    destroyTargets();
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    bright.destroy();
    blur.destroy();
    composite.destroy();
}

void PostFX::drawFullscreen() {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void PostFX::beginScene() {
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, width, height);
}

void PostFX::endScene() {
    // Resolve MSAA scene -> single-sample texture.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Post passes are 2D; no depth needed.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Bright-pass: full-res scene -> half-res bloom[0].
    glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[0]);
    glViewport(0, 0, bw, bh);
    bright.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    bright.set("uScene", 0);
    bright.set("uThreshold", threshold);
    bright.set("uKnee", knee);
    drawFullscreen();

    // Separable gaussian ping-pong.
    blur.use();
    blur.set("uTex", 0);
    int src = 0, dst = 1;
    bool horizontal = true;
    for (int i = 0; i < blurIters * 2; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo[dst]);
        glViewport(0, 0, bw, bh);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, bloomTex[src]);
        glm::vec2 dir = horizontal ? glm::vec2(1.0f / (float)bw, 0.0f)
                                   : glm::vec2(0.0f, 1.0f / (float)bh);
        blur.set("uDir", dir);
        drawFullscreen();
        std::swap(src, dst);
        horizontal = !horizontal;
    }
    // Blurred result now lives in bloomTex[src].

    // Composite scene + bloom -> default framebuffer (the window back buffer).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    composite.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    composite.set("uScene", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex[src]);
    composite.set("uBloom", 1);
    composite.set("uBloomStrength", bloomStrength);
    drawFullscreen();

    // Restore default-ish state for the next frame's scene pass.
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
