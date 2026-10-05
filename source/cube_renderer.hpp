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
    struct ObjectResources {
        graphics::Buffer uniform_buffer;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };

    void createDescriptors();
    void createPipeline();

    graphics::Buffer vertex_buffer_;
    graphics::Buffer index_buffer_;
    std::array<ObjectResources, cube_count> objects_{};
    VkDescriptorSetLayout descriptor_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace application
