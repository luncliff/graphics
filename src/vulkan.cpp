#include "vulkan_1.h"

#include <vector>

using namespace std;

// static VKAPI_ATTR VkBool32 VKAPI_CALL
// on_debug_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
//                  VkDebugUtilsMessageTypeFlagsEXT mtype,
//                  const VkDebugUtilsMessengerCallbackDataEXT* mdata, //
//                  void* user_data) {
//     switch (severity) {
//     case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
//     default:
//         return VK_FALSE;
//     }
// }

vulkan_instance_t::vulkan_instance_t(gsl::czstring<> _name,               //
                                     gsl::span<const char* const> layers, //
                                     gsl::span<const char* const> extensions) noexcept(false)
    : name{_name} {
    info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    info.pApplicationName = name.c_str();
    info.applicationVersion = 0x00'03;
    info.apiVersion = VK_API_VERSION_1_2; // highest required
    info.pEngineName = nullptr;
    info.engineVersion = VK_API_VERSION_1_2; // used to create
    VkInstanceCreateInfo request{};
    request.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    request.pApplicationInfo = &info;
    request.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    if (request.enabledExtensionCount > 0)
        request.ppEnabledExtensionNames = extensions.data();
    request.enabledLayerCount = static_cast<uint32_t>(layers.size());
    if (request.enabledLayerCount > 0)
        request.ppEnabledLayerNames = layers.data();
    if (auto ec = vkCreateInstance(&request, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateInstance"};
}

vulkan_instance_t::~vulkan_instance_t() noexcept {
    vkDestroyInstance(handle, nullptr);
}

VkResult get_physical_device(VkInstance instance, VkPhysicalDevice& handle) noexcept {
    uint32_t count = 0;
    if (auto ec = vkEnumeratePhysicalDevices(instance, &count, nullptr))
        return ec;
    auto devices = make_unique<VkPhysicalDevice[]>(count);
    if (auto ec = vkEnumeratePhysicalDevices(instance, &count, devices.get())) {
        handle = VK_NULL_HANDLE;
        return ec;
    }
    handle = devices[count - 1];
    return VK_SUCCESS;
}

uint32_t get_graphics_queue_available(VkQueueFamilyProperties* properties, uint32_t count) noexcept {
    for (auto i = 0u; i < count; ++i)
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return i;
    return static_cast<uint32_t>(-1);
}

const float global_queue_priority = 0;

VkResult create_device(VkPhysicalDevice physical_device, //
                       VkDevice& device, VkDeviceQueueCreateInfo& queue_info) noexcept {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    auto properties = make_unique<VkQueueFamilyProperties[]>(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.get());

    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.pQueuePriorities = &global_queue_priority;
    queue_info.queueFamilyIndex = get_graphics_queue_available(properties.get(), count);
    if (queue_info.queueFamilyIndex > count)
        return VK_ERROR_UNKNOWN;
    queue_info.queueCount = 1;
    // create a device
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.enabledExtensionCount = 0;
    info.enabledLayerCount = 0;
    VkPhysicalDeviceFeatures features{};
    info.pEnabledFeatures = &features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    return vkCreateDevice(physical_device, &info, nullptr, &device);
}

uint32_t get_surface_support(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t count,
                             uint32_t exclude_index) noexcept {
    for (auto i = 0u; i < count; ++i) {
        if (i == exclude_index)
            continue;
        VkBool32 support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &support);
        if (support)
            return i;
    }
    return static_cast<uint32_t>(-1);
}

vulkan_renderpass_t::vulkan_renderpass_t(VkDevice _device, VkFormat surface_format) noexcept(false) : device{_device} {
    setup_color_attachment(colors, color_ref, surface_format);
    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = 1;
    subpasses[0].pColorAttachments = &color_ref;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstSubpass = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &colors;
    info.subpassCount = 1;
    info.pSubpasses = subpasses;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;
    if (auto ec = vkCreateRenderPass(device, &info, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateRenderPass"};
}

vulkan_renderpass_t::~vulkan_renderpass_t() noexcept {
    vkDestroyRenderPass(device, handle, nullptr);
}

void vulkan_renderpass_t::setup_color_attachment(VkAttachmentDescription& colors, VkAttachmentReference& color_ref,
                                                 VkFormat surface_format) noexcept {
    colors.format = surface_format;
    colors.samples = VK_SAMPLE_COUNT_1_BIT;
    colors.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colors.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // color/depth
    colors.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colors.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // stencil
    colors.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colors.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // referencing
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

vulkan_pipeline_t::vulkan_pipeline_t(VkDevice device, VkRenderPass renderpass, VkExtent2D& extent,
                                     vulkan_pipeline_input_t& input) noexcept(false)
    : device{device} {
    input.setup_shader_stage(shader_stages);
    input.setup_vertex_input_state(vertex_input_state);
    setup_input_assembly(input_assembly);
    setup_viewport_scissor(extent, viewport_state, viewport, scissor);
    setup_rasterization_state(rasterization);
    setup_multi_sample_state(multisample);
    setup_color_blend_state(color_blend_attachment, color_blend_state);
    // setup_depth_stencil_state(depth_stencil_state)
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = shader_stages;
    info.pVertexInputState = &vertex_input_state;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = nullptr;
    info.pColorBlendState = &color_blend_state;
    info.pDynamicState = nullptr;
    if (auto ec = input.make_pipeline_layout(device, layout))
        throw vulkan_exception_t{ec, "vkCreatePipelineLayout"};
    info.layout = layout;
    info.renderPass = renderpass;
    info.subpass = 0;
    // ...
    info.basePipelineHandle = VK_NULL_HANDLE;
    info.basePipelineIndex = -1;
    if (auto ec = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &handle)) {
        vkDestroyPipelineLayout(device, layout, nullptr);
        throw vulkan_exception_t{ec, "vkCreateGraphicsPipelines"};
    }
}

vulkan_pipeline_t::~vulkan_pipeline_t() noexcept {
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyPipeline(device, handle, nullptr);
}

void vulkan_pipeline_t::setup_input_assembly(VkPipelineInputAssemblyStateCreateInfo& info) noexcept {
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.primitiveRestartEnable = VK_FALSE;
}

void vulkan_pipeline_t::setup_viewport_scissor(const VkExtent2D& extent, VkPipelineViewportStateCreateInfo& info,
                                               VkViewport& viewport, VkRect2D& scissor) noexcept {
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.x = viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.offset = {0, 0};
    scissor.extent = extent;
    info.viewportCount = 1;
    info.pViewports = &viewport;
    info.scissorCount = 1;
    info.pScissors = &scissor;
}

void vulkan_pipeline_t::setup_rasterization_state(VkPipelineRasterizationStateCreateInfo& info) noexcept {
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.depthClampEnable = VK_FALSE;
    info.rasterizerDiscardEnable = VK_FALSE;
    info.polygonMode = VK_POLYGON_MODE_FILL;
    info.lineWidth = 1.0f;
    info.cullMode = VK_CULL_MODE_BACK_BIT;
    info.frontFace = VK_FRONT_FACE_CLOCKWISE; // GL coordinate
    // info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // Vulkan coordinate
    info.depthBiasEnable = VK_FALSE;
    info.depthBiasConstantFactor = 0.0f;
    info.depthBiasClamp = 0.0f;
    info.depthBiasSlopeFactor = 0.0f;
}

void vulkan_pipeline_t::setup_multi_sample_state(VkPipelineMultisampleStateCreateInfo& info) noexcept {
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    info.sampleShadingEnable = VK_FALSE;
    info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    info.minSampleShading = 1.0f;
    info.pSampleMask = nullptr;
    info.alphaToCoverageEnable = VK_FALSE;
    info.alphaToOneEnable = VK_FALSE;
}

void vulkan_pipeline_t::setup_color_blend_state(VkPipelineColorBlendAttachmentState& attachment,
                                                VkPipelineColorBlendStateCreateInfo& info) noexcept {
    attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info.logicOpEnable = VK_FALSE;
    info.logicOp = VK_LOGIC_OP_COPY;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
}

VkResult check_surface_format(VkPhysicalDevice device, VkSurfaceKHR surface, //
                              VkFormat surface_format, VkColorSpaceKHR surface_color_space, bool& suitable) noexcept {
    uint32_t num_formats = 0;
    if (auto ec = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, nullptr))
        return ec;
    auto formats = make_unique<VkSurfaceFormatKHR[]>(num_formats);
    if (auto ec = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, formats.get()))
        return ec;
    suitable = false;
    for (auto i = 0u; i < num_formats; ++i) {
        const auto& format = formats[i];
        if (format.format == surface_format && format.colorSpace == surface_color_space) {
            suitable = true;
            break;
        }
    }
    return VK_SUCCESS;
}

VkResult check_present_mode(VkPhysicalDevice device, VkSurfaceKHR surface, //
                            const VkPresentModeKHR present_mode, bool& suitable) noexcept {
    uint32_t num_modes = 0;
    if (auto ec = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_modes, nullptr))
        return ec;
    auto modes = make_unique<VkPresentModeKHR[]>(num_modes);
    if (auto ec = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_modes, modes.get()))
        return ec;
    suitable = false;
    for (auto i = 0u; i < num_modes; ++i) {
        if (modes[i] == present_mode) {
            suitable = true;
            break;
        }
    }
    return VK_SUCCESS;
}

VkResult check_support(VkPhysicalDevice device, VkSurfaceKHR surface, //
                       VkFormat surface_format, VkColorSpaceKHR surface_color_space,
                       VkPresentModeKHR present_mode) noexcept {
    uint32_t num_formats = 0;
    if (auto ec = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, nullptr))
        return ec;
    auto formats = make_unique<VkSurfaceFormatKHR[]>(num_formats);
    if (auto ec = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &num_formats, formats.get()))
        return ec;
    bool suitable = false;
    for (auto i = 0u; i < num_formats; ++i) {
        const auto& format = formats[i];
        if (format.format == surface_format && format.colorSpace == surface_color_space) {
            suitable = true;
            break;
        }
    }
    if (suitable == false)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    uint32_t num_modes = 0;
    if (auto ec = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_modes, nullptr))
        return ec;
    auto modes = make_unique<VkPresentModeKHR[]>(num_modes);
    if (auto ec = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &num_modes, modes.get()))
        return ec;
    suitable = false;
    for (auto i = 0u; i < num_modes; ++i) {
        if (modes[i] == present_mode) {
            suitable = true;
            break;
        }
    }
    return suitable ? VK_SUCCESS : VK_ERROR_UNKNOWN;
};

VkResult create_device(VkPhysicalDevice physical_device,                     //
                       const VkSurfaceKHR* surfaces, uint32_t surface_count, //
                       VkDevice& device, VkDeviceQueueCreateInfo (&queues)[2]) noexcept {
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_device, &features);
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    auto properties = std::make_unique<VkQueueFamilyProperties[]>(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties.get());
    // 2 queue (gfx, present)
    queues[0].sType = queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].pQueuePriorities = queues[1].pQueuePriorities = &global_queue_priority;
    queues[0].queueFamilyIndex = queues[1].queueFamilyIndex = UINT32_MAX;
    for (auto i = 0u; i < count; ++i) {
        // graphics queue
        if (queues[0].queueFamilyIndex == UINT32_MAX) {
            const auto& prop = properties[i];
            if (prop.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queues[0].queueFamilyIndex = i;
                queues[0].queueCount = 1;
                continue;
            }
        }
        // present queue
        if (queues[1].queueFamilyIndex == UINT32_MAX) {
            VkBool32 support = false;
            for (auto surface : gsl::make_span(surfaces, surface_count)) {
                if (auto ec = vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &support))
                    return ec;
                if (support == false) // all surface should be supported
                    break;
            }
            if (support == false) // try next
                continue;
            queues[1].queueFamilyIndex = i;
            queues[1].queueCount = 1;
        }
    }
    if (queues[0].queueFamilyIndex > count)
        return VK_ERROR_UNKNOWN;
    if (queues[1].queueFamilyIndex > count)
        return VK_ERROR_UNKNOWN;
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 2;
    info.pQueueCreateInfos = queues;
    const char* extension_names[1]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    info.ppEnabledExtensionNames = extension_names;
    info.enabledExtensionCount = 1;
    info.pEnabledFeatures = &features;
    return vkCreateDevice(physical_device, &info, nullptr, &device);
}

vulkan_shader_module_t::vulkan_shader_module_t(VkDevice _device, const fs::path fpath) noexcept(false)
    : device{_device} {
    if (fs::exists(fpath) == false)
        throw system_error{ENOENT, system_category()};
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    const auto blob = read_all(fpath, info.codeSize);
    info.pCode = reinterpret_cast<const uint32_t*>(blob.get());
    if (auto ec = vkCreateShaderModule(device, &info, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateShaderModule"};
}

vulkan_shader_module_t::~vulkan_shader_module_t() noexcept {
    vkDestroyShaderModule(device, handle, nullptr);
}

vulkan_swapchain_t::vulkan_swapchain_t(VkDevice _device, VkSurfaceKHR surface,
                                       const VkSurfaceCapabilitiesKHR& capabilities, VkFormat surface_format,
                                       VkColorSpaceKHR surface_color_space, VkPresentModeKHR present_mode)
    : device{_device} {
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = capabilities.minImageCount + 1;
    info.imageFormat = surface_format;
    info.imageColorSpace = surface_color_space;
    info.imageExtent = capabilities.maxImageExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform; // rotation/flip
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = present_mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;
    if (auto ec = vkCreateSwapchainKHR(device, &info, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateSwapchainKHR"};
}

vulkan_swapchain_t::~vulkan_swapchain_t() noexcept {
    vkDestroySwapchainKHR(device, handle, nullptr);
}

vulkan_presentation_t::vulkan_presentation_t(VkDevice _device, VkRenderPass renderpass, //
                                             VkSwapchainKHR swapchain, const VkSurfaceCapabilitiesKHR& capabilities,
                                             VkFormat surface_format) noexcept(false)
    : device{_device} {
    if (auto ec = vkGetSwapchainImagesKHR(device, swapchain, &num_images, nullptr))
        throw vulkan_exception_t{ec, "vkGetSwapchainImagesKHR"};
    num_images = min(num_images, capabilities.minImageCount + 1);
    images = make_unique<VkImage[]>(num_images);
    if (auto ec = vkGetSwapchainImagesKHR(device, swapchain, &num_images, images.get()))
        throw vulkan_exception_t{ec, "vkGetSwapchainImagesKHR"};

    image_views = make_unique<VkImageView[]>(num_images);
    for (auto i = 0u; i < num_images; ++i) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = images[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D; // 1D, 2D, CUBE ...
        info.format = surface_format;
        info.components = {}; // VK_COMPONENT_SWIZZLE_IDENTITY == 0
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        if (auto ec = vkCreateImageView(device, &info, nullptr, //
                                        &image_views[i]))
            throw vulkan_exception_t{ec, "vkCreateImageView"};
    }
    framebuffers = make_unique<VkFramebuffer[]>(num_images);
    for (auto i = 0u; i < num_images; ++i) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderpass;
        VkImageView attachments[] = {image_views[i]};
        info.attachmentCount = 1;
        info.pAttachments = attachments;
        const auto& image_extent = capabilities.maxImageExtent;
        info.width = image_extent.width;
        info.height = image_extent.height;
        info.layers = 1;
        if (auto ec = vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]))
            throw vulkan_exception_t{ec, "vkCreateFramebuffer"};
    }
}

vulkan_presentation_t::~vulkan_presentation_t() noexcept {
    for (auto i = 0u; i < num_images; ++i) {
        vkDestroyFramebuffer(device, framebuffers[i], nullptr);
        vkDestroyImageView(device, image_views[i], nullptr);
        // vkDestroyImage(device, images[i], nullptr);
    }
}

vulkan_command_pool_t::vulkan_command_pool_t(VkDevice _device, uint32_t queue_index, uint32_t _count) noexcept(false)
    : device{_device}, count{_count} {
    {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = queue_index;
        if (auto ec = vkCreateCommandPool(device, &info, nullptr, &handle))
            throw vulkan_exception_t{ec, "vkCreateCommandPool"};
    }
    {
        buffers = make_unique<VkCommandBuffer[]>(count);
        VkCommandBufferAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = handle;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // == 0
        info.commandBufferCount = count;
        if (auto ec = vkAllocateCommandBuffers(device, &info, buffers.get())) {
            vkDestroyCommandPool(device, handle, nullptr);
            throw vulkan_exception_t{ec, "vkAllocateCommandBuffers"};
        }
    }
}

vulkan_command_pool_t::~vulkan_command_pool_t() noexcept {
    vkFreeCommandBuffers(device, handle, count, buffers.get());
    vkDestroyCommandPool(device, handle, nullptr);
}

vulkan_semaphore_t::vulkan_semaphore_t(VkDevice _device) noexcept(false) : device{_device} {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (auto ec = vkCreateSemaphore(device, &info, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateSemaphore"};
}

vulkan_semaphore_t::~vulkan_semaphore_t() noexcept {
    vkDestroySemaphore(device, handle, nullptr);
}

vulkan_fence_t::vulkan_fence_t(VkDevice _device) noexcept(false) : device{_device} {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (auto ec = vkCreateFence(device, &info, nullptr, &handle))
        throw vulkan_exception_t{ec, "vkCreateFence"};
}

vulkan_fence_t::~vulkan_fence_t() noexcept {
    vkDestroyFence(device, handle, nullptr);
}

VkResult create_uniform_buffer(VkDevice device, VkBuffer& buffer, VkBufferCreateInfo& info,
                               VkDeviceSize length) noexcept {
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = length;
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return vkCreateBuffer(device, &info, nullptr, &buffer);
}
VkResult create_vertex_buffer(VkDevice device, VkBuffer& buffer, VkBufferCreateInfo& info,
                              VkDeviceSize length) noexcept {
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = length;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return vkCreateBuffer(device, &info, nullptr, &buffer);
}
VkResult create_index_buffer(VkDevice device, VkBuffer& buffer, VkBufferCreateInfo& info,
                             VkDeviceSize length) noexcept {
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = length;
    info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return vkCreateBuffer(device, &info, nullptr, &buffer);
}

VkResult allocate_memory(VkDevice device, VkBuffer buffer, VkDeviceMemory& memory,
                         const VkBufferCreateInfo& buffer_info, VkFlags desired,
                         const VkPhysicalDeviceMemoryProperties& props) noexcept {
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = requirements.size;
    for (auto i = 0u; i < props.memoryTypeCount; ++i) {
        // match type?
        const auto typebit = (1 << i);
        if (requirements.memoryTypeBits & typebit) {
            // match prop?
            const auto mtype = props.memoryTypes[i];
            if ((mtype.propertyFlags & desired) == desired)
                info.memoryTypeIndex = i;
        }
    }
    return vkAllocateMemory(device, &info, nullptr, &memory);
}

VkResult update_memory(VkDevice device, VkDeviceMemory memory,
                       const VkMemoryRequirements& requirements, //
                       const void* data, uint32_t offset) noexcept {
    const auto flags = VkMemoryMapFlags{};
    void* dst = nullptr;
    if (auto ec = vkMapMemory(device, memory, offset, requirements.size, flags, &dst))
        return ec;
    memcpy(dst, data, requirements.size);
    vkUnmapMemory(device, memory);
    return VK_SUCCESS;
}

VkResult write_memory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, const void* data) noexcept {
    constexpr auto offset = 0;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    const auto flags = VkMemoryMapFlags{};
    void* dst = nullptr;
    if (auto ec = vkMapMemory(device, memory, offset, requirements.size, //
                              flags, &dst))
        return ec;
    memcpy(dst, data, requirements.size);
    vkUnmapMemory(device, memory);
    return VK_SUCCESS;
}

VkResult render_submit(VkQueue queue,                       //
                       gsl::span<VkCommandBuffer> commands, //
                       VkFence fence, VkSemaphore wait, VkSemaphore signal) noexcept {
    VkSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.pCommandBuffers = commands.data();
    info.commandBufferCount = commands.size();
    const VkPipelineStageFlags stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    info.pWaitDstStageMask = stages;
    if (wait != VK_NULL_HANDLE) { // if null handle, no wait
        info.pWaitSemaphores = &wait;
        info.waitSemaphoreCount = 1;
    }
    if (signal != VK_NULL_HANDLE) { // if null handle, no signal
        info.pSignalSemaphores = &signal;
        info.signalSemaphoreCount = 1;
    }
    return vkQueueSubmit(queue, 1, &info, fence);
}

VkResult present_submit(VkQueue queue,                                  //
                        uint32_t image_index, VkSwapchainKHR swapchain, //
                        VkSemaphore wait) noexcept {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    VkSemaphore wait_group[] = {wait};
    info.pWaitSemaphores = wait_group;
    info.waitSemaphoreCount = 1;
    VkSwapchainKHR swapchains[] = {swapchain};
    info.pSwapchains = swapchains;
    info.swapchainCount = 1;
    info.pImageIndices = &image_index;
    return vkQueuePresentKHR(queue, &info);
}

vulkan_command_recorder_t::vulkan_command_recorder_t(VkCommandBuffer command_buffer, //
                                                     VkRenderPass renderpass, VkFramebuffer framebuffer,
                                                     VkExtent2D extent) noexcept(false)
    : commands{command_buffer}, clear{} {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (auto ec = vkBeginCommandBuffer(commands, &begin))
        throw vulkan_exception_t{ec, "vkBeginCommandBuffer"};
    VkRenderPassBeginInfo render{};
    render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render.renderPass = renderpass;
    render.framebuffer = framebuffer;
    render.renderArea.offset = {0, 0}; // consider shader (loads/stores)
    render.renderArea.extent = extent;
    render.clearValueCount = 1;
    clear.color.float32[0] = 0;
    clear.color.float32[1] = 0;
    clear.color.float32[2] = 0;
    clear.color.float32[3] = 1;
    render.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &render, VK_SUBPASS_CONTENTS_INLINE);
}

vulkan_command_recorder_t::~vulkan_command_recorder_t() noexcept(false) {
    vkCmdEndRenderPass(commands);
    if (auto ec = vkEndCommandBuffer(commands))
        throw vulkan_exception_t{ec, "vkEndCommandBuffer"};
}
