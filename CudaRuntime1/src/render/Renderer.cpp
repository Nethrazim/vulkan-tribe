#include "render/Renderer.h"

#include "core/Window.h"
#include "ecs/Components.h"
#include "render/Mesh.h"
#include "render/Swapchain.h"
#include "render/Texture.h"
#include "render/VulkanContext.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace vt {
namespace {

void check(VkResult result, const char *what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(what) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

std::vector<char> readFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open " + path.string());
    }
    const std::streamsize size = file.tellg();
    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(bytes.data(), size);
    return bytes;
}

} // namespace

Renderer::Renderer(VulkanContext &context,
                   Swapchain &swapchain,
                   Window &window,
                   const std::filesystem::path &shaderDirectory)
    : context_(context), swapchain_(swapchain), window_(window) {
    createDescriptorSetLayout();
    createPipeline(shaderDirectory);
    createUniformBuffers();
    createDescriptorPool();
    createCommandBuffers();
    createSyncObjects();

    descriptorSets_.resize(kFramesInFlight);
}

Renderer::~Renderer() {
    VkDevice device = context_.device();

    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : imageAvailable_) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (VkFence fence : inFlight_) {
        vkDestroyFence(device, fence, nullptr);
    }

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
    }

    for (std::size_t i = 0; i < uniformBuffers_.size(); ++i) {
        if (uniformMapped_[i]) {
            vkUnmapMemory(device, uniformMemory_[i]);
        }
        vkDestroyBuffer(device, uniformBuffers_[i], nullptr);
        vkFreeMemory(device, uniformMemory_[i], nullptr);
    }

    // Scene resources hold Vulkan handles, so release them before the device.
    meshes_.clear();
    textures_.clear();

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
    }
}

void Renderer::waitIdle() const {
    vkDeviceWaitIdle(context_.device());
}

void Renderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding cameraBinding{};
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    const std::array<VkDescriptorSetLayoutBinding, 2> bindings = {cameraBinding, samplerBinding};

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    info.pBindings = bindings.data();

    check(vkCreateDescriptorSetLayout(context_.device(), &info, nullptr, &descriptorSetLayout_),
          "vkCreateDescriptorSetLayout");
}

VkShaderModule Renderer::createShaderModule(const std::filesystem::path &path) const {
    const std::vector<char> code = readFile(path);

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const std::uint32_t *>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(context_.device(), &info, nullptr, &module),
          "vkCreateShaderModule");
    return module;
}

void Renderer::createPipeline(const std::filesystem::path &shaderDirectory) {
    VkShaderModule vertexModule = createShaderModule(shaderDirectory / "cube.vert.spv");
    VkShaderModule fragmentModule = createShaderModule(shaderDirectory / "cube.frag.spv");

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexModule;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentModule;
    fragmentStage.pName = "main";

    const VkPipelineShaderStageCreateInfo stages[] = {vertexStage, fragmentStage};

    const VkVertexInputBindingDescription binding = Vertex::bindingDescription();
    const auto attributes = Vertex::attributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor are dynamic so a window resize does not need a
    // new pipeline.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // Y is flipped in the projection matrix, which reverses winding on screen.
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ModelPushConstant);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    check(vkCreatePipelineLayout(context_.device(), &layoutInfo, nullptr, &pipelineLayout_),
          "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = swapchain_.renderPass();
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1,
                                                      &pipelineInfo, nullptr, &pipeline_);

    vkDestroyShaderModule(context_.device(), fragmentModule, nullptr);
    vkDestroyShaderModule(context_.device(), vertexModule, nullptr);

    check(result, "vkCreateGraphicsPipelines");
}

void Renderer::createUniformBuffers() {
    const VkDeviceSize size = sizeof(CameraUniforms);

    uniformBuffers_.resize(kFramesInFlight);
    uniformMemory_.resize(kFramesInFlight);
    uniformMapped_.resize(kFramesInFlight, nullptr);

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        context_.createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              uniformBuffers_[i], uniformMemory_[i]);
        check(vkMapMemory(context_.device(), uniformMemory_[i], 0, size, 0, &uniformMapped_[i]),
              "vkMapMemory");
    }
}

void Renderer::createDescriptorPool() {
    const std::uint32_t maxSets = kFramesInFlight * kMaxTextures;

    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = maxSets;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = maxSets;

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    info.maxSets = maxSets;

    check(vkCreateDescriptorPool(context_.device(), &info, nullptr, &descriptorPool_),
          "vkCreateDescriptorPool");
}

void Renderer::createCommandBuffers() {
    commandBuffers_.resize(kFramesInFlight);

    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = context_.commandPool();
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = kFramesInFlight;

    check(vkAllocateCommandBuffers(context_.device(), &info, commandBuffers_.data()),
          "vkAllocateCommandBuffers");
}

void Renderer::createSyncObjects() {
    imageAvailable_.resize(kFramesInFlight);
    inFlight_.resize(kFramesInFlight);
    // One render-finished semaphore per swapchain image: presentation waits on
    // the image's own semaphore, not the frame slot's.
    renderFinished_.resize(swapchain_.imageCount());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        check(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &imageAvailable_[i]),
              "vkCreateSemaphore");
        check(vkCreateFence(context_.device(), &fenceInfo, nullptr, &inFlight_[i]),
              "vkCreateFence");
    }
    for (std::size_t i = 0; i < renderFinished_.size(); ++i) {
        check(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &renderFinished_[i]),
              "vkCreateSemaphore");
    }
}

std::uint32_t Renderer::registerMesh(Mesh *mesh) {
    meshes_.emplace_back(mesh);
    return static_cast<std::uint32_t>(meshes_.size() - 1);
}

std::uint32_t Renderer::registerTexture(Texture *texture) {
    if (textures_.size() >= kMaxTextures) {
        throw std::runtime_error("Texture table is full");
    }
    textures_.emplace_back(texture);
    const auto id = static_cast<std::uint32_t>(textures_.size() - 1);
    allocateDescriptorSetsForTexture(id);
    return id;
}

void Renderer::allocateDescriptorSetsForTexture(std::uint32_t textureId) {
    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout_;

        VkDescriptorSet set = VK_NULL_HANDLE;
        check(vkAllocateDescriptorSets(context_.device(), &allocInfo, &set),
              "vkAllocateDescriptorSets");

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[frame];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUniforms);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textures_[textureId]->view();
        imageInfo.sampler = textures_[textureId]->sampler();

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = set;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = set;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(context_.device(), static_cast<std::uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);

        if (descriptorSets_[frame].size() <= textureId) {
            descriptorSets_[frame].resize(textureId + 1, VK_NULL_HANDLE);
        }
        descriptorSets_[frame][textureId] = set;
    }
}

void Renderer::updateCameraUniforms(Registry &registry, std::uint32_t frame) {
    CameraUniforms uniforms{};
    uniforms.view = glm::mat4(1.0f);
    uniforms.proj = glm::mat4(1.0f);

    bool found = false;
    registry.each<Camera, Transform>([&](Entity entity) {
        if (found) {
            return;
        }
        found = true;

        const Camera &camera = registry.get<Camera>(entity);
        const Transform &transform = registry.get<Transform>(entity);

        uniforms.view = glm::lookAt(transform.position, camera.target, camera.up);
        uniforms.proj = glm::perspective(camera.fovY, swapchain_.aspectRatio(),
                                         camera.nearZ, camera.farZ);
        // GLM targets OpenGL's Y-up clip space; Vulkan's Y points down.
        uniforms.proj[1][1] *= -1.0f;
    });

    std::memcpy(uniformMapped_[frame], &uniforms, sizeof(uniforms));
}

void Renderer::recordCommands(VkCommandBuffer commandBuffer,
                              std::uint32_t imageIndex,
                              Registry &registry,
                              std::uint32_t frame) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.06f, 0.07f, 0.10f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = swapchain_.renderPass();
    renderPassInfo.framebuffer = swapchain_.framebuffer(imageIndex);
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain_.extent();
    renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_.extent().width);
    viewport.height = static_cast<float>(swapchain_.extent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_.extent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    registry.each<MeshRenderer, Transform>([&](Entity entity) {
        const MeshRenderer &renderable = registry.get<MeshRenderer>(entity);
        const Transform &transform = registry.get<Transform>(entity);

        if (renderable.mesh >= meshes_.size() || renderable.texture >= textures_.size()) {
            return;
        }

        VkDescriptorSet set = descriptorSets_[frame][renderable.texture];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_, 0, 1, &set, 0, nullptr);

        ModelPushConstant push{};
        push.model = transform.matrix();
        vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(push), &push);

        meshes_[renderable.mesh]->bind(commandBuffer);
        meshes_[renderable.mesh]->draw(commandBuffer);
    });

    vkCmdEndRenderPass(commandBuffer);
    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::drawFrame(Registry &registry) {
    VkDevice device = context_.device();

    check(vkWaitForFences(device, 1, &inFlight_[currentFrame_], VK_TRUE, UINT64_MAX),
          "vkWaitForFences");

    std::uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(device, swapchain_.handle(), UINT64_MAX,
                                              imageAvailable_[currentFrame_],
                                              VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.recreate();
        return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }

    updateCameraUniforms(registry, currentFrame_);

    // Only reset once we know we will submit, or the fence stays unsignalled.
    check(vkResetFences(device, 1, &inFlight_[currentFrame_]), "vkResetFences");

    check(vkResetCommandBuffer(commandBuffers_[currentFrame_], 0), "vkResetCommandBuffer");
    recordCommands(commandBuffers_[currentFrame_], imageIndex, registry, currentFrame_);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_[currentFrame_];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffers_[currentFrame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_[imageIndex];

    check(vkQueueSubmit(context_.graphicsQueue(), 1, &submit, inFlight_[currentFrame_]),
          "vkQueueSubmit");

    VkSwapchainKHR swapchains[] = {swapchain_.handle()};

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = swapchains;
    present.pImageIndices = &imageIndex;

    const VkResult presented = vkQueuePresentKHR(context_.presentQueue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
        window_.consumeResized()) {
        swapchain_.recreate();
    } else if (presented != VK_SUCCESS) {
        check(presented, "vkQueuePresentKHR");
    }

    currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
}

} // namespace vt
