#include "image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace graphics {
ImagePixels decodeImage(const std::filesystem::path& path) {
    const auto fail = [&](const std::string& reason) {
        return std::runtime_error("Cannot decode image '" + path.string() + "': " + reason);
    };
    if (!std::filesystem::is_regular_file(path)) throw fail("file does not exist or is not a regular file");
    int width = 0, height = 0, channels = 0;
    if (!stbi_info(path.string().c_str(), &width, &height, &channels))
        throw fail(stbi_failure_reason() ? stbi_failure_reason() : "unsupported or corrupt image");
    if (width <= 0 || height <= 0 || width > STBI_MAX_DIMENSIONS || height > STBI_MAX_DIMENSIONS)
        throw fail("dimensions must be in [1, 8192]");
    if (stbi_is_hdr(path.string().c_str())) throw fail("HDR images are not supported; use an 8-bit PNG or JPEG");
    const auto bytes = std::uint64_t(width) * std::uint64_t(height) * 4;
    if (bytes > 256ull * 1024 * 1024) throw fail("decoded image exceeds 256 MiB");
    // Keep the original top-to-bottom row order; UV generation uses the same convention.
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels) throw fail(stbi_failure_reason() ? stbi_failure_reason() : "decoding failed");
    ImagePixels result{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), {}};
    const auto decoded_bytes = std::uint64_t(result.width) * result.height * 4;
    if (decoded_bytes > 256ull * 1024 * 1024) throw fail("decoded image exceeds 256 MiB");
    result.rgba.assign(pixels.get(), pixels.get() + decoded_bytes);
    return result;
}
} // namespace graphics
