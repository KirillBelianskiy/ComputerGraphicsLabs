#pragma once

#include "graphics.hpp"
#include "scene.hpp"

namespace application {

class CubeRenderer {
public:
    void initialize();
    void shutdown() noexcept;
    void render(const graphics::internal::FrameData& frame, const Scene& scene);

private:
    void createMaterials();
    void createDescriptors();
    void createPipeline();
    void updateBuffers(const Scene& scene, float aspect);
    // Called only after prepare() has waited for the previous frame's fence.
    bool ensureCapacity(graphics::Buffer& buffer, VkDeviceSize& capacity,
                        VkDeviceSize required, VkBufferUsageFlags usage,
                        graphics::MemoryUsage memory = graphics::MemoryUsage::host);
    void bindBuffer(VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type,
                    const graphics::Buffer& buffer, VkDeviceSize range);

    struct DrawState {
        std::uint32_t material_id = 0;
        EffectPushConstants effect{};
    };
    graphics::UploadContext upload_;
    std::vector<graphics::Texture> textures_;
    std::array<std::array<std::size_t, 3>, materials.size()> material_textures_{};
    std::array<VkDescriptorSet, materials.size()> material_sets_{};
    std::vector<DrawState> draw_states_;
    graphics::Buffer guide_staging_buffer_;
    VkDeviceSize guide_staging_capacity_ = 0, guide_bytes_ = 0;
    graphics::Buffer vertex_buffer_;
    graphics::Buffer index_buffer_;
    graphics::Buffer scene_buffer_;
    graphics::Buffer model_buffer_;
    graphics::Buffer point_buffer_;
    graphics::Buffer spot_buffer_;
    graphics::Buffer guide_buffer_;
    VkDeviceSize model_capacity_ = 0, point_capacity_ = 0, spot_capacity_ = 0;
    VkDeviceSize guide_capacity_ = 0, model_stride_ = 0;
    std::uint32_t model_count_ = 0, guide_vertex_count_ = 0;
    VkDescriptorSet scene_set_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout scene_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout model_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline line_pipeline_ = VK_NULL_HANDLE;
};

} // namespace application
