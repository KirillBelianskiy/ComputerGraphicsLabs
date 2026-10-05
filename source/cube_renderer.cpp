#include "cube_renderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace application {

static_assert(std::is_standard_layout_v<Vertex>);
static_assert(sizeof(glm::mat4) == 64);
static_assert(offsetof(UniformData, model) == 0);
static_assert(offsetof(UniformData, view) == 64);
static_assert(offsetof(UniformData, projection) == 128);
static_assert(offsetof(UniformData, tint) == 192);
static_assert(sizeof(UniformData) == 208);
static_assert(alignof(UniformData) == 16);

void CubeRenderer::initialize() {
    const auto vertices = cubeVertices();
    vertex_buffer_.create(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vertex_buffer_.write(vertices.data(), sizeof(vertices));
    index_buffer_.create(sizeof(cube_indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    index_buffer_.write(cube_indices.data(), sizeof(cube_indices));
    createDescriptors();
    createPipeline();
}

void CubeRenderer::createDescriptors() {
    const auto device = graphics::internal::context.device;
    const VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &descriptor_layout_),
                    "vkCreateDescriptorSetLayout");
    const VkDescriptorPoolSize size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = static_cast<std::uint32_t>(cube_count),
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = static_cast<std::uint32_t>(cube_count),
        .poolSizeCount = 1,
        .pPoolSizes = &size,
    };
    graphics::check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool_),
                    "vkCreateDescriptorPool");
    std::array<VkDescriptorSetLayout, cube_count> layouts{};
    layouts.fill(descriptor_layout_);
    std::array<VkDescriptorSet, cube_count> sets{};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = static_cast<std::uint32_t>(cube_count),
        .pSetLayouts = layouts.data(),
    };
    graphics::check(vkAllocateDescriptorSets(device, &allocate, sets.data()),
                    "vkAllocateDescriptorSets");
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        auto& object = objects_[i];
        object.uniform_buffer.create(sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        object.descriptor_set = sets[i];
        const VkDescriptorBufferInfo buffer = {
            .buffer = object.uniform_buffer.handle(),
            .offset = 0,
            .range = sizeof(UniformData),
        };
        const VkWriteDescriptorSet write = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = object.descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &buffer,
        };
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void CubeRenderer::createPipeline() {
    const auto& context = graphics::internal::context;
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_layout_,
    };
    graphics::check(vkCreatePipelineLayout(context.device, &layout, nullptr,
                                           &pipeline_layout_), "vkCreatePipelineLayout");
    const graphics::ShaderModule vertex_shader(
        std::filesystem::path(LAB1_SHADER_DIRECTORY) / "lab1.vert.spv");
    const graphics::ShaderModule fragment_shader(
        std::filesystem::path(LAB1_SHADER_DIRECTORY) / "lab1.frag.spv");
    const VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader.handle(), .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader.handle(), .pName = "main"},
    };
    const VkVertexInputBindingDescription vertex_binding = {
        .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attributes,
    };
    const VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        // Outward cube indices, RH projection with Y flip, positive viewport height.
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    const VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
    };
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states,
    };
    const VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport, .pRasterizationState = &rasterization,
        .pMultisampleState = &multisampling, .pDepthStencilState = &depth,
        .pColorBlendState = &blending, .pDynamicState = &dynamic,
        .layout = pipeline_layout_, .renderPass = context.render_pass, .subpass = 0,
    };
    graphics::check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
                                              &pipeline, nullptr, &pipeline_),
                    "vkCreateGraphicsPipelines");
    // Modules are released by their destructors. The pipeline keeps compiled code.
}

void CubeRenderer::render(const graphics::internal::FrameData& frame, const Scene& scene) {
    const auto& context = graphics::internal::context;
    const auto extent = context.swapchain_extent;
    const auto& io = ImGui::GetIO();
    const float pixel_scale = io.DisplaySize.x > 0.0f
        ? static_cast<float>(extent.width) / io.DisplaySize.x : 1.0f;
    const auto left = static_cast<std::uint32_t>(std::clamp(
        scene.viewport_left * pixel_scale, 0.0f, static_cast<float>(extent.width - 1)));
    const auto scene_width = extent.width - left;
    const float aspect = static_cast<float>(scene_width) / static_cast<float>(extent.height);
    const auto view = scene.camera.view();
    const auto projection = scene.camera.projectionMatrix(aspect);
    // prepare() waited for the starter's single frame fence. Only now may the
    // CPU overwrite the UBOs and reuse this command buffer safely.
    for (std::size_t i = 0; i < objects_.size(); ++i) {
        const auto& cube = scene.cubes[i];
        const UniformData uniform = {cube.model(), view, projection, glm::vec4(cube.tint, 1.0f)};
        objects_[i].uniform_buffer.write(&uniform, sizeof(uniform));
    }
    const auto command = frame.command_buffer;
    graphics::check(vkResetCommandBuffer(command, 0), "vkResetCommandBuffer");
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    graphics::check(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    const VkClearValue clear[] = {
        {.color = {{0.025f, 0.035f, 0.06f, 1.0f}}},
        {.depthStencil = {1.0f, 0}},
    };
    const VkRenderPassBeginInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass, .framebuffer = frame.framebuffer,
        .renderArea = {.extent = extent}, .clearValueCount = 2, .pClearValues = clear,
    };
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkViewport viewport = {static_cast<float>(left), 0.0f, static_cast<float>(scene_width),
                                  static_cast<float>(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor = {
        .offset = {static_cast<std::int32_t>(left), 0},
        .extent = {scene_width, extent.height},
    };
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    const VkBuffer vertex_buffer = vertex_buffer_.handle();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(command, index_buffer_.handle(), 0, VK_INDEX_TYPE_UINT16);
    for (const auto& object : objects_) {
        // Every draw binds a different descriptor set with its own UBO.
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeline_layout_, 0, 1, &object.descriptor_set, 0, nullptr);
        vkCmdDrawIndexed(command, static_cast<std::uint32_t>(cube_indices.size()), 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(command);
    graphics::check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
}

void CubeRenderer::shutdown() noexcept {
    const auto device = graphics::internal::context.device;
    // application::shutdown waits for the GPU before entering here.
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool_, nullptr); // also frees all sets
    vkDestroyDescriptorSetLayout(device, descriptor_layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    descriptor_layout_ = VK_NULL_HANDLE;
    for (auto& object : objects_) {
        object.descriptor_set = VK_NULL_HANDLE;
        object.uniform_buffer.destroy();
    }
    index_buffer_.destroy();
    vertex_buffer_.destroy();
}

} // namespace application
