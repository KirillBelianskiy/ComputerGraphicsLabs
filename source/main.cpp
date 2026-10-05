#include <cstdint>
#include <climits>
#include <cstdlib>

#include <iostream>
#include <exception>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

constexpr int32_t default_window_width = 1280;
constexpr int32_t default_window_height = 720;

constexpr char default_window_title[] = "Lab 1 - Variant 1: Cube (Vulkan)";

GLFWwindow* glfw_window;

} // namespace

int main() {
	int status = EXIT_SUCCESS;
    bool graphics_initialized = false;

	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfw_window = glfwCreateWindow(default_window_width, default_window_height,
	                               default_window_title, nullptr, nullptr);
	if (glfw_window == nullptr) {
		status = EXIT_FAILURE;
		goto err_null_window;
	}

	glfwSetFramebufferSizeCallback(glfw_window, [](GLFWwindow*, int width, int height){
		if (width == 0 || height == 0) {
			return;
		}

		graphics::internal::resize(width, height);
	});

	if (ImGui::CreateContext() == nullptr) {
		std::cerr << "Failed to create ImGUI context\n";
		status = EXIT_FAILURE;
		goto err_imgui_init;
	}

	if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
		std::cerr << "Failed to initialize ImGUI GLFW backend for Vulkan renderer\n";
		status = EXIT_FAILURE;
		goto err_imgui_glfw_init;
	}

    try {
        graphics_initialized = graphics::internal::initialize(glfw_window);
    } catch (const std::exception& error) {
        std::cerr << "Graphics initialization: " << error.what() << '\n';
    }
	if (!graphics_initialized) {
		std::cerr << "Failed to initialize graphics\n";
		status = EXIT_FAILURE;
        graphics::internal::shutdown();
		goto err_graphics_init;
	}

	if (!application::initialize()) {
		std::cerr << "Failed to initialize application\n";
		status = EXIT_FAILURE;
		goto err_application_init;
	}

    try {
        while (!glfwWindowShouldClose(glfw_window)) {
            glfwPollEvents();
            int width = 0, height = 0;
            glfwGetFramebufferSize(glfw_window, &width, &height);
            if (width == 0 || height == 0) {
                // A minimized window has no drawable swapchain extent.
                glfwWaitEventsTimeout(0.05);
                continue;
            }
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            application::update(glfwGetTime());
            ImGui::Render();

            const auto fd = graphics::internal::prepare();
            application::render(fd);
            graphics::internal::submitAndPresent();
        }
    } catch (const std::exception& error) {
        std::cerr << "Rendering stopped: " << error.what() << '\n';
        status = EXIT_FAILURE;
    }

	application::shutdown();
err_application_init:
	graphics::internal::shutdown();
err_graphics_init:
	ImGui_ImplGlfw_Shutdown();
err_imgui_glfw_init:
	ImGui::DestroyContext();
err_imgui_init:
	glfwDestroyWindow(glfw_window);
err_null_window:
	glfwTerminate();

	return status;
}
