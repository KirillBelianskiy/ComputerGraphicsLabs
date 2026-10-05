#include "graphics.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace graphics {

void check(VkResult result, std::string_view operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ")");
    }
}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer&& other) noexcept { *this = std::move(other); }

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, nullptr);
        allocator_ = std::exchange(other.allocator_, nullptr);
        mapped_ = std::exchange(other.mapped_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void Buffer::create(VkDeviceSize size, VkBufferUsageFlags usage) {
    if (buffer_ != VK_NULL_HANDLE || size == 0) {
        throw std::logic_error("Buffer::create requires an empty buffer and nonzero size");
    }
    allocator_ = internal::context.allocator;
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    
    const VmaAllocationCreateInfo allocation = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo allocated{};
    check(vmaCreateBuffer(allocator_, &info, &allocation, &buffer_, &allocation_,
                          &allocated), "vmaCreateBuffer");
    mapped_ = allocated.pMappedData;
    size_ = size;
}

void Buffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (!mapped_ || !data || offset > size_ || size > size_ - offset) {
        throw std::out_of_range("Buffer::write exceeds the mapped allocation");
    }
    std::memcpy(static_cast<std::byte*>(mapped_) + offset, data,
                static_cast<std::size_t>(size));
    // Required for non-coherent heaps; a no-op for coherent memory.
    check(vmaFlushAllocation(allocator_, allocation_, offset, size),
          "vmaFlushAllocation");
}

void Buffer::destroy() noexcept {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
    buffer_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    allocator_ = nullptr;
    mapped_ = nullptr;
    size_ = 0;
}

ShaderModule::ShaderModule(const std::filesystem::path& path)
    : device_(internal::context.device) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open SPIR-V shader: " + path.string());
    }
    const auto length = file.tellg();
    if (length < 20 || length % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(length) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), length) ||
        words.front() != 0x07230203) {
        throw std::runtime_error("Invalid SPIR-V contents: " + path.string());
    }
    const VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = words.size() * sizeof(std::uint32_t),
        .pCode = words.data(),
    };
    check(vkCreateShaderModule(device_, &info, nullptr, &module_),
          "vkCreateShaderModule");
}

ShaderModule::~ShaderModule() {
    vkDestroyShaderModule(device_, module_, nullptr);
}

} // namespace graphics
