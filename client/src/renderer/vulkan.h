#pragma once

#include "renderer_internal.h"
#include <deque>

#define VK_NO_PROTOTYPES
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

constexpr u32 gFramesInFlight = 2;

#define VULKAN_INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties2) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFeatures2) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) \
    X(vkGetDeviceProcAddr) \
    X(vkDestroySurfaceKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define VULKAN_DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) \
    X(vkGetDeviceQueue) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkDeviceWaitIdle) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkBindImageMemory) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory) \
    X(vkMapMemory) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkGetFenceStatus) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkSignalSemaphore) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) \
    X(vkFreeDescriptorSets) \
    X(vkUpdateDescriptorSets) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdBeginRendering) \
    X(vkCmdEndRendering) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdPushConstants) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed)

#define VULKAN_DECLARE(name) PFN_##name name;

struct Vulkan_Functions {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    PFN_vkCreateInstance vkCreateInstance;
    VULKAN_INSTANCE_FUNCTIONS(VULKAN_DECLARE)
    VULKAN_DEVICE_FUNCTIONS(VULKAN_DECLARE)
#if defined(_WIN32)
    PFN_vkImportSemaphoreWin32HandleKHR vkImportSemaphoreWin32HandleKHR;
#endif
};

class Vulkan_Renderer;

struct Vulkan_Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    u64 size = 0;
};

struct Vulkan_Upload {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    Vulkan_Buffer staging;
    bool busy = false;
};

struct Vulkan_Pipelines {
    VkFormat format;
    VkPipeline straight;
    VkPipeline premultiplied;
    VkPipeline image;
};

struct Vulkan_Descriptor_Pool {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    u32 used = 0;
};

class Vulkan_Texture final : public Renderer_Texture {
public:
    explicit Vulkan_Texture(Vulkan_Renderer& owner)
        : renderer(owner) {
    }
    ~Vulkan_Texture() override;

    Vulkan_Renderer& renderer;
    VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
    u32 pixelBytes = 4;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    Vulkan_Descriptor_Pool* descriptorPool = nullptr;
    bool written = false;
    bool shared = false;
    bool acquired = false;
    VkImageLayout externalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSemaphore ready = VK_NULL_HANDLE;
    VkSemaphore release = VK_NULL_HANDLE;
};

struct Vulkan_Frame {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    std::vector<Vulkan_Buffer> chunks;
    usize chunkCount = 0;
    u64 used = 0;

    bool Space(Vulkan_Renderer& renderer, u64 size, VkBuffer& out_buffer, u64& out_offset, void*& out_data);
};

class Vulkan_Surface final : public Renderer_Surface {
public:
    Vulkan_Surface(Vulkan_Renderer& owner, SDL_Window* shown_in) : renderer(owner), window(shown_in) {}
    ~Vulkan_Surface() override;

    bool Create_Swapchain();
    void Release_Swapchain();

    Vulkan_Renderer& renderer;
    SDL_Window* window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> finished;
    u32 imageIndex = 0;
    Vulkan_Frame frames[gFramesInFlight];
    u32 frameIndex = 0;
    bool vsync = false;
    bool stale = true;
};

class Vulkan_Renderer final : public Renderer {
public:
    ~Vulkan_Renderer() override;
    bool Start();

    void Gpu_Uuid(u8 out_uuid[16]) const override;
    u32 Texture_Size_Limit() const override;
    std::unique_ptr<Renderer_Surface> Create_Surface(SDL_Window* window) override;
    std::unique_ptr<Renderer_Texture> Create_Texture(u32 width, u32 height, Renderer_Format format, const void* pixels) override;
    void Update_Texture(Renderer_Texture& texture, u32 x, u32 y, u32 width, u32 height, const void* pixels, u32 pitch) override;
    std::unique_ptr<Renderer_Texture> Import_Texture(const ModLoader_Gpu_Image& image, u32 width, u32 height) override;
    void Release_Texture(Renderer_Texture& texture, u64 value) override;
    bool Begin(Renderer_Surface& surface, u32& out_width, u32& out_height) override;
    void Draw_Image(const Renderer_Image& image) override;
    void Draw_Mesh(const Renderer_Mesh& mesh) override;
    void End() override;

    bool Memory_Type(u32 bits, VkMemoryPropertyFlags flags, u32& out_type) const;
    bool Create_Buffer(u64 size, VkBufferUsageFlags usage, Vulkan_Buffer& out_buffer);
    void Destroy_Buffer(Vulkan_Buffer& buffer);
    void Barrier(
        VkCommandBuffer commands,
        VkImage image,
        VkImageLayout from,
        VkImageLayout to,
        VkPipelineStageFlags source_stage,
        VkAccessFlags source_access,
        VkPipelineStageFlags destination_stage,
        VkAccessFlags destination_access
    );
    void Ownership_Barrier(VkCommandBuffer commands, const Vulkan_Texture& texture, bool acquire);
    Vulkan_Upload* Upload_Slot(u64 size);
    VkCommandBuffer Slot_Begin(Vulkan_Upload& slot);
    bool Slot_Submit(Vulkan_Upload& slot, VkSemaphore signal, u64 value);
    bool Finish_Texture(Vulkan_Texture& texture);
    bool Allocate_Descriptor(Vulkan_Texture& texture);
    bool Upload(Vulkan_Texture& texture, u32 x, u32 y, u32 width, u32 height, const void* pixels, u32 pitch);
    VkSemaphore Shared_Semaphore(u64 handle, bool d3d12_fence);
    const Vulkan_Pipelines* Pipelines_For(VkFormat format);
    void Begin_Rendering(VkAttachmentLoadOp load);
    void Ensure_Rendering();

    Vulkan_Functions vk = {};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory = {};
    u32 textureSizeLimit = 0;
    u8 uuid[VK_UUID_SIZE] = {};
    u8 luid[VK_LUID_SIZE] = {};
    bool luidValid = false;
    bool external = false;
    VkDevice device = VK_NULL_HANDLE;
    u32 queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::deque<Vulkan_Descriptor_Pool> descriptorPools;
    VkShaderModule uiVertex = VK_NULL_HANDLE;
    VkShaderModule uiPixel = VK_NULL_HANDLE;
    VkShaderModule imageVertex = VK_NULL_HANDLE;
    VkShaderModule imagePixel = VK_NULL_HANDLE;
    std::vector<Vulkan_Pipelines> pipelines;
    std::vector<Vulkan_Upload> uploads;
    std::unique_ptr<Renderer_Texture> white;
    Vulkan_Surface* current = nullptr;
    const Vulkan_Pipelines* currentPipelines = nullptr;
    bool rendering = false;
    std::vector<VkSemaphore> waits;
    std::vector<u64> waitValues;
    std::vector<VkPipelineStageFlags> waitStages;

private:
    bool Load_Instance_Functions();
    bool Load_Device_Functions();
    bool Has_External_Extensions();
    bool Pick_Device();
    bool Create_Device();
    bool Create_Objects();
    VkShaderModule Shader(const u8* code, usize size);
    VkPipeline Create_Pipeline(VkFormat format, bool mesh, bool premultiplied);
};
