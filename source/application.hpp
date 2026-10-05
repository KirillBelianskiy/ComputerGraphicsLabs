#pragma once

#include "graphics_internal.hpp"

namespace application {

struct Scene;
// The same model is edited by ImGui and exercised by the runtime smoke test.
Scene& currentScene();

bool initialize();
void shutdown();

void update(double time);
void render(const graphics::internal::FrameData& fd);

} // namespace application
