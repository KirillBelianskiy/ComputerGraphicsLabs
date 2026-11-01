#include "image.hpp"
#include "scene.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void throws(Function action, const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void geometry() {
    const auto vertices = application::cubeVertices();
    for (std::size_t face = 0; face < 6; ++face) {
        int corners[4]{};
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& v = vertices[face * 4 + i];
            require(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1, "Cube UV must be normalized");
            ++corners[int(v.uv.x) + 2 * int(v.uv.y)];
        }
        for (int count : corners) require(count == 1, "Each face must contain four distinct UV corners");
        const auto& a = vertices[face * 4];
        const auto& b = vertices[face * 4 + 1];
        const auto& c = vertices[face * 4 + 2];
        const auto u = b.uv - a.uv, v = c.uv - a.uv;
        const float determinant = u.x * v.y - u.y * v.x;
        require(determinant < 0, "UV must preserve the top-to-bottom image convention without mirrored faces");
        const auto tangent = ((b.position-a.position)*v.y - (c.position-a.position)*u.y) / determinant;
        const auto bitangent = ((c.position-a.position)*u.x - (b.position-a.position)*v.x) / determinant;
        require(glm::dot(glm::cross(tangent, -bitangent), a.normal) > 0.99f,
                "Image right and up must form an outward-facing basis");
    }
    for (const auto& vertex : application::spotlightGuide(application::SpotLight{}))
        require(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y), "Guides must initialize UV");
}
void uniforms() {
    for (std::uint64_t alignment : {1, 16, 64, 256, 512}) {
        const auto stride = application::alignedUniformStride(sizeof(application::ModelUniformData), alignment);
        require(stride >= sizeof(application::ModelUniformData) && stride % alignment == 0, "Invalid dynamic UBO stride");
        for (std::uint64_t i = 0; i < 100; ++i)
            require(i * stride + sizeof(application::ModelUniformData) <= 100 * stride, "UBO range exceeds allocation");
    }
    throws([] { application::alignedUniformStride(192, 0); }, "Zero alignment must fail");
    throws([] { application::alignedUniformStride(std::numeric_limits<std::uint64_t>::max(), 256); }, "Alignment overflow must fail");
}
void effects() {
    application::TextureEffect effect;
    effect.enabled = true;
    effect.advance(2.0);
    effect.playing = false;
    effect.advance(100.0);
    require(effect.time == 2.0, "Pause must freeze effect time");
    effect.playing = true; effect.speed = 2;
    effect.advance(0.5);
    require(effect.time == 3.0, "Effect speed must integrate without a phase jump");
    effect.advance(std::numeric_limits<double>::quiet_NaN());
    effect.advance(-1);
    require(effect.time == 3.0, "Invalid time must not corrupt effect phase");
    require(application::effectPushConstants(effect, true).control.y == 1, "Push constants must enable textured effects");
    require(application::effectPushConstants(effect, false).control.y == 0, "Legacy rendering must disable effects");
    effect.enabled = false;
    require(application::effectPushConstants(effect, true).control.y == 0, "Disable must reach push constants");
    application::Scene scene;
    scene.setAnimationsPlaying(false);
    scene.update(10); scene.update(11);
    require(scene.cubes[1].texture_effect.time == 1, "Sampling animation must be independent of cube playback");
}
void images() {
    const auto root = std::filesystem::path(SCENE_ASSET_DIRECTORY);
    for (const auto& material : application::materials) {
        for (const auto* path : {material.albedo_path, material.specular_path, material.emissive_path}) {
            if (!path[0]) continue;
            const auto pixels = graphics::decodeImage(root / path);
            require(pixels.width > 0 && pixels.height > 0 && pixels.rgba.size() == std::uint64_t(pixels.width)*pixels.height*4,
                    "Image must decode into complete RGBA8 pixels");
            for (std::size_t i = 3; i < pixels.rgba.size(); i += 4)
                require(pixels.rgba[i] == 255, "RGB and grayscale PNGs must decode as opaque RGBA");
        }
    }
    const auto specular = graphics::decodeImage(root / application::materials[1].specular_path);
    const auto emissive = graphics::decodeImage(root / application::materials[1].emissive_path);
    require(specular.width == emissive.width && specular.height == emissive.height, "Panel map dimensions must match");
    for (std::size_t i = 0; i < specular.rgba.size(); i += 4)
        require(specular.rgba[i] == specular.rgba[i+1] && specular.rgba[i] == specular.rgba[i+2], "Specular mask must remain grayscale");
    throws([&] { graphics::decodeImage(root / "missing.png"); }, "Missing file must fail");
    const auto temporary = std::filesystem::temp_directory_path() / "lab3-corrupt-image-test.png";
    { std::ofstream file(temporary); file << "Not a valid PNG"; }
    try { throws([&] { graphics::decodeImage(temporary); }, "Corrupt file must fail"); }
    catch (...) { std::filesystem::remove(temporary); throw; }
    std::filesystem::remove(temporary);
}
}
int main() {
    try {
        geometry(); uniforms(); effects(); images();
        std::cout << "PASS: UV orientation, guide vertices, UBO alignment, effect playback, image decoding and errors\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
