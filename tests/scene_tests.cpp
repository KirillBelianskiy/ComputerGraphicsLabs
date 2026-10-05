#include "scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool close(float a, float b, float tolerance = 0.0001f) {
    return std::abs(a - b) < tolerance;
}
bool same(const glm::mat4& a, const glm::mat4& b) {
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            if (!close(a[col][row], b[col][row])) return false;
    return true;
}
glm::vec3 ndc(const glm::mat4& matrix, const glm::vec3& point) {
    const auto clip = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(clip) / clip.w;
}
float projectedWidth(const glm::mat4& projection, float depth) {
    return ndc(projection, {1, 0, -depth}).x - ndc(projection, {-1, 0, -depth}).x;
}
void projections() {
    application::Camera camera;
    const float aspect = 16.0f / 9.0f;
    for (const auto kind : {application::Projection::perspective, application::Projection::orthographic}) {
        camera.projection = kind;
        const auto p = camera.projectionMatrix(aspect);
        require(close(ndc(p, {0, 0, -0.1f}).z, 0), "Near plane must map to Vulkan depth 0");
        require(close(ndc(p, {0, 0, -100.0f}).z, 1), "Far plane must map to Vulkan depth 1");
        require(ndc(p, {0, 1, -10}).y < 0, "Positive world Y must map upward in the Vulkan viewport");
        require(ndc(p, {1, 0, -10}).x > 0, "Positive camera X must map right");
        require(close(std::abs(p[1][1] / p[0][0]), aspect), "Projection must preserve viewport aspect");
        const auto invalid_aspect = camera.projectionMatrix(0);
        require(std::isfinite(invalid_aspect[0][0]), "Zero aspect must not produce infinity");
    }
    camera.projection = application::Projection::perspective;
    const auto perspective = camera.projectionMatrix(aspect);
    // Lecture 1, slide 29: forward +Z and screen-positive +Y.
    constexpr float near_plane = 0.1f;
    constexpr float far_plane = 100.0f;
    const float focal = 1.0f / std::tan(glm::radians(camera.fov_degrees) * 0.5f);
    glm::mat4 lecture(0.0f);
    lecture[0][0] = focal / aspect;
    lecture[1][1] = focal;
    lecture[2][2] = far_plane / (far_plane - near_plane);
    lecture[2][3] = 1.0f;
    lecture[3][2] = -far_plane * near_plane / (far_plane - near_plane);
    glm::mat4 basis(1.0f);
    basis[1][1] = -1.0f;
    basis[2][2] = -1.0f;
    require(same(perspective, lecture * basis),
            "RH projection must match the lecture matrix after changing camera axes");
    require(close(projectedWidth(perspective, 5) / projectedWidth(perspective, 10), 2),
            "Perspective size must decrease inversely with depth");
    camera.fov_degrees = 80;
    require(projectedWidth(camera.projectionMatrix(aspect), 5) < projectedWidth(perspective, 5),
            "Wider field of view must make objects smaller");
    camera.projection = application::Projection::orthographic;
    const auto ortho = camera.projectionMatrix(aspect);
    require(close(projectedWidth(ortho, 5), projectedWidth(ortho, 10)),
            "Orthographic size must be independent of depth");
    require(close(ndc(ortho, {camera.orthographic_half_height * aspect, 0, -10}).x, 1),
            "Orthographic right bound must map to NDC 1");
    const auto before = ortho * camera.view();
    camera.distance += 5;
    const auto after = ortho * camera.view();
    require(close(ndc(before, {1, 0, 0}).x, ndc(after, {1, 0, 0}).x) &&
            close(ndc(before, {1, 0, 0}).y, ndc(after, {1, 0, 0}).y),
            "Orthographic camera distance must not change screen position or size");

    application::Scene scene;
    const auto view = scene.camera.view();
    const float near_depth = -(view * glm::vec4(scene.cubes[0].transform.position, 1)).z;
    const float far_depth = -(view * glm::vec4(scene.cubes[1].transform.position, 1)).z;
    require(close(near_depth, 8.5f) && close(far_depth, 13.5f), "Default cubes must have different camera depths");
    require(projectedWidth(perspective, near_depth) > projectedWidth(perspective, far_depth) * 1.5f,
            "Default scene must clearly demonstrate perspective scaling");
    require(close(projectedWidth(ortho, near_depth), projectedWidth(ortho, far_depth)),
            "Default scene must preserve equal orthographic scale");
}
void animation() {
    application::Scene scene;
    require(scene.cubes.size() == 2, "Exactly two cubes are required");
    glm::mat4 initial[2] = {scene.cubes[0].model(), scene.cubes[1].model()};
    scene.update(100);
    scene.update(101);
    for (std::size_t i = 0; i < scene.cubes.size(); ++i) {
        const auto& cube = scene.cubes[i];
        require(cube.animation.enabled && cube.animation.playing, "Both cubes must animate at startup");
        require(!same(initial[i], cube.model()), "Both cubes must rotate");
        require(cube.animation.trajectory_enabled &&
                cube.animation.trajectory == application::Trajectory::circular,
                "Both cubes must follow circles at startup");
        require(close(glm::length(glm::vec3(cube.model()[3]) - cube.transform.position),
                      cube.animation.orbit_radius), "Default motion must stay on the circle");
    }
    scene.setAnimationsPlaying(false);
    const auto paused0 = scene.cubes[0].model();
    const auto paused1 = scene.cubes[1].model();
    scene.update(105);
    require(same(paused0, scene.cubes[0].model()) && same(paused1, scene.cubes[1].model()),
            "Pause both must preserve both animated poses");
    scene.setAnimationsPlaying(true);
    scene.update(106);
    require(close(static_cast<float>(scene.cubes[0].animation.time), 2), "Resume must not include paused time");
    scene.cubes[0].animation.speed = 0;
    const auto stopped = scene.cubes[0].model();
    scene.update(107);
    require(same(stopped, scene.cubes[0].model()), "Zero speed must freeze the selected cube");
    require(!same(paused1, scene.cubes[1].model()), "Other cube must keep animating independently");
    scene.cubes[0].animation.speed = 2;
    const auto angle = scene.cubes[0].animation.rotation_degrees;
    scene.update(107.5);
    require(glm::length(scene.cubes[0].animation.rotation_degrees - angle -
                        glm::dvec3(scene.cubes[0].animation.rotation_speed_degrees)) < 0.0001,
            "Speed changes must integrate from the current angle");
    scene.cubes[0].animation.trajectory_enabled = true;
    scene.cubes[0].animation.trajectory = application::Trajectory::complex;
    require(glm::length(scene.cubes[0].animation.offset()) > 0.01f, "3D path must create movement");
    scene.restartAnimations();
    for (const auto& cube : scene.cubes) {
        require(cube.animation.time == 0 && cube.animation.enabled && cube.animation.playing,
                "Restart must reset and play both animations");
        require(cube.animation.orbit_angle_degrees == 0, "Restart must reset the orbit angle");
        if (cube.animation.trajectory == application::Trajectory::complex) {
            require(glm::length(cube.animation.offset()) < 0.0001f, "Complex path reset must return to the base position");
        } else {
            require(close(glm::length(cube.animation.offset()), cube.animation.orbit_radius),
                    "Circular reset must return to the start of the circle");
        }
    }
    const auto reset = scene.cubes[0].model();
    scene.update(std::numeric_limits<double>::quiet_NaN());
    require(same(reset, scene.cubes[0].model()), "Invalid clock values must not corrupt the scene");
}
void individualPlayback() {
    for (const auto trajectory : {application::Trajectory::circular, application::Trajectory::complex}) {
        for (std::size_t paused_index = 0; paused_index < 2; ++paused_index) {
            application::Scene scene;
            for (auto& cube : scene.cubes) cube.animation.trajectory = trajectory;
            scene.update(100);
            scene.update(101);
            auto& paused = scene.cubes[paused_index];
            auto& running = scene.cubes[1 - paused_index];
            paused.animation.playing = false;
            const auto paused_pose = paused.model();
            const auto running_pose = running.model();
            scene.update(106);
            require(same(paused_pose, paused.model()) && paused.animation.time == 1,
                    "Individual pause must freeze both movement and rotation of its cube");
            require(!same(running_pose, running.model()) && running.animation.time == 6,
                    "Individual pause must leave the other cube running");
            paused.animation.playing = true;
            require(same(paused_pose, paused.model()), "Resume must preserve the paused pose immediately");
            scene.update(106.25);
            require(!same(paused_pose, paused.model()) && paused.animation.time == 1.25,
                    "Individual resume must continue without including paused time");
            scene.setAnimationsPlaying(false);
            require(!paused.animation.playing && !running.animation.playing,
                    "Global pause must still stop both cubes after individual playback");
            scene.setAnimationsPlaying(true);
            require(paused.animation.playing && running.animation.playing,
                    "Global play must still start both cubes after individual playback");
        }
    }
}
void circularMotion() {
    application::Animation animation;
    animation.orbit_speed_degrees = 90.0f;
    for (auto plane : {application::OrbitPlane::xy, application::OrbitPlane::xz, application::OrbitPlane::yz}) {
        animation.orbit_plane = plane;
        for (float radius : {0.0f, 0.3f, 1.0f, 2.5f}) {
            animation.orbit_radius = radius;
            animation.reset();
            const auto start = animation.offset();
            for (int step = 1; step <= 16; ++step) {
                animation.advance(0.25);
                const auto position = animation.offset();
                require(close(glm::length(position), radius), "Distance to circle center must equal its radius");
                const int fixed_axis = plane == application::OrbitPlane::xy ? 2
                                     : plane == application::OrbitPlane::xz ? 1 : 0;
                require(close(position[fixed_axis], 0), "Circular motion must stay in its selected plane");
            }
            require(glm::length(animation.offset()-start) < 0.0001f, "One revolution must close the circle");
        }
    }
    animation.orbit_plane = application::OrbitPlane::xy;
    animation.orbit_radius = 1.0f;
    animation.reset();
    animation.advance(1.0);
    require(glm::length(animation.offset()-glm::vec3(0, 1, 0)) < 0.0001f,
            "A quarter revolution must move from +X to +Y");
    const auto before_speed_edit = animation.offset();
    animation.orbit_speed_degrees = -90.0f;
    require(glm::length(animation.offset()-before_speed_edit) < 0.0001f,
            "Editing orbit speed must not jump the phase");
    animation.advance(1.0);
    require(glm::length(animation.offset()-glm::vec3(1, 0, 0)) < 0.0001f,
            "Negative speed must reverse the orbit");
    animation.orbit_radius = 2.0f;
    require(glm::length(animation.offset()-glm::vec3(2, 0, 0)) < 0.0001f,
            "Editing radius must keep phase and scale distance from the center");
    animation.playing = false;
    const auto paused = animation.offset();
    animation.advance(10.0);
    require(glm::length(animation.offset()-paused) < 0.0001f, "Pause must freeze circular motion");
    animation.trajectory_enabled = false;
    require(glm::length(animation.offset()) == 0, "Disabling the path must restore rotation in place");
}
void complexPathSize() {
    application::Animation animation;
    animation.trajectory = application::Trajectory::complex;
    animation.advance(1.25);
    const auto original = animation.offset();
    require(glm::length(original) > 0.01f, "Complex path must include movement");
    for (float size : {0.0f, 0.5f, 1.0f, 2.5f}) {
        animation.path_scale = size;
        require(glm::length(animation.offset() - original * size) < 0.0001f,
                "One path-size parameter must uniformly scale the complex trajectory");
    }
    animation.reset();
    require(glm::length(animation.offset()) < 0.0001f, "Path size must preserve reset to the base position");
}
void triangleWinding() {
    const auto vertices = application::cubeVertices();
    const auto& indices = application::cube_indices;
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        require(indices[i] < vertices.size() && indices[i+1] < vertices.size() &&
                indices[i+2] < vertices.size(), "Every index must reference a cube vertex");
        const auto a = vertices[indices[i]].position;
        const auto b = vertices[indices[i+1]].position;
        const auto c = vertices[indices[i+2]].position;
        require(glm::dot(glm::cross(b-a, c-a), (a+b+c)/3.0f) > 0,
                "All twelve cube triangles must face outward");
    }
    application::Scene scene;
    for (auto kind : {application::Projection::perspective, application::Projection::orthographic}) {
        scene.camera.projection = kind;
        const auto projection = scene.camera.projectionMatrix(1.2f);
        for (auto& cube : scene.cubes) {
            const auto model_view = scene.camera.view() * cube.model();
            int visible_triangles = 0;
            for (std::size_t i = 0; i < indices.size(); i += 3) {
                glm::vec3 points[3];
                for (int j = 0; j < 3; ++j)
                    points[j] = glm::vec3(model_view * glm::vec4(vertices[indices[i+j]].position, 1));
                const auto normal = glm::cross(points[1]-points[0], points[2]-points[0]);
                const bool facing = kind == application::Projection::perspective
                    ? glm::dot(normal, -(points[0]+points[1]+points[2])/3.0f) > 0
                    : normal.z > 0;
                const auto a = ndc(projection, points[0]);
                const auto b = ndc(projection, points[1]);
                const auto c = ndc(projection, points[2]);
                // Vulkan's polygon-area convention for positive-height framebuffer Y.
                const float area = -0.5f * ((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x));
                require(std::abs(area) > 0.00001f, "Test triangle must not be edge-on");
                require((area > 0) == facing,
                        "CCW culling must keep front-facing triangles in both projections");
                if (facing) ++visible_triangles;
            }
            require(visible_triangles > 0 && visible_triangles < 12,
                    "Culling must keep visible faces and reject hidden faces");
        }
    }
}
void geometryAndTransform() {
    for (const auto& vertex : application::cubeVertices()) {
        require(glm::length(vertex.color - (vertex.position + glm::vec3(1)) * 0.5f) < 0.0001f,
                "Vertex colors must remain procedural");
    }
    application::Cube cube;
    cube.animation.enabled = false;
    cube.transform.position = {3, 4, 5};
    cube.transform.rotation_degrees = {0, 0, 90};
    cube.transform.scale = {2, 3, 4};
    const auto transformed = glm::vec3(cube.model() * glm::vec4(1, 0, 0, 1));
    require(glm::length(transformed - glm::vec3(3, 6, 5)) < 0.0001f,
            "Model must apply scale, then rotation, then translation");
}
} // namespace
int main() {
    try {
        projections();
        animation();
        individualPlayback();
        geometryAndTransform();
        triangleWinding();
        circularMotion();
        complexPathSize();
        std::cout << "PASS: projections, depth, aspect, two cubes, playback, circle radius, orbit planes, complex path, transforms, lecture matrix, outward winding and culling\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
