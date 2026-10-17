#include "scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace application {

std::array<Vertex, 24> cubeVertices() {
    constexpr std::array<glm::vec3, 8> positions = {
        glm::vec3{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
    };
    constexpr std::array<std::array<int, 4>, 6> faces = {{
        {{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 4, 7, 3}},
        {{1, 2, 6, 5}}, {{0, 1, 5, 4}}, {{3, 7, 6, 2}},
    }};
    std::array<Vertex, 24> vertices{};
    for (std::size_t face = 0; face < faces.size(); ++face) {
        const auto& f = faces[face];
        const auto normal = glm::normalize(glm::cross(positions[f[1]] - positions[f[0]],
                                                      positions[f[2]] - positions[f[0]]));
        for (std::size_t corner = 0; corner < 4; ++corner) {
            const auto position = positions[f[corner]];
            vertices[face*4+corner] = {position, (position + glm::vec3(1)) * 0.5f, normal};
        }
    }
    return vertices;
}

glm::vec3 safeDirection(glm::vec3 direction) {
    const float length = glm::length(direction);
    return length > 0.00001f ? direction / length : glm::vec3(0, -1, 0);
}

std::vector<Vertex> spotlightGuide(const SpotLight& light) {
    const auto axis = safeDirection(light.direction);
    const auto helper = std::abs(axis.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const auto right = glm::normalize(glm::cross(axis, helper));
    const auto up = glm::cross(right, axis);
    const float length = std::max(light.guide_length, 0.01f);
    const auto center = light.position + length * axis;
    std::vector<Vertex> result;
    const auto line = [&](glm::vec3 a, glm::vec3 b) {
        result.push_back({a, glm::vec3(1), glm::vec3(0, 1, 0)});
        result.push_back({b, glm::vec3(1), glm::vec3(0, 1, 0)});
    };
    line(light.position, center);
    for (float angle : {light.inner_degrees, light.outer_degrees}) {
        const float radius = length * std::tan(glm::radians(std::clamp(angle, 0.0f, 89.0f)));
        for (int i = 0; i < 32; ++i) {
            const float a = glm::radians(i * 360.0f / 32);
            const float b = glm::radians((i+1) * 360.0f / 32);
            const auto p = center + radius * (std::cos(a) * right + std::sin(a) * up);
            const auto q = center + radius * (std::cos(b) * right + std::sin(b) * up);
            line(p, q);
            if (i % 8 == 0) line(light.position, p);
        }
    }
    return result;
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

glm::vec3 Camera::forward() const {
    const auto angles = glm::radians(rotation_degrees);
    return {std::cos(angles.x) * std::cos(angles.y), std::sin(angles.x),
            std::cos(angles.x) * std::sin(angles.y)};
}

void Camera::rotate(glm::vec2 delta_degrees) {
    rotation_degrees += delta_degrees;
    rotation_degrees.x = std::clamp(rotation_degrees.x, -89.0f, 89.0f);
    rotation_degrees.y = std::remainder(rotation_degrees.y, 360.0f);
}

void Camera::move(glm::vec3 local_delta) {
    const auto right = glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
    position += right * local_delta.x + glm::vec3(0, 1, 0) * local_delta.y
                + forward() * local_delta.z;
}

glm::mat4 Camera::view() const {
    return glm::lookAtRH(position, position + forward(), glm::vec3(0, 1, 0));
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
    // Cube coordinates are [-1, 1], so scale contains half extents.
    constexpr float floor_y = -3.5f, ceiling_y = 4.8f, thickness = 0.1f;
    const float wall_half_height = (ceiling_y - floor_y) * 0.5f;
    const float wall_center_y = (ceiling_y + floor_y) * 0.5f;
    const std::array<glm::vec3, 4> positions = {{
        {0, floor_y - thickness, 0}, {0, wall_center_y, -6.0f - thickness},
        {-6.0f - thickness, wall_center_y, 0}, {0, ceiling_y + thickness, 0},
    }};
    const std::array<glm::vec3, 4> scales = {{
        {6, thickness, 6}, {6, wall_half_height, thickness},
        {thickness, wall_half_height, 6}, {6, thickness, 6},
    }};
    const std::array<glm::vec3, 4> colors = {{
        {0.42f, 0.46f, 0.52f}, {0.72f, 0.68f, 0.60f},
        {0.48f, 0.60f, 0.68f}, {0.78f, 0.78f, 0.74f},
    }};
    for (std::size_t i = 0; i < room.size(); ++i) {
        room[i].animation.enabled = false;
        room[i].transform.position = positions[i];
        room[i].transform.scale = scales[i];
        room[i].albedo = colors[i];
        room[i].specular = glm::vec3(0.12f);
        room[i].shininess = 32.0f;
    }

    for (std::size_t i = 0; i < cubes.size(); ++i) {
        resetTransform(i);
    }
    cubes[0].animation.orbit_phase_degrees = 180.0f;
    cubes[1].albedo = glm::vec3(0.65f, 1.0f, 0.85f);
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
