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
    auto* window = glfwCreateWindow(1280, 720, "Lab 1 | Runtime verification", nullptr, nullptr);
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
                scene.cubes[1].tint = {1.0f, 0.7f, 0.85f};
            }
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            application::update(frame / 60.0);
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
                     "circle radius, complex path size, individual pause/resume, per-cube edits, zero validation errors (including shutdown)\n";
    }
    if (backend_ready) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}
