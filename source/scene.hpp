#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace application {

constexpr std::size_t cube_count = 2;

struct Vertex {
    glm::vec3 position;
    glm::vec3 color;
    glm::vec3 normal;
};

// Separate vertices per face preserve the cube's hard edges.
std::array<Vertex, 24> cubeVertices();
inline constexpr auto cube_indices = [] {
    std::array<std::uint16_t, 36> indices{};
    for (std::uint16_t face = 0; face < 6; ++face) {
        const std::uint16_t base = face * 4;
        const std::size_t at = face * 6;
        indices[at] = base; indices[at+1] = base+1; indices[at+2] = base+2;
        indices[at+3] = base; indices[at+4] = base+2; indices[at+5] = base+3;
    }
    return indices;
}();

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
    glm::vec3 albedo{1.0f};
    Animation animation;
    glm::vec3 specular{1.0f};
    float shininess = 64.0f;
    bool vertex_colors = false;
    [[nodiscard]] glm::mat4 model() const;
};

enum class Projection { perspective, orthographic };

struct Camera {
    Projection projection = Projection::perspective;
    float fov_degrees = 48.0f;
    float orthographic_half_height = 4.9f;
    glm::vec3 position{0.0f, 3.352533f, 10.476666f};
    // Pitch and yaw in degrees; yaw -90 looks along world -Z.
    glm::vec2 rotation_degrees{-17.744672f, -90.0f};
    float move_speed = 4.0f;
    [[nodiscard]] glm::vec3 forward() const;
    void move(glm::vec3 local_delta);
    void rotate(glm::vec2 delta_degrees);
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspect) const;
};

glm::vec3 safeDirection(glm::vec3 direction);

struct DirectionalLight {
    glm::vec3 direction{-0.5f, -1.0f, -0.4f}; // direction of light travel
    glm::vec3 color{1.0f, 0.95f, 0.85f};
    float intensity = 0.7f;
};
struct PointLight {
    glm::vec3 position{-3.0f, 2.0f, 3.0f};
    glm::vec3 color{0.3f, 0.65f, 1.0f};
    float intensity = 25.0f;
};
struct SpotLight {
    glm::vec3 position{3.0f, 4.0f, 4.0f};
    glm::vec3 direction{-0.5f, -0.6f, -1.0f};
    glm::vec3 color{1.0f, 0.45f, 0.2f};
    float intensity = 50.0f;
    float inner_degrees = 15.0f; // half angles
    float outer_degrees = 25.0f;
    float guide_length = 3.0f;
};
// White line segments in world space for the axis and both cone boundaries.
std::vector<Vertex> spotlightGuide(const SpotLight& light);

struct Scene {
    std::array<Cube, cube_count> cubes{};
    // Floor, back wall, left wall, ceiling: instances of the same cube mesh.
    std::array<Cube, 4> room{};
    bool show_room = true;
    Camera camera;
    glm::vec3 ambient_color{0.8f, 0.9f, 1.0f};
    float ambient_intensity = 0.08f;
    DirectionalLight directional;
    std::vector<PointLight> point_lights{PointLight{}};
    std::vector<SpotLight> spot_lights{SpotLight{}};
    bool show_light_markers = true;
    bool show_spot_guides = true;
    int selected_cube = 0;
    float viewport_left = 0.0f;
    double previous_time = 0.0;
    bool clock_started = false;

    Scene();
    void update(double absolute_time);
    void resetTransform(std::size_t index);
    void setAnimationsPlaying(bool playing);
    void restartAnimations();
};

// GPU contracts: std140 UBOs and std430 SSBO array elements, all in world space.
struct alignas(16) SceneUniformData {
    glm::mat4 view_projection;
    glm::vec4 camera_position;
    glm::vec4 ambient;
    glm::vec4 directional_direction_intensity;
    glm::vec4 directional_color;
    glm::uvec4 light_counts;
};
struct alignas(16) ModelUniformData {
    glm::mat4 model;
    glm::mat4 normal_matrix;
    glm::vec4 albedo; // w selects procedural vertex colors
    glm::vec4 specular_shininess;
    glm::vec4 emission; // w = 1 for unlit visualization
};
struct alignas(16) PointLightData {
    glm::vec4 position;
    glm::vec4 color_intensity;
};
struct alignas(16) SpotLightData {
    glm::vec4 position;
    glm::vec4 direction_inner_cos;
    glm::vec4 color_intensity;
    glm::vec4 outer_cos;
};

} // namespace application
