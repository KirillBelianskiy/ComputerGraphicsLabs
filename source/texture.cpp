#include "graphics.hpp"

#include <stdexcept>
#include <algorithm>
#include <utility>

namespace graphics {
UploadContext::~UploadContext() { destroy(); }
void UploadContext::initialize() {
    if (pool_) throw std::logic_error("UploadContext already initialized");
    device_ = internal::context.device;
    try {
        const VkCommandPoolCreateInfo pool = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = internal::context.graphics_queue_index,
        };
        check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "Create upload command pool");
        const VkCommandBufferAllocateInfo allocate = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool_, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
        };
        check(vkAllocateCommandBuffers(device_, &allocate, &command_), "Allocate upload command buffer");
        const VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device_, &fence, nullptr, &fence_), "Create upload fence");
    } catch (...) { destroy(); throw; }
}
void UploadContext::submit(const std::function<void(VkCommandBuffer)>& record) {
    check(vkResetCommandPool(device_, pool_, 0), "Reset upload command pool");
    check(vkResetFences(device_, 1, &fence_), "Reset upload fence");
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    check(vkBeginCommandBuffer(command_, &begin), "Begin upload commands");
    record(command_);
    check(vkEndCommandBuffer(command_), "End upload commands");
    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command_,
    };
    check(vkQueueSubmit(internal::context.graphics_queue, 1, &submit, fence_), "Submit upload");
    // Staging resources owned by the caller must outlive this completion point.
    check(vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX), "Wait for upload");
}
void UploadContext::destroy() noexcept {
    if (device_) {
        vkDestroyFence(device_, fence_, nullptr);
        vkDestroyCommandPool(device_, pool_, nullptr);
    }
    fence_ = VK_NULL_HANDLE; pool_ = VK_NULL_HANDLE; command_ = VK_NULL_HANDLE; device_ = VK_NULL_HANDLE;
}

void copyBuffer(VkCommandBuffer command, const Buffer& source, const Buffer& destination,
                VkDeviceSize bytes, VkAccessFlags destination_access) {
    const VkBufferCopy copy{0, 0, bytes};
    vkCmdCopyBuffer(command, source.handle(), destination.handle(), 1, &copy);
    const VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = destination_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = destination.handle(), .offset = 0, .size = bytes,
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}
void uploadBuffer(UploadContext& upload, Buffer& destination, const void* data,
                  VkDeviceSize bytes, VkBufferUsageFlags usage) {
    Buffer staging;
    staging.create(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staging.write(data, bytes);
    destination.create(bytes, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::device);
    const auto access = usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        ? VK_ACCESS_INDEX_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    upload.submit([&](VkCommandBuffer command) { copyBuffer(command, staging, destination, bytes, access); });
}

Texture::~Texture() { destroy(); }
Texture::Texture(Texture&& other) noexcept { *this = std::move(other); }
Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, nullptr);
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, nullptr);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
        sampler_ = std::exchange(other.sampler_, VK_NULL_HANDLE);
    }
    return *this;
}
void Texture::create(const ImagePixels& pixels, bool srgb, UploadContext& upload,
                     VkSamplerAddressMode address) {
    if (image_ || pixels.width == 0 || pixels.height == 0 ||
        pixels.rgba.size() != std::uint64_t(pixels.width) * pixels.height * 4)
        throw std::invalid_argument("Texture requires an empty resource and valid RGBA8 pixels");
    const auto& context = internal::context;
    device_ = context.device; allocator_ = context.allocator;
    const auto format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
    constexpr VkFormatFeatureFlags mip_features = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool can_generate_mips = (properties.optimalTilingFeatures & mip_features) == mip_features;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        (can_generate_mips ? VkImageUsageFlags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT) : 0u);
    VkImageFormatProperties supported{};
    check(vkGetPhysicalDeviceImageFormatProperties(context.physical_device, format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported), "Check texture format support");
    if (pixels.width > supported.maxExtent.width || pixels.height > supported.maxExtent.height)
        throw std::runtime_error("Texture dimensions exceed device limits");
    std::uint32_t mip_levels = 1;
    if (can_generate_mips)
        for (auto size = std::max(pixels.width, pixels.height); size > 1; size /= 2) ++mip_levels;
    mip_levels = std::min(mip_levels, supported.maxMipLevels);
    const auto filter = properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
        ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    try {
        const VkImageCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = format,
            .extent = {pixels.width, pixels.height, 1}, .mipLevels = mip_levels, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocation = {
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };
        check(vmaCreateImage(allocator_, &info, &allocation, &image_, &allocation_, nullptr), "Create texture image");
        Buffer staging;
        staging.create(pixels.rgba.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        staging.write(pixels.rgba.data(), pixels.rgba.size());
        upload.submit([&](VkCommandBuffer command) {
            VkImageMemoryBarrier barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image_, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1},
            };
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
            const VkBufferImageCopy region = {
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageExtent = {pixels.width, pixels.height, 1},
            };
            vkCmdCopyBufferToImage(command, staging.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            auto width = static_cast<std::int32_t>(pixels.width);
            auto height = static_cast<std::int32_t>(pixels.height);
            barrier.subresourceRange.levelCount = 1;
            for (std::uint32_t level = 1; level < mip_levels; ++level) {
                barrier.subresourceRange.baseMipLevel = level - 1;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
                const auto next_width = std::max(1, width / 2), next_height = std::max(1, height / 2);
                const VkImageBlit blit = {
                    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1},
                    .srcOffsets = {{0, 0, 0}, {width, height, 1}},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                    .dstOffsets = {{0, 0, 0}, {next_width, next_height, 1}},
                };
                vkCmdBlitImage(command, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
                width = next_width; height = next_height;
            }
            barrier.subresourceRange.baseMipLevel = mip_levels - 1;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });
        const VkImageViewCreateInfo view = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image_, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_levels, 0, 1},
        };
        check(vkCreateImageView(device_, &view, nullptr, &view_), "Create texture view");
        const VkSamplerCreateInfo sampler = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = filter, .minFilter = filter, .mipmapMode = can_generate_mips ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = address, .addressModeV = address, .addressModeW = address,
            .maxAnisotropy = 1.0f, .minLod = 0, .maxLod = static_cast<float>(mip_levels - 1),
        };
        check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Create texture sampler");
    } catch (...) { destroy(); throw; }
}
VkDescriptorImageInfo Texture::descriptor() const noexcept {
    return {sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
void Texture::destroy() noexcept {
    if (device_) {
        vkDestroySampler(device_, sampler_, nullptr);
        vkDestroyImageView(device_, view_, nullptr);
        if (image_) vmaDestroyImage(allocator_, image_, allocation_);
    }
    image_ = VK_NULL_HANDLE; allocation_ = nullptr; view_ = VK_NULL_HANDLE; sampler_ = VK_NULL_HANDLE;
    allocator_ = nullptr; device_ = VK_NULL_HANDLE;
}
} // namespace graphics
