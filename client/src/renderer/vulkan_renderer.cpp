#include "vulkan.h"
#include "shaders.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string.h>

std::unique_ptr<Renderer> Renderer::Create() {
    auto renderer = std::make_unique<Vulkan_Renderer>();

    if (!renderer->Start()) {
        Log_Error("renderer", "cannot initialize Vulkan");
        return nullptr;
    }

    return renderer;
}

bool Vulkan_Renderer::Load_Instance_Functions() {
    bool loaded = true;

#define VULKAN_LOAD_INSTANCE(name) \
    vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(instance, #name)); \
    loaded = loaded && vk.name != nullptr;
    VULKAN_INSTANCE_FUNCTIONS(VULKAN_LOAD_INSTANCE)
#undef VULKAN_LOAD_INSTANCE
    return loaded;
}

bool Vulkan_Renderer::Load_Device_Functions() {
    bool loaded = true;

#define VULKAN_LOAD_DEVICE(name) \
    vk.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(device, #name)); \
    loaded = loaded && vk.name != nullptr;
    VULKAN_DEVICE_FUNCTIONS(VULKAN_LOAD_DEVICE)
#undef VULKAN_LOAD_DEVICE
#if defined(_WIN32)
    vk.vkImportSemaphoreWin32HandleKHR = reinterpret_cast<PFN_vkImportSemaphoreWin32HandleKHR>(
        vk.vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR")
    );
    external = external && vk.vkImportSemaphoreWin32HandleKHR != nullptr;
#endif
    return loaded;
}

bool Vulkan_Renderer::Has_External_Extensions() {
#if defined(_WIN32)
    std::vector<VkExtensionProperties> available;
    u32 count = 0;
    u32 found = 0;

    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    available.resize(count);
    vk.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());
    for (const VkExtensionProperties& extension : available) {
        found += strcmp(extension.extensionName, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0 || strcmp(extension.extensionName, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME) == 0 ? 1 : 0;
    }
    return found == 2;
#else
    return false;
#endif
}

bool Vulkan_Renderer::Pick_Device() {
    VkPhysicalDevice physicals[16];
    u32 physical_count = 16;
    VkPhysicalDeviceIDProperties ids = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 properties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features features13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkQueueFamilyProperties families[16];
    u32 family_count;
    s32 best_score = -1;
    s32 score;

    if (vk.vkEnumeratePhysicalDevices(instance, &physical_count, physicals) < VK_SUCCESS) {
        return false;
    }

    for (u32 index = 0; index < physical_count; index++) {
        properties.pNext = &ids;
        vk.vkGetPhysicalDeviceProperties2(physicals[index], &properties);
        features12.pNext = &features13;
        features.pNext = &features12;
        vk.vkGetPhysicalDeviceFeatures2(physicals[index], &features);
        family_count = 16;
        vk.vkGetPhysicalDeviceQueueFamilyProperties(physicals[index], &family_count, families);
        score = properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
        for (u32 family = 0; family < family_count; family++) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && properties.properties.apiVersion >= VK_API_VERSION_1_3 &&
                features13.dynamicRendering && features12.timelineSemaphore && score > best_score) {
                best_score = score;
                physical = physicals[index];
                queueFamily = family;
                textureSizeLimit = properties.properties.limits.maxImageDimension2D;
                memcpy(uuid, ids.deviceUUID, VK_UUID_SIZE);
                memcpy(luid, ids.deviceLUID, VK_LUID_SIZE);
                luidValid = ids.deviceLUIDValid;
            }
        }
    }

    return best_score >= 0;
}

bool Vulkan_Renderer::Create_Device() {
    f32 priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    const char* extensions[3] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    u32 extension_count = 1;
    VkPhysicalDeviceVulkan12Features enable12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features enable13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

    vk.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    external = Has_External_Extensions();
#if defined(_WIN32)
    if (external) {
        extensions[extension_count++] = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
    }
#endif
    queue_info.queueFamilyIndex = queueFamily;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    enable12.timelineSemaphore = VK_TRUE;
    enable12.pNext = &enable13;
    enable13.dynamicRendering = VK_TRUE;
    info.pNext = &enable12;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = extension_count;
    info.ppEnabledExtensionNames = extensions;
    return vk.vkCreateDevice(physical, &info, nullptr, &device) == VK_SUCCESS && Load_Device_Functions();
}

VkShaderModule Vulkan_Renderer::Shader(const u8* code, usize size) {
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule module = VK_NULL_HANDLE;

    info.codeSize = size;
    info.pCode = reinterpret_cast<const u32*>(code);
    vk.vkCreateShaderModule(device, &info, nullptr, &module);
    return module;
}

bool Vulkan_Renderer::Create_Objects() {
    VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkSamplerCreateInfo sampler_info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    VkDescriptorSetLayoutBinding bindings[2] = {
        { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
    };
    VkDescriptorSetLayoutCreateInfo set_layout = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPushConstantRange push = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64 };
    VkPipelineLayoutCreateInfo layout_info = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    u32 white_pixel = 0xFFFFFFFF;

    vk.vkGetDeviceQueue(device, queueFamily, 0, &queue);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queueFamily;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = VK_LOD_CLAMP_NONE;
    set_layout.bindingCount = 2;
    set_layout.pBindings = bindings;
    if (vk.vkCreateCommandPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS ||
        vk.vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS ||
        vk.vkCreateDescriptorSetLayout(device, &set_layout, nullptr, &setLayout) != VK_SUCCESS) {
        return false;
    }

    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &setLayout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vk.vkCreatePipelineLayout(device, &layout_info, nullptr, &layout) != VK_SUCCESS) {
        return false;
    }

    uiVertex = Shader(gUi_Vertex_Spirv, sizeof(gUi_Vertex_Spirv));
    uiPixel = Shader(gUi_Pixel_Spirv, sizeof(gUi_Pixel_Spirv));
    imageVertex = Shader(gImage_Vertex_Spirv, sizeof(gImage_Vertex_Spirv));
    imagePixel = Shader(gImage_Pixel_Spirv, sizeof(gImage_Pixel_Spirv));
    if (uiVertex == VK_NULL_HANDLE || uiPixel == VK_NULL_HANDLE || imageVertex == VK_NULL_HANDLE || imagePixel == VK_NULL_HANDLE) {
        return false;
    }

    white = Create_Texture(1, 1, Renderer_Format_Rgba8, &white_pixel);
    return white != nullptr;
}

bool Vulkan_Renderer::Start() {
    VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    u32 extension_count = 0;

    if (!SDL_Vulkan_LoadLibrary(nullptr)) {
        Log_Error("renderer", "no Vulkan loader: %s", SDL_GetError());
        return false;
    }

    vk.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    vk.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vk.vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    application.pApplicationName = "ModLoader";
    application.apiVersion = VK_API_VERSION_1_3;
    info.pApplicationInfo = &application;
    info.ppEnabledExtensionNames = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    info.enabledExtensionCount = extension_count;
    return vk.vkCreateInstance != nullptr && vk.vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS && Load_Instance_Functions() && Pick_Device() && Create_Device() && Create_Objects();
}

Vulkan_Renderer::~Vulkan_Renderer() {
    if (device != VK_NULL_HANDLE) {
        vk.vkDeviceWaitIdle(device);
        white.reset();
        for (Vulkan_Upload& upload : uploads) {
            Destroy_Buffer(upload.staging);
            vk.vkDestroyFence(device, upload.fence, nullptr);
        }

        for (const Vulkan_Pipelines& set : pipelines) {
            vk.vkDestroyPipeline(device, set.straight, nullptr);
            vk.vkDestroyPipeline(device, set.premultiplied, nullptr);
            vk.vkDestroyPipeline(device, set.image, nullptr);
        }

        for (VkShaderModule module : { uiVertex, uiPixel, imageVertex, imagePixel }) {
            vk.vkDestroyShaderModule(device, module, nullptr);
        }

        for (const Vulkan_Descriptor_Pool& descriptors : descriptorPools) {
            vk.vkDestroyDescriptorPool(device, descriptors.pool, nullptr);
        }

        vk.vkDestroyPipelineLayout(device, layout, nullptr);
        vk.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        vk.vkDestroySampler(device, sampler, nullptr);
        vk.vkDestroyCommandPool(device, pool, nullptr);
        vk.vkDestroyDevice(device, nullptr);
    }

    if (instance != VK_NULL_HANDLE) {
        vk.vkDestroyInstance(instance, nullptr);
    }
}

void Vulkan_Renderer::Gpu_Uuid(u8 out_uuid[16]) const {
    memcpy(out_uuid, uuid, 16);
}

bool Vulkan_Renderer::Memory_Type(u32 bits, VkMemoryPropertyFlags flags, u32& out_type) const {
    for (u32 index = 0; index < memory.memoryTypeCount; index++) {
        if ((bits & (1u << index)) != 0 && (memory.memoryTypes[index].propertyFlags & flags) == flags) {
            out_type = index;
            return true;
        }
    }
    return false;
}

bool Vulkan_Renderer::Create_Buffer(u64 size, VkBufferUsageFlags usage, Vulkan_Buffer& out_buffer) {
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bool created;

    out_buffer = {};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk.vkCreateBuffer(device, &info, nullptr, &out_buffer.buffer) != VK_SUCCESS) {
        return false;
    }

    vk.vkGetBufferMemoryRequirements(device, out_buffer.buffer, &requirements);
    allocation.allocationSize = requirements.size;
    created = Memory_Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, allocation.memoryTypeIndex);
    created = created && vk.vkAllocateMemory(device, &allocation, nullptr, &out_buffer.memory) == VK_SUCCESS;
    created = created && vk.vkBindBufferMemory(device, out_buffer.buffer, out_buffer.memory, 0) == VK_SUCCESS;
    created = created && vk.vkMapMemory(device, out_buffer.memory, 0, VK_WHOLE_SIZE, 0, &out_buffer.mapped) == VK_SUCCESS;
    if (!created) {
        Destroy_Buffer(out_buffer);
        return false;
    }

    out_buffer.size = size;
    return true;
}

void Vulkan_Renderer::Destroy_Buffer(Vulkan_Buffer& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
        vk.vkDestroyBuffer(device, buffer.buffer, nullptr);
        vk.vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = {};
}

VkPipeline Vulkan_Renderer::Create_Pipeline(VkFormat format, bool mesh, bool premultiplied) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }
    };
    VkVertexInputBindingDescription binding = { 0, sizeof(Renderer_Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attributes[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, 8 },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16 },
    };
    VkPipelineVertexInputStateCreateInfo input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState attachment = {};
    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipeline pipeline = VK_NULL_HANDLE;

    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = mesh ? uiVertex : imageVertex;
    stages[0].pName = "Vertex_Main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = mesh ? uiPixel : imagePixel;
    stages[1].pName = "Pixel_Main";
    input.vertexBindingDescriptionCount = mesh ? 1 : 0;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = mesh ? 3 : 0;
    input.pVertexAttributeDescriptions = attributes;
    assembly.topology = mesh ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    attachment.blendEnable = mesh ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = premultiplied ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    vk.vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    return pipeline;
}

const Vulkan_Pipelines* Vulkan_Renderer::Pipelines_For(VkFormat format) {
    Vulkan_Pipelines made;

    for (const Vulkan_Pipelines& set : pipelines) {
        if (set.format == format) {
            return &set;
        }
    }

    made = { format, Create_Pipeline(format, true, false), Create_Pipeline(format, true, true), Create_Pipeline(format, false, false) };
    if (made.straight == VK_NULL_HANDLE || made.premultiplied == VK_NULL_HANDLE || made.image == VK_NULL_HANDLE) {
        for (VkPipeline pipeline : {made.straight, made.premultiplied, made.image}) {
            vk.vkDestroyPipeline(device, pipeline, nullptr);
        }
        return nullptr;
    }

    pipelines.push_back(made);
    return &pipelines.back();
}

void Vulkan_Renderer::Barrier(
    VkCommandBuffer commands,
    VkImage image,
    VkImageLayout from,
    VkImageLayout to,
    VkPipelineStageFlags source_stage,
    VkAccessFlags source_access,
    VkPipelineStageFlags destination_stage,
    VkAccessFlags destination_access
) {
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vk.vkCmdPipelineBarrier(commands, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

Vulkan_Upload* Vulkan_Renderer::Upload_Slot(u64 size) {
    Vulkan_Upload* slot = nullptr;
    VkCommandBufferAllocateInfo allocation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    for (Vulkan_Upload& upload : uploads) {
        if (!upload.busy || vk.vkGetFenceStatus(device, upload.fence) == VK_SUCCESS) {
            slot = &upload;
            break;
        }
    }

    if (slot == nullptr) {
        slot = &uploads.emplace_back();
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        if (vk.vkAllocateCommandBuffers(device, &allocation, &slot->commands) != VK_SUCCESS ||
            vk.vkCreateFence(device, &fence, nullptr, &slot->fence) != VK_SUCCESS) {
            if (slot->commands != VK_NULL_HANDLE) {
                vk.vkFreeCommandBuffers(device, pool, 1, &slot->commands);
            }
            uploads.pop_back();
            return nullptr;
        }
    }

    if (size != 0 && slot->staging.size < size) {
        Destroy_Buffer(slot->staging);
        if (!Create_Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, slot->staging)) {
            return nullptr;
        }
    }

    slot->busy = false;
    return slot;
}

VkCommandBuffer Vulkan_Renderer::Slot_Begin(Vulkan_Upload& slot) {
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };

    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkResetCommandBuffer(slot.commands, 0);
    vk.vkBeginCommandBuffer(slot.commands, &begin);
    return slot.commands;
}

bool Vulkan_Renderer::Slot_Submit(Vulkan_Upload& slot, VkSemaphore signal, u64 value) {
    VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

    vk.vkEndCommandBuffer(slot.commands);
    vk.vkResetFences(device, 1, &slot.fence);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.commands;
    if (signal != VK_NULL_HANDLE) {
        timeline.signalSemaphoreValueCount = 1;
        timeline.pSignalSemaphoreValues = &value;
        submit.pNext = &timeline;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signal;
    }

    if (vk.vkQueueSubmit(queue, 1, &submit, slot.fence) != VK_SUCCESS) {
        return false;
    }
    slot.busy = true;
    return true;
}

std::unique_ptr<Renderer_Surface> Vulkan_Renderer::Create_Surface(SDL_Window* window) {
    auto surface = std::make_unique<Vulkan_Surface>(*this, window);
    VkBool32 supported = VK_FALSE;
    std::vector<VkSurfaceFormatKHR> formats;
    u32 format_count = 0;
    VkCommandBufferAllocateInfo allocation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface->surface)) {
        Log_Error("renderer", "cannot create Vulkan surface: %s", SDL_GetError());
        return nullptr;
    }

    if (vk.vkGetPhysicalDeviceSurfaceSupportKHR(physical, queueFamily, surface->surface, &supported) != VK_SUCCESS || !supported ||
        vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface->surface, &format_count, nullptr) != VK_SUCCESS) {
        return nullptr;
    }

    formats.resize(format_count);
    if (vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface->surface, &format_count, formats.data()) != VK_SUCCESS) {
        return nullptr;
    }

    for (u32 index = 0; index < format_count && surface->format == VK_FORMAT_UNDEFINED; index++) {
        if (formats[index].format == VK_FORMAT_B8G8R8A8_UNORM || formats[index].format == VK_FORMAT_R8G8B8A8_UNORM) {
            surface->format = formats[index].format;
        }
    }

    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (Vulkan_Frame& frame : surface->frames) {
        if (vk.vkAllocateCommandBuffers(device, &allocation, &frame.commands) != VK_SUCCESS ||
            vk.vkCreateFence(device, &fence, nullptr, &frame.fence) != VK_SUCCESS ||
            vk.vkCreateSemaphore(device, &semaphore, nullptr, &frame.acquired) != VK_SUCCESS) {
            return nullptr;
        }
    }

    if (surface->format == VK_FORMAT_UNDEFINED) {
        Log_Error("renderer", "GPU cannot present to this window");
        return nullptr;
    }

    return surface;
}
