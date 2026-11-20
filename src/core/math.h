#pragma once
// Math layer: glm under the hood, engine code only uses gc6 types.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
namespace gc6 {
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;
inline Mat4 perspective(float fovDeg, float aspect, float nearP, float farP) {
    return glm::perspective(glm::radians(fovDeg), aspect, nearP, farP);
}
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    return glm::lookAt(eye, target, up);
}
inline Mat4 translate(const Mat4& m, const Vec3& v) { return glm::translate(m, v); }
inline Mat4 rotateY(const Mat4& m, float radians) { return glm::rotate(m, radians, Vec3(0, 1, 0)); }
inline Mat4 rotateX(const Mat4& m, float radians) { return glm::rotate(m, radians, Vec3(1, 0, 0)); }
inline Mat4 rotateZ(const Mat4& m, float radians) { return glm::rotate(m, radians, Vec3(0, 0, 1)); }
inline Mat4 scale(const Mat4& m, const Vec3& v) { return glm::scale(m, v); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float deg2rad(float d) { return d * glm::pi<float>() / 180.0f; }
inline float rad2deg(float r) { return r * 180.0f / glm::pi<float>(); }
inline float length(const Vec3& v) { return glm::length(v); }
inline Vec3 normalize(const Vec3& v) { return glm::normalize(v); }
inline float dot(const Vec3& a, const Vec3& b) { return glm::dot(a, b); }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return glm::cross(a, b); }
} // namespace gc6
