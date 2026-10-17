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

void materialControls(Cube& object) {
    ImGui::ColorEdit3("Albedo", glm::value_ptr(object.albedo));
    ImGui::ColorEdit3("Specular color", glm::value_ptr(object.specular));
    ImGui::SliderFloat("Shininess", &object.shininess, 1.0f, 256.0f, "%.1f");
    ImGui::Checkbox("Procedural vertex colors", &object.vertex_colors);
}

void cameraControls() {
    auto& camera = scene.camera;
    int projection = static_cast<int>(camera.projection);
    ImGui::RadioButton("Perspective", &projection, static_cast<int>(Projection::perspective));
    ImGui::SameLine();
    ImGui::RadioButton("Orthographic", &projection, static_cast<int>(Projection::orthographic));
    camera.projection = static_cast<Projection>(projection);
    ImGui::DragFloat3("Position", glm::value_ptr(camera.position), 0.05f);
    ImGui::SliderFloat("Pitch", &camera.rotation_degrees.x, -89.0f, 89.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Yaw", &camera.rotation_degrees.y, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Field of view", &camera.fov_degrees, 15.0f, 110.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Ortho half height", &camera.orthographic_half_height, 0.5f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Move speed", &camera.move_speed, 0.5f, 20.0f);
    if (ImGui::Button("Reset camera")) camera = Camera{};
    ImGui::Spacing();
    ImGui::TextWrapped("Hover over the scene: WASD to move, Q/E down/up, Shift faster. "
                       "Hold the right mouse button to look around. Scroll: field of view.");
}

void lightControls() {
    ImGui::Checkbox("Show light markers", &scene.show_light_markers);
    ImGui::Checkbox("Show spotlight cones", &scene.show_spot_guides);
    if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient color", glm::value_ptr(scene.ambient_color));
        ImGui::SliderFloat("Ambient intensity", &scene.ambient_intensity, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Direction", glm::value_ptr(scene.directional.direction), 0.02f, -1, 1);
        ImGui::ColorEdit3("Light color", glm::value_ptr(scene.directional.color));
        ImGui::DragFloat("Intensity", &scene.directional.intensity, 0.02f, 0, 10, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextWrapped("Direction is the direction of light travel. A zero vector uses world down.");
    }
    ImGui::SeparatorText("Point lights");
    if (ImGui::Button("Add point light")) {
        PointLight light;
        light.position.x += static_cast<float>(scene.point_lights.size()) * 0.5f;
        scene.point_lights.push_back(light);
    }
    ImGui::Text("Count: %zu", scene.point_lights.size());
    for (std::size_t i = 0; i < scene.point_lights.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx("Point", ImGuiTreeNodeFlags_DefaultOpen, "Point %zu", i+1);
        bool remove = false;
        if (open) {
            auto& light = scene.point_lights[i];
            ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f);
            ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.2f, 0, 10000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            remove = ImGui::Button("Remove point");
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove) { scene.point_lights.erase(scene.point_lights.begin() + i); break; }
    }
    ImGui::SeparatorText("Spotlights");
    if (ImGui::Button("Add spotlight")) {
        SpotLight light;
        light.position.x -= static_cast<float>(scene.spot_lights.size()) * 0.5f;
        scene.spot_lights.push_back(light);
    }
    ImGui::Text("Count: %zu", scene.spot_lights.size());
    for (std::size_t i = 0; i < scene.spot_lights.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx("Spot", ImGuiTreeNodeFlags_DefaultOpen, "Spotlight %zu", i+1);
        bool remove = false;
        if (open) {
            auto& light = scene.spot_lights[i];
            ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f);
            ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.02f, -1, 1);
            ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
            ImGui::DragFloat("Intensity", &light.intensity, 0.2f, 0, 10000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Inner half angle", &light.inner_degrees, 0, light.outer_degrees, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Outer half angle", &light.outer_degrees, light.inner_degrees, 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Cone guide length", &light.guide_length, 0.2f, 10.0f);
            remove = ImGui::Button("Remove spotlight");
            ImGui::TreePop();
        }
        ImGui::PopID();
        if (remove) { scene.spot_lights.erase(scene.spot_lights.begin() + i); break; }
    }
}

void cameraInput() {
    const auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse || io.MousePos.x < scene.viewport_left || io.MousePos.y < 0 ||
        io.MousePos.x >= io.DisplaySize.x || io.MousePos.y >= io.DisplaySize.y) return;
    // Navigation focus alone must not disable scene controls. Text input and
    // active widgets still own the keyboard until editing/dragging ends.
    if (io.WantTextInput || ImGui::IsAnyItemActive()) return;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        scene.camera.rotate({-io.MouseDelta.y * 0.15f, io.MouseDelta.x * 0.15f});
    }
    glm::vec3 movement(0);
    movement.x = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_D)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_A));
    movement.y = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_E)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_Q));
    movement.z = static_cast<float>(ImGui::IsKeyDown(ImGuiKey_W)) - static_cast<float>(ImGui::IsKeyDown(ImGuiKey_S));
    if (glm::length(movement) > 0) movement = glm::normalize(movement);
    scene.camera.move(movement * scene.camera.move_speed * std::min(io.DeltaTime, 0.1f)
                      * (io.KeyShift ? 3.0f : 1.0f));
    scene.camera.fov_degrees = std::clamp(scene.camera.fov_degrees - io.MouseWheel * 2, 15.0f, 110.0f);
}

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
    if (!ImGui::Begin("Lab 2 | Camera and lighting", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.52f);

    if (ImGui::BeginTabBar("Scene settings")) {
        if (ImGui::BeginTabItem("Camera")) {
            cameraControls();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Objects")) {
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
            ImGui::SeparatorText("Blinn-Phong material");
            materialControls(cube);

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
            ImGui::SeparatorText("Room");
            ImGui::Checkbox("Show room", &scene.show_room);
            constexpr const char* surface_names[] = {"Floor", "Back wall", "Left wall", "Ceiling"};
            for (std::size_t i = 0; i < scene.room.size(); ++i) {
                if (ImGui::TreeNode(surface_names[i])) {
                    materialControls(scene.room[i]);
                    ImGui::TreePop();
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lights")) {
            lightControls();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
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
        std::cerr << "Lab 2 initialization: " << error.what() << '\n';
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
    cameraInput();
}

void render(const graphics::internal::FrameData& fd) {
    renderer.render(fd, scene);
}

} // namespace application
