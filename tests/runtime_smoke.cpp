#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "application.hpp"
#include "scene.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

// Explicit desktop-only check: real swapchain, descriptors, UI, resize and both projections.
int main() {
    if (!glfwInit()) return EXIT_FAILURE;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* window = glfwCreateWindow(1280, 720, "Lab 2 | Runtime verification", nullptr, nullptr);
    if (!window) { glfwTerminate(); return EXIT_FAILURE; }
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int width, int height) {
        if (width > 0 && height > 0) graphics::internal::resize(width, height);
    });
    ImGui::CreateContext();
    bool backend_ready = ImGui_ImplGlfw_InitForVulkan(window, true);
    bool graphics_ready = false;
    bool application_ready = false;
    int status = EXIT_FAILURE;
    try {
        if (!backend_ready) throw std::runtime_error("ImGui GLFW initialization failed");
        graphics_ready = graphics::internal::initialize(window);
        if (!graphics_ready) throw std::runtime_error("Vulkan initialization failed");
        if (graphics::internal::context.api_version != VK_MAKE_VERSION(1, 1, 0))
            throw std::runtime_error("Vulkan API version does not match the requested 1.1");
        if (!graphics::internal::validationMessengerActive())
            throw std::runtime_error("Vulkan validation messenger was not connected");
        application_ready = application::initialize();
        if (!application_ready) throw std::runtime_error("Application initialization failed");
        auto& scene = application::currentScene();
        int rendered_frames = 0;
        for (int frame = 0; frame < 240 && !glfwWindowShouldClose(window); ++frame) {
            glfwPollEvents();
            if (frame == 70) scene.show_room = false;
            if (frame == 75) {
                scene.show_room = true;
                scene.room[0].albedo = {0.5f, 0.4f, 0.3f};
                scene.room[0].shininess = 96.0f;
            }
            if (frame == 20) scene.cubes[0].animation.playing = false;
            if (frame == 30) scene.cubes[0].animation.playing = true;
            if (frame == 40) scene.setAnimationsPlaying(false);
            if (frame == 60) scene.camera.projection = application::Projection::orthographic;
            if (frame == 80) scene.setAnimationsPlaying(true);
            if (frame == 100) {
                for (auto& cube : scene.cubes) {
                    cube.animation.trajectory = application::Trajectory::complex;
                    cube.animation.path_scale = 0.8f;
                }
            }
            if (frame == 110) scene.cubes[1].animation.playing = false;
            if (frame == 115) scene.cubes[1].animation.playing = true;
            if (frame == 120) glfwSetWindowSize(window, 1000, 650);
            if (frame == 160) {
                for (auto& cube : scene.cubes) {
                    cube.animation.trajectory = application::Trajectory::circular;
                    cube.animation.orbit_radius = 1.5f;
                    cube.animation.orbit_plane = application::OrbitPlane::xz;
                }
                scene.camera.projection = application::Projection::perspective;
                scene.restartAnimations();
            }
            if (frame == 200) {
                glfwSetWindowSize(window, 1280, 720);
                scene.selected_cube = 1;
                scene.cubes[1].transform.scale = {0.7f, 1.1f, 0.9f};
                scene.cubes[1].albedo = {1.0f, 0.7f, 0.85f};
            }
            // Exercise SSBO and dynamic UBO growth, empty arrays and shrinking.
            if (frame == 10) {
                for (int i = 0; i < 40; ++i) {
                    application::PointLight point;
                    point.position = {float(i % 8)-4, 2, float(i / 8)-2};
                    point.intensity = 0.2f;
                    scene.point_lights.push_back(point);
                    application::SpotLight spot;
                    spot.direction = {0, -1, 0};
                    spot.inner_degrees = spot.outer_degrees = 20; // hard edge
                    spot.intensity = 0.2f;
                    scene.spot_lights.push_back(spot);
                }
            }
            if (frame == 25) { scene.point_lights.clear(); scene.spot_lights.clear(); }
            if (frame == 35) {
                scene.point_lights.push_back(application::PointLight{});
                scene.spot_lights.push_back(application::SpotLight{});
                scene.spot_lights[0].direction = {0, 0, 0}; // safe fallback
            }
            if (frame == 45) scene.show_light_markers = false;
            if (frame == 55) scene.show_spot_guides = false;
            if (frame == 65) { scene.show_light_markers = true; scene.show_spot_guides = true; }
            if (frame == 90) {
                scene.camera.position += glm::vec3(1, 0.5f, 0);
                scene.camera.rotate({4, -8});
                scene.camera.fov_degrees = 60;
                scene.cubes[0].specular = {0.5f, 0.7f, 1};
                scene.cubes[0].shininess = 128;
                scene.cubes[0].vertex_colors = true;
            }
            if (frame == 140) {
                scene.spot_lights[0].direction = {-0.5f, -0.6f, -1};
                scene.spot_lights[0].inner_degrees = 10;
                scene.spot_lights[0].outer_degrees = 35;
                scene.directional.intensity = 0;
                scene.ambient_intensity = 0;
            }
            if (frame == 180) {
                scene.directional.intensity = 0.7f;
                scene.ambient_intensity = 0.08f;
                scene.camera = application::Camera{};
            }
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            auto& io = ImGui::GetIO();
            // Exercise the real application input path, not Camera::move directly.
            if (frame == 210 || frame == 214 || frame == 218 || frame == 222) {
                io.AddMousePosEvent(900, 500);
                io.AddKeyEvent(ImGuiKey_W, true);
            }
            if (frame == 211 || frame == 215 || frame == 219 || frame == 223)
                io.AddKeyEvent(ImGuiKey_W, false);
            ImGui::NewFrame();
            const auto camera_before_input = scene.camera.position;
            if (frame == 210) io.WantCaptureKeyboard = true; // navigation focus
            if (frame == 214) io.WantTextInput = true;       // editing a field
            if (frame == 218) io.MousePos = ImVec2(100, 100); // over settings
            if (frame == 222) {
                io.WantCaptureMouse = false;
                io.WantTextInput = false;
                io.WantCaptureKeyboard = false;
            }
            application::update(frame / 60.0);
            if (frame == 210 || frame == 222) {
                const auto delta = scene.camera.position - camera_before_input;
                if (glm::dot(delta, scene.camera.forward()) <= 0.0f)
                    throw std::runtime_error("W must move the camera without RMB, including with UI navigation focus");
            }
            if ((frame == 214 || frame == 218) &&
                glm::length(scene.camera.position - camera_before_input) > 0.00001f)
                throw std::runtime_error("Camera must not move while typing or hovering over settings");
            ImGui::Render();
            const auto fd = graphics::internal::prepare();
            application::render(fd);
            graphics::internal::submitAndPresent();
            ++rendered_frames;
        }
        if (rendered_frames != 240) throw std::runtime_error("Runtime verification interrupted");
        status = EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    if (application_ready) application::shutdown();
    // Handles partial graphics initialization as in the production entry point.
    graphics::internal::shutdown();
    if (graphics::internal::validationErrorCount() != 0) {
        std::cerr << "FAIL: " << graphics::internal::validationErrorCount() << " Vulkan validation errors\n";
        status = EXIT_FAILURE;
    }
    if (status == EXIT_SUCCESS) {
        std::cout << "PASS: Vulkan 1.1, 240 frames, both projections, playback, paths, resize, "
                     "circle radius, complex path size, individual pause/resume, per-cube edits, free camera, materials, SSBO growth/clear/re-add, markers and cones, room visibility/materials, keyboard input/capture, zero validation errors (including shutdown)\n";
    }
    if (backend_ready) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
