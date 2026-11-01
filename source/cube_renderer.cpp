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
    upload_.initialize();
    const auto vertices = cubeVertices();
    graphics::uploadBuffer(upload_, vertex_buffer_, vertices.data(), sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    graphics::uploadBuffer(upload_, index_buffer_, cube_indices.data(), sizeof(cube_indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    if (!(vertex_buffer_.memoryProperties() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !(index_buffer_.memoryProperties() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        vertex_buffer_.mapped() || index_buffer_.mapped())
        throw std::runtime_error("Geometry must use unmapped device-local allocations");
    createMaterials();
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
                                  VkDeviceSize required, VkBufferUsageFlags usage, graphics::MemoryUsage memory) {
    if (required <= capacity) return false;
    graphics::Buffer replacement;
    const auto new_capacity = std::max(required, capacity * 2);
    replacement.create(new_capacity, usage, memory);
    buffer = std::move(replacement);
    capacity = new_capacity;
    return true;
}

void CubeRenderer::createMaterials() {
    textures_.reserve(2 + materials.size() * 3);
    textures_.emplace_back();
    textures_.back().create({1, 1, {255, 255, 255, 255}}, true, upload_);
    textures_.emplace_back();
    textures_.back().create({1, 1, {0, 0, 0, 255}}, false, upload_);
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& material = materials[i];
        const char* paths[] = {material.albedo_path, material.specular_path, material.emissive_path};
        for (std::size_t map = 0; map < 3; ++map) {
            if (paths[map][0] == '\0') {
                material_textures_[i][map] = map == 0 ? 0 : 1;
                continue;
            }
            const auto pixels = graphics::decodeImage(std::filesystem::path(SCENE_ASSET_DIRECTORY) / paths[map]);
            const auto index = textures_.size();
            textures_.emplace_back();
            textures_.back().create(pixels, map != 1, upload_, i == 3
                ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT : VK_SAMPLER_ADDRESS_MODE_REPEAT);
            material_textures_[i][map] = index;
        }
    }
}

void CubeRenderer::createDescriptors() {
    const auto device = graphics::internal::context.device;
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &properties);
    const auto alignment = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    model_stride_ = alignedUniformStride(sizeof(ModelUniformData), alignment);
    if (sizeof(ModelUniformData) > properties.limits.maxUniformBufferRange)
        throw std::runtime_error("Model UBO exceeds maxUniformBufferRange");
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
    const VkDescriptorSetLayoutBinding material_bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    layout.bindingCount = 4;
    layout.pBindings = material_bindings;
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &model_layout_),
                    "Material descriptor layout");
    constexpr auto material_count = static_cast<std::uint32_t>(materials.size());
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, material_count},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * material_count},
    };
    const VkDescriptorPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = material_count + 1, .poolSizeCount = 4, .pPoolSizes = sizes,
    };
    graphics::check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptor_pool_),
                    "vkCreateDescriptorPool");
    std::array<VkDescriptorSetLayout, material_count + 1> layouts{};
    layouts.fill(model_layout_); layouts[0] = scene_layout_;
    std::array<VkDescriptorSet, material_count + 1> sets{};
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool_, .descriptorSetCount = material_count + 1, .pSetLayouts = layouts.data(),
    };
    graphics::check(vkAllocateDescriptorSets(device, &allocate, sets.data()), "vkAllocateDescriptorSets");
    scene_set_ = sets[0];
    for (std::size_t i = 0; i < materials.size(); ++i) {
        material_sets_[i] = sets[i + 1];
        for (std::uint32_t map = 0; map < 3; ++map) {
            const auto image = textures_[material_textures_[i][map]].descriptor();
            const VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = material_sets_[i], .dstBinding = map + 1, .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &image,
            };
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
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
    draw_states_.clear();
    const auto addObject = [&](const Cube& cube) {
        if (cube.material_id >= materials.size()) throw std::out_of_range("Invalid material id");
        draw_states_.push_back({cube.material_id, effectPushConstants(cube.texture_effect, cube.material_id != 0)});
        const auto model = cube.model();
        models.push_back({model, glm::transpose(glm::inverse(model)),
            glm::vec4(cube.albedo, cube.vertex_colors ? 1.0f : 0.0f),
            glm::vec4(cube.specular, cube.shininess), glm::vec4(0),
            glm::vec4(cube.material_id != 0 ? 1.0f : 0.0f, cube.emissive_strength,
                      cube.specular_map_enabled ? 1.0f : 0.0f, cube.emissive_map_enabled ? 1.0f : 0.0f)});
    };
    for (const auto& cube : scene.cubes) addObject(cube);
    if (scene.show_room)
        for (const auto& surface : scene.room) addObject(surface);
    const auto marker = [&](glm::vec3 position, glm::vec3 color) {
        draw_states_.push_back({});
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
    guide_bytes_ = guides.size() * sizeof(Vertex);
    if (!guides.empty()) {
        ensureCapacity(guide_buffer_, guide_capacity_, guide_bytes_,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, graphics::MemoryUsage::device);
        ensureCapacity(guide_staging_buffer_, guide_staging_capacity_, guide_bytes_, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        guide_staging_buffer_.write(guides.data(), guide_bytes_);
        draw_states_.push_back({});
        models.push_back({glm::mat4(1), glm::mat4(1), glm::vec4(1), glm::vec4(0), glm::vec4(1)});
    }
    if (models.size() * model_stride_ > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("Model dynamic offsets exceed uint32 range");
    if (ensureCapacity(model_buffer_, model_capacity_, models.size() * model_stride_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
        for (auto set : material_sets_)
            bindBuffer(set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, model_buffer_, sizeof(ModelUniformData));
    for (std::size_t i = 0; i < models.size(); ++i)
        model_buffer_.write(&models[i], sizeof(ModelUniformData), i * model_stride_);
}

void CubeRenderer::createPipeline() {
    const auto& context = graphics::internal::context;
    const VkDescriptorSetLayout layouts[] = {scene_layout_, model_layout_};
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physical_device, &properties);
    if (sizeof(EffectPushConstants) > properties.limits.maxPushConstantsSize)
        throw std::runtime_error("Texture effect exceeds maxPushConstantsSize");
    const VkPushConstantRange push_range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(EffectPushConstants)};
    const VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = layouts,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range,
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
        {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = 4, .pVertexAttributeDescriptions = attributes,
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
    if (guide_bytes_)
        graphics::copyBuffer(command, guide_staging_buffer_, guide_buffer_, guide_bytes_, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
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
        const auto& draw = draw_states_.at(index);
        const auto set = material_sets_[draw.material_id];
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline_layout_, 1, 1, &set, 1, &dynamic_offset);
        vkCmdPushConstants(command, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(EffectPushConstants), &draw.effect);
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
    scene_set_ = VK_NULL_HANDLE;
    material_sets_.fill(VK_NULL_HANDLE);
    textures_.clear();
    upload_.destroy();
    guide_staging_buffer_.destroy();
    guide_staging_capacity_ = guide_bytes_ = 0;
    draw_states_.clear();
    model_buffer_.destroy(); scene_buffer_.destroy();
    point_buffer_.destroy(); spot_buffer_.destroy(); guide_buffer_.destroy();
    index_buffer_.destroy(); vertex_buffer_.destroy();
    model_capacity_ = point_capacity_ = spot_capacity_ = guide_capacity_ = 0;
}

} // namespace application
