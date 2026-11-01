#pragma once

#include "graphics_internal.hpp"
#include "image.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>

namespace graphics {

// These resources belong to the application; the starter owns the context.
void check(VkResult result, std::string_view operation);

enum class MemoryUsage { host, device };

class Buffer {
public:
    Buffer() = default;
    ~Buffer();
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    void create(VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory = MemoryUsage::host);
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    void destroy() noexcept;
    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
    [[nodiscard]] VkMemoryPropertyFlags memoryProperties() const noexcept { return memory_properties_; }
    [[nodiscard]] bool mapped() const noexcept { return mapped_ != nullptr; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    void* mapped_ = nullptr;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags memory_properties_ = 0;
};

class UploadContext {
public:
    UploadContext() = default;
    ~UploadContext();
    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;
    void initialize();
    void submit(const std::function<void(VkCommandBuffer)>& record);
    void destroy() noexcept;
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

void copyBuffer(VkCommandBuffer command, const Buffer& source, const Buffer& destination,
                VkDeviceSize bytes, VkAccessFlags destination_access);
void uploadBuffer(UploadContext& upload, Buffer& destination, const void* data,
                  VkDeviceSize bytes, VkBufferUsageFlags usage);

class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    void create(const ImagePixels& pixels, bool srgb, UploadContext& upload,
                VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    void destroy() noexcept;
    [[nodiscard]] VkDescriptorImageInfo descriptor() const noexcept;
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
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
