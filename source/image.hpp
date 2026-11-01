#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace graphics {
struct ImagePixels {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

ImagePixels decodeImage(const std::filesystem::path& path);
} // namespace graphics
