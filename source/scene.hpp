#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace application {

constexpr std::size_t cube_count = 2;

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
};

std::array<Vertex, 8> cubeVertices();
inline constexpr std::array<std::uint16_t, 36> cube_indices = {
    0, 2, 1, 0, 3, 2, // -Z
    4, 5, 6, 4, 6, 7, // +Z
    0, 4, 7, 0, 7, 3, // -X
    1, 2, 6, 1, 6, 5, // +X
    0, 1, 5, 0, 5, 4, // -Y
    3, 7, 6, 3, 6, 2, // +Y
};

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation_degrees{0.0f};
    glm::vec3 scale{0.85f};
};

enum class Trajectory { circular, complex };
enum class OrbitPlane { xy, xz, yz };

struct Animation {
    bool enabled = true;
    bool trajectory_enabled = true;
    Trajectory trajectory = Trajectory::circular;
    OrbitPlane orbit_plane = OrbitPlane::xy;
    float orbit_radius = 1.0f;
    float orbit_speed_degrees = 35.0f;
    float orbit_phase_degrees = 0.0f;
    double orbit_angle_degrees = 0.0;
    bool playing = true;
    float speed = 1.0f;
    float path_scale = 1.0f;
    glm::vec3 radii{0.9f, 0.8f, 1.1f};
    glm::vec3 frequencies{1.0f, 1.7f, 2.3f};
    glm::vec3 phases_degrees{0.0f, 35.0f, 70.0f};
    glm::vec3 rotation_speed_degrees{25.0f, 40.0f, 15.0f};
    double time = 0.0;
    glm::dvec3 rotation_degrees{0.0};

    void advance(double delta_seconds);
    void reset();
    [[nodiscard]] glm::vec3 offset() const;
};

struct Cube {
    Transform transform;
    glm::vec3 tint{1.0f};
    Animation animation;
    [[nodiscard]] glm::mat4 model() const;
};

enum class Projection { perspective, orthographic };

struct Camera {
    Projection projection = Projection::perspective;
    float fov_degrees = 48.0f;
    float orthographic_half_height = 4.9f;
    float distance = 11.0f;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspect) const;
};

struct Scene {
    std::array<Cube, cube_count> cubes{};
    Camera camera;
    int selected_cube = 0;
    // Logical UI coordinates; converted to framebuffer pixels by the renderer.
    float viewport_left = 0.0f;
    double previous_time = 0.0;
    bool clock_started = false;

    Scene();
    void update(double absolute_time);
    void resetTransform(std::size_t index);
    void setAnimationsPlaying(bool playing);
    void restartAnimations();
};

// std140: three column-major mat4 followed by one vec4.
struct alignas(16) UniformData {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 projection;
    alignas(16) glm::vec4 tint;
};

} // namespace application
