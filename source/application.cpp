#include "application.hpp"

#include "cube_renderer.hpp"
#include "scene.hpp"

#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <exception>
#include <iostream>

namespace application {
namespace {
Scene scene;
CubeRenderer renderer;

void controls() {
    const auto display = ImGui::GetIO().DisplaySize;
    const float panel_width = std::min(360.0f, display.x * 0.45f);
    scene.viewport_left = panel_width + 32.0f;
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_width, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(panel_width, 0),
        ImVec2(panel_width, std::max(100.0f, display.y - 32.0f)));
    const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("Lab 1 | Two cubes", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.52f);

    ImGui::SeparatorText("Projection");
    int projection = static_cast<int>(scene.camera.projection);
    ImGui::RadioButton("Perspective", &projection, static_cast<int>(Projection::perspective));
    ImGui::SameLine();
    ImGui::RadioButton("Orthographic", &projection, static_cast<int>(Projection::orthographic));
    scene.camera.projection = static_cast<Projection>(projection);

    ImGui::SeparatorText("Cube");
    ImGui::RadioButton("Cube 1", &scene.selected_cube, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Cube 2", &scene.selected_cube, 1);
    auto& cube = scene.cubes[static_cast<std::size_t>(scene.selected_cube)];
    ImGui::DragFloat3("Position XYZ", glm::value_ptr(cube.transform.position),
                      0.05f, -10.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Circle center, or base position for the complex path.");
    }
    ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(cube.transform.rotation_degrees),
                      0.5f, -180.0f, 180.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat3("Scale XYZ", glm::value_ptr(cube.transform.scale),
                      0.01f, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Color", glm::value_ptr(cube.tint));

    ImGui::SeparatorText("Animation");
    const bool playing = std::any_of(scene.cubes.begin(), scene.cubes.end(),
        [](const Cube& object) { return object.animation.playing; });
    if (ImGui::Button(playing ? "Pause both" : "Play both",
                      ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
        scene.setAnimationsPlaying(!playing);
    }
    const float playback_button_width =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    for (std::size_t i = 0; i < scene.cubes.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        auto& playback = scene.cubes[i].animation;
        const char* label = i == 0
            ? (playback.playing ? "Pause cube 1" : "Play cube 1")
            : (playback.playing ? "Pause cube 2" : "Play cube 2");
        if (ImGui::Button(label, ImVec2(playback_button_width, 30))) {
            playback.playing = !playback.playing;
        }
    }
    auto& animation = cube.animation;
    ImGui::SliderFloat("Speed", &animation.speed, 0.0f, 3.0f, "%.2fx");
    int trajectory = static_cast<int>(animation.trajectory);
    ImGui::RadioButton("Circular", &trajectory, static_cast<int>(Trajectory::circular));
    ImGui::SameLine();
    ImGui::RadioButton("Complex 3D", &trajectory, static_cast<int>(Trajectory::complex));
    animation.trajectory = static_cast<Trajectory>(trajectory);
    if (animation.trajectory == Trajectory::circular) {
        ImGui::SliderFloat("Radius", &animation.orbit_radius, 0.0f, 2.5f, "%.2f");
    } else {
        ImGui::SliderFloat("Path size", &animation.path_scale, 0.0f, 2.5f, "%.2f");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Zero stops movement along the path while preserving spin.");
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

} // namespace

Scene& currentScene() { return scene; }

bool initialize() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(6, 5);
    style.ItemSpacing = ImVec2(8, 7);
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.10f, 0.15f, 0.98f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.24f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.32f, 0.43f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.3f, 0.8f, 0.9f, 1.0f);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigDragClickToInputText = true;
    ImGui::GetIO().IniFilename = nullptr;
    try {
        renderer.initialize();
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Lab 1 initialization: " << error.what() << '\n';
        shutdown(); // release resources from a partially completed initialization
        return false;
    }
}

void shutdown() {
    vkDeviceWaitIdle(graphics::internal::context.device);
    renderer.shutdown();
}

void update(double time) {
    scene.update(time);
    controls();
}

void render(const graphics::internal::FrameData& fd) {
    renderer.render(fd, scene);
}

} // namespace application
