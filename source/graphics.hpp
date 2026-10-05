#pragma once

#include "graphics_internal.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace graphics {

// These resources belong to the application; the starter owns the context.
void check(VkResult result, std::string_view operation);

class Buffer {
public:
    Buffer() = default;
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    void create(VkDeviceSize size, VkBufferUsageFlags usage);
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void destroy() noexcept;
    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    void* mapped_ = nullptr;
    VkDeviceSize size_ = 0;
};

class ShaderModule {
public:
    explicit ShaderModule(const std::filesystem::path& path);
    ~ShaderModule();
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    [[nodiscard]] VkShaderModule handle() const noexcept { return module_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

} // namespace graphics
