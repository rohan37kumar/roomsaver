#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

// Fixed-angle orthographic isometric camera (the screensaver never moves it,
// but yaw/pitch stay tunable so we can match the reference framing).
struct IsoCamera {
    glm::vec3 target{0.0f};
    float yawDeg   = 45.0f;      // rotation around vertical axis
    float pitchDeg = 35.264f;    // classic isometric elevation
    float distance = 30.0f;      // ortho: affects clipping only, not scale
    float orthoHalfHeight = 5.0f;
    float nearP = 0.05f;
    float farP  = 300.0f;

    glm::vec3 eye() const {
        float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
        glm::vec3 dir{ std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y) };
        return target + dir * distance;
    }

    glm::mat4 view() const {
        return glm::lookAt(eye(), target, glm::vec3(0, 1, 0));
    }

    glm::mat4 proj(float aspect) const {
        float hh = orthoHalfHeight;
        float hw = hh * aspect;
        return glm::ortho(-hw, hw, -hh, hh, nearP, farP);
    }

    glm::mat4 viewProj(float aspect) const { return proj(aspect) * view(); }

    // Frame a bounding sphere so the whole model fits with a little margin.
    void fitBounds(const glm::vec3& center, float radius) {
        target = center;
        orthoHalfHeight = radius * 1.05f;
        distance = radius * 3.0f;
        nearP = 0.05f;
        farP  = distance + radius * 3.0f;
    }
};
