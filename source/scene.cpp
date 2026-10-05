#include "scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace application {

std::array<Vertex, 8> cubeVertices() {
    constexpr std::array<glm::vec3, 8> positions = {
        glm::vec3{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    };
    std::array<Vertex, 8> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i] = {positions[i], (positions[i] + glm::vec3(1.0f)) * 0.5f};
    }
    return vertices;
}

void Animation::advance(double delta_seconds) {
    if (!enabled || !playing || !std::isfinite(delta_seconds) || delta_seconds <= 0) {
        return;
    }
    const double step = delta_seconds * speed;
    time += step;
    // Integrate the orbit angle too: changing speed preserves the current phase.
    orbit_angle_degrees = std::fmod(orbit_angle_degrees + orbit_speed_degrees * step, 360.0);
    // Integrate rotation, so editing its speed does not change the current angle.
    rotation_degrees += glm::dvec3(rotation_speed_degrees) * step;
    for (int axis = 0; axis < 3; ++axis) {
        rotation_degrees[axis] = std::fmod(rotation_degrees[axis], 360.0);
    }
}

void Animation::reset() {
    time = 0.0;
    orbit_angle_degrees = 0.0;
    rotation_degrees = glm::dvec3(0.0);
}

glm::vec3 Animation::offset() const {
    if (!enabled || !trajectory_enabled) {
        return glm::vec3(0.0f);
    }
    if (trajectory == Trajectory::circular) {
        const double angle = glm::radians(orbit_angle_degrees + orbit_phase_degrees);
        const float u = orbit_radius * static_cast<float>(std::cos(angle));
        const float v = orbit_radius * static_cast<float>(std::sin(angle));
        switch (orbit_plane) {
        case OrbitPlane::xy: return {u, v, 0.0f};
        case OrbitPlane::xz: return {u, 0.0f, v};
        case OrbitPlane::yz: return {0.0f, u, v};
        }
    }
    const glm::dvec3 phase = glm::radians(glm::dvec3(phases_degrees));
    const glm::dvec3 angle = glm::dvec3(frequencies) * time + phase;
    // Subtract the initial sample: Reset returns exactly to the manual position.
    return path_scale * radii * glm::vec3(std::sin(angle.x) - std::sin(phase.x),
                            std::sin(angle.y) - std::sin(phase.y),
                            std::cos(angle.z) - std::cos(phase.z));
}

glm::mat4 Cube::model() const {
    const glm::vec3 animated_rotation = animation.enabled
        ? glm::vec3(animation.rotation_degrees) : glm::vec3(0.0f);
    const glm::vec3 angles = glm::radians(transform.rotation_degrees + animated_rotation);
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f),
                                      transform.position + animation.offset());
    matrix = glm::rotate(matrix, angles.z, glm::vec3(0, 0, 1));
    matrix = glm::rotate(matrix, angles.y, glm::vec3(0, 1, 0));
    matrix = glm::rotate(matrix, angles.x, glm::vec3(1, 0, 0));
    return glm::scale(matrix, transform.scale); // T * Rz * Ry * Rx * S
}

glm::mat4 Camera::view() const {
    const glm::vec3 direction = glm::normalize(glm::vec3(0.0f, 0.32f, 1.0f));
    const glm::vec3 target(0.0f);
    return glm::lookAtRH(target + direction * distance, target, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    aspect = std::max(aspect, 0.001f);
    glm::mat4 matrix;
    if (projection == Projection::perspective) {
        matrix = glm::perspectiveRH_ZO(glm::radians(fov_degrees), aspect, 0.1f, 100.0f);
    } else {
        const float height = orthographic_half_height;
        matrix = glm::orthoRH_ZO(-height * aspect, height * aspect,
                                 -height, height, 0.1f, 100.0f);
    }
    // Lecture 1 uses forward +Z; this camera uses RH forward -Z instead.
    // Both map near/far to 0/1. Flip Y for the positive-height Vulkan viewport.
    matrix[1][1] *= -1.0f;
    return matrix;
}

Scene::Scene() {
    for (std::size_t i = 0; i < cubes.size(); ++i) {
        resetTransform(i);
    }
    cubes[0].animation.orbit_phase_degrees = 180.0f;
    cubes[1].tint = glm::vec3(0.65f, 1.0f, 0.85f);
    cubes[1].animation.rotation_speed_degrees = glm::vec3(-20.0f, 35.0f, -10.0f);
}

void Scene::resetTransform(std::size_t index) {
    auto& transform = cubes.at(index).transform;
    transform = Transform{};
    // Equal cubes at different camera depths make projection differences visible.
    const float side = index == 0 ? -1.0f : 1.0f;
    const glm::vec3 camera_direction = glm::normalize(glm::vec3(0.0f, 0.32f, 1.0f));
    transform.position = glm::vec3(side * 1.8f, 0.0f, 0.0f)
                       - side * 2.5f * camera_direction;
    transform.rotation_degrees = glm::vec3(18.0f, 28.0f, 0.0f);
}

void Scene::setAnimationsPlaying(bool playing) {
    for (auto& cube : cubes) {
        cube.animation.playing = playing;
    }
}

void Scene::restartAnimations() {
    for (auto& cube : cubes) {
        cube.animation.reset();
        cube.animation.enabled = true;
        cube.animation.playing = true;
    }
}

void Scene::update(double absolute_time) {
    if (!std::isfinite(absolute_time)) {
        return;
    }
    const double delta = clock_started ? std::max(0.0, absolute_time - previous_time) : 0.0;
    previous_time = absolute_time;
    clock_started = true;
    for (auto& cube : cubes) {
        cube.animation.advance(delta);
    }
}

} // namespace application
