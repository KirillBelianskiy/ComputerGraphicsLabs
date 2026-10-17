#include "cube_renderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace application {

void CubeRenderer::initialize() {
    const auto vertices = cubeVertices();
    vertex_buffer_.create(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    vertex_buffer_.write(vertices.data(), sizeof(vertices));
    index_buffer_.create(sizeof(cube_indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    index_buffer_.write(cube_indices.data(), sizeof(cube_indices));
    createDescriptors();
    createPipeline();
}

void CubeRenderer::bindBuffer(VkDescriptorSet set, std::uint32_t binding,
                              VkDescriptorType type, const graphics::Buffer& buffer,
                              VkDeviceSize range) {
    const VkDescriptorBufferInfo info = {buffer.handle(), 0, range};
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set, .dstBinding = binding, .descriptorCount = 1,
        .descriptorType = type, .pBufferInfo = &info,
    };
    vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

bool CubeRenderer::ensureCapacity(graphics::Buffer& buffer, VkDeviceSize& capacity,
                                  VkDeviceSize required, VkBufferUsageFlags usage) {
    if (required <= capacity) return false;
    graphics::Buffer replacement;
    const auto new_capacity = std::max(required, capacity * 2);
    replacement.create(new_capacity, usage);
    buffer = std::move(replacement);
    capacity = new_capacity;
    return true;
}

void CubeRenderer::createDescriptors() {
    const auto device = graphics::internal::context.device;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
    const auto alignment = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    model_stride_ = (sizeof(ModelUniformData) + alignment - 1) / alignment * alignment;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3, .pBindings = bindings,
    };
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &scene_layout_),
                    "Scene descriptor layout");
    const VkDescriptorSetLayoutBinding model_binding = {
        0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr,
    };
    layout.bindingCount = 1;
    layout.pBindings = &model_binding;
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &model_layout_),
                    "Model descriptor layout");
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2, .poolSizeCount = 3, .pPoolSizes = sizes,
    };
    graphics::check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool_),
                    "vkCreateDescriptorPool");
    const VkDescriptorSetLayout layouts[] = {scene_layout_, model_layout_};
    VkDescriptorSet sets[2]{};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_, .descriptorSetCount = 2, .pSetLayouts = layouts,
    };
    graphics::check(vkAllocateDescriptorSets(device, &allocate, sets), "vkAllocateDescriptorSets");
    scene_set_ = sets[0]; model_set_ = sets[1];
    scene_buffer_.create(sizeof(SceneUniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    bindBuffer(scene_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_buffer_, sizeof(SceneUniformData));
}

void CubeRenderer::updateBuffers(const Scene& scene, float aspect) {
    const SceneUniformData scene_data = {
        scene.camera.projectionMatrix(aspect) * scene.camera.view(),
        glm::vec4(scene.camera.position, 1),
        glm::vec4(scene.ambient_color, scene.ambient_intensity),
        glm::vec4(safeDirection(scene.directional.direction), scene.directional.intensity),
        glm::vec4(scene.directional.color, 1),
        glm::uvec4(scene.point_lights.size(), scene.spot_lights.size(), 0, 0),
    };
    scene_buffer_.write(&scene_data, sizeof(scene_data));
    std::vector<PointLightData> points;
    for (const auto& light : scene.point_lights)
        points.push_back({glm::vec4(light.position, 1), glm::vec4(light.color, light.intensity)});
    std::vector<SpotLightData> spots;
    for (const auto& light : scene.spot_lights) {
        const float outer = std::clamp(light.outer_degrees, 0.0f, 89.0f);
        const float inner = std::clamp(light.inner_degrees, 0.0f, outer);
        spots.push_back({glm::vec4(light.position, 1),
            glm::vec4(safeDirection(light.direction), std::cos(glm::radians(inner))),
            glm::vec4(light.color, light.intensity), glm::vec4(std::cos(glm::radians(outer)), 0, 0, 0)});
    }
    // Vulkan descriptors need a nonzero range even for an empty logical array.
    if (points.empty()) points.push_back({});
    if (spots.empty()) spots.push_back({});
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
    const auto point_bytes = points.size() * sizeof(PointLightData);
    const auto spot_bytes = spots.size() * sizeof(SpotLightData);
    if (point_bytes > properties.limits.maxStorageBufferRange ||
        spot_bytes > properties.limits.maxStorageBufferRange)
        throw std::runtime_error("Light arrays exceed the device storage buffer range");
    ensureCapacity(point_buffer_, point_capacity_, point_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ensureCapacity(spot_buffer_, spot_capacity_, spot_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    point_buffer_.write(points.data(), point_bytes);
    spot_buffer_.write(spots.data(), spot_bytes);
    bindBuffer(scene_set_, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, point_buffer_, point_bytes);
    bindBuffer(scene_set_, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spot_buffer_, spot_bytes);

    std::vector<ModelUniformData> models;
    const auto addObject = [&](const Cube& cube) {
        const auto model = cube.model();
        models.push_back({model, glm::transpose(glm::inverse(model)),
            glm::vec4(cube.albedo, cube.vertex_colors ? 1.0f : 0.0f),
            glm::vec4(cube.specular, cube.shininess), glm::vec4(0)});
    };
    for (const auto& cube : scene.cubes) addObject(cube);
    if (scene.show_room)
        for (const auto& surface : scene.room) addObject(surface);
    const auto marker = [&](glm::vec3 position, glm::vec3 color) {
        const auto model = glm::scale(glm::translate(glm::mat4(1), position), glm::vec3(0.12f));
        models.push_back({model, glm::mat4(1), glm::vec4(1), glm::vec4(0), glm::vec4(color, 1)});
    };
    if (scene.show_light_markers) {
        for (const auto& light : scene.point_lights) marker(light.position, light.color);
        for (const auto& light : scene.spot_lights) marker(light.position, light.color);
    }
    model_count_ = static_cast<std::uint32_t>(models.size());
    std::vector<Vertex> guides;
    if (scene.show_spot_guides) {
        for (const auto& light : scene.spot_lights) {
            const auto vertices = spotlightGuide(light);
            guides.insert(guides.end(), vertices.begin(), vertices.end());
        }
    }
    guide_vertex_count_ = static_cast<std::uint32_t>(guides.size());
    if (!guides.empty()) {
        ensureCapacity(guide_buffer_, guide_capacity_, guides.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        guide_buffer_.write(guides.data(), guides.size() * sizeof(Vertex));
        models.push_back({glm::mat4(1), glm::mat4(1), glm::vec4(1), glm::vec4(0), glm::vec4(1)});
    }
    if (models.size() * model_stride_ > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Model dynamic offsets exceed uint32 range");
    if (ensureCapacity(model_buffer_, model_capacity_, models.size() * model_stride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        bindBuffer(model_set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, model_buffer_, sizeof(ModelUniformData));
    for (std::size_t i = 0; i < models.size(); ++i)
        model_buffer_.write(&models[i], sizeof(ModelUniformData), i * model_stride_);
}

void CubeRenderer::createPipeline() {
    const auto& context = graphics::internal::context;
    const VkDescriptorSetLayout layouts[] = {scene_layout_, model_layout_};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = layouts,
    };
    graphics::check(vkCreatePipelineLayout(context.device, &layout, nullptr,
                                           &pipeline_layout_), "vkCreatePipelineLayout");
    const graphics::ShaderModule vertex_shader(
        std::filesystem::path(SCENE_SHADER_DIRECTORY) / "scene.vert.spv");
    const graphics::ShaderModule fragment_shader(
        std::filesystem::path(SCENE_SHADER_DIRECTORY) / "scene.frag.spv");
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
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rasterization = {
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
    VkPipelineDepthStencilStateCreateInfo depth = {
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
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    depth.depthWriteEnable = VK_FALSE;
    graphics::check(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1,
                                              &pipeline, nullptr, &line_pipeline_),
                    "Create spotlight line pipeline");
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
    // prepare() has waited for the single frame fence: writes, descriptor updates
    // and replacing buffers cannot race with the previous GPU submission.
    updateBuffers(scene, aspect);
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
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipeline_layout_, 0, 1, &scene_set_, 0, nullptr);
    const auto bindModel = [&](std::uint32_t index) {
        const auto dynamic_offset = static_cast<std::uint32_t>(index * model_stride_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_, 1, 1, &model_set_, 1, &dynamic_offset);
    };
    for (std::uint32_t i = 0; i < model_count_; ++i) {
        bindModel(i);
        vkCmdDrawIndexed(command, static_cast<std::uint32_t>(cube_indices.size()), 1, 0, 0, 0);
    }
    if (guide_vertex_count_ > 0) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, line_pipeline_);
        const VkBuffer guides = guide_buffer_.handle();
        vkCmdBindVertexBuffers(command, 0, 1, &guides, &offset);
        bindModel(model_count_);
        vkCmdDraw(command, guide_vertex_count_, 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
    graphics::check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
}

void CubeRenderer::shutdown() noexcept {
    const auto device = graphics::internal::context.device;
    vkDestroyPipeline(device, line_pipeline_, nullptr);
    vkDestroyPipeline(device, pipeline_, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(device, scene_layout_, nullptr);
    vkDestroyDescriptorSetLayout(device, model_layout_, nullptr);
    line_pipeline_ = pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    scene_layout_ = model_layout_ = VK_NULL_HANDLE;
    scene_set_ = model_set_ = VK_NULL_HANDLE;
    model_buffer_.destroy(); scene_buffer_.destroy();
    point_buffer_.destroy(); spot_buffer_.destroy(); guide_buffer_.destroy();
    index_buffer_.destroy(); vertex_buffer_.destroy();
    model_capacity_ = point_capacity_ = spot_capacity_ = guide_capacity_ = 0;
}

} // namespace application
