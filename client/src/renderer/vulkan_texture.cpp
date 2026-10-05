#include "vulkan.h"

#include <string.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

static int Duplicate_Handle(u64 handle) {
    return handle <= INT_MAX ? fcntl(static_cast<int>(handle), F_DUPFD_CLOEXEC, 0) : -1;
}
#endif

u32 Vulkan_Renderer::Texture_Size_Limit() const {
    return textureSizeLimit;
}

Vulkan_Texture::~Vulkan_Texture() {
    const Vulkan_Functions& vk = renderer.vk;

    vk.vkQueueWaitIdle(renderer.queue);
    if (descriptor != VK_NULL_HANDLE) {
        vk.vkFreeDescriptorSets(renderer.device, descriptorPool->pool, 1, &descriptor);
        descriptorPool->used--;
    }

    vk.vkDestroyImageView(renderer.device, view, nullptr);
    vk.vkDestroyImage(renderer.device, image, nullptr);
    if (memory != VK_NULL_HANDLE) {
        vk.vkFreeMemory(renderer.device, memory, nullptr);
    }

    vk.vkDestroySemaphore(renderer.device, ready, nullptr);
    vk.vkDestroySemaphore(renderer.device, release, nullptr);
}

bool Vulkan_Renderer::Allocate_Descriptor(Vulkan_Texture& texture) {
    constexpr u32 batch_size = 256;
    VkDescriptorSetAllocateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };

    set.descriptorSetCount = 1;
    set.pSetLayouts = &setLayout;
    for (Vulkan_Descriptor_Pool& descriptors : descriptorPools) {
        if (descriptors.used == batch_size) {
            continue;
        }

        set.descriptorPool = descriptors.pool;
        VkResult result = vk.vkAllocateDescriptorSets(device, &set, &texture.descriptor);
        if (result == VK_SUCCESS) {
            texture.descriptorPool = &descriptors;
            descriptors.used++;
            return true;
        }

        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            return false;
        }
    }

    VkDescriptorPoolSize sizes[2] = { { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, batch_size }, { VK_DESCRIPTOR_TYPE_SAMPLER, batch_size } };
    VkDescriptorPoolCreateInfo info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorPool pool = VK_NULL_HANDLE;

    info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets = batch_size;
    info.poolSizeCount = 2;
    info.pPoolSizes = sizes;
    if (vk.vkCreateDescriptorPool(device, &info, nullptr, &pool) != VK_SUCCESS) {
        return false;
    }

    set.descriptorPool = pool;
    if (vk.vkAllocateDescriptorSets(device, &set, &texture.descriptor) != VK_SUCCESS) {
        vk.vkDestroyDescriptorPool(device, pool, nullptr);
        return false;
    }

    descriptorPools.push_back({ pool, 1 });
    texture.descriptorPool = &descriptorPools.back();
    return true;
}

bool Vulkan_Renderer::Finish_Texture(Vulkan_Texture& texture) {
    VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkDescriptorImageInfo image_descriptor = {};
    VkDescriptorImageInfo sampler_descriptor = {};
    VkWriteDescriptorSet writes[2] = { { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET } };

    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = texture.vkFormat;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vk.vkCreateImageView(device, &view, nullptr, &texture.view) != VK_SUCCESS || !Allocate_Descriptor(texture)) {
        return false;
    }

    image_descriptor.imageView = texture.view;
    image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    sampler_descriptor.sampler = sampler;
    writes[0].dstSet = texture.descriptor;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &image_descriptor;
    writes[1].dstSet = texture.descriptor;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &sampler_descriptor;
    vk.vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    return true;
}

bool Vulkan_Renderer::Upload(Vulkan_Texture& texture, u32 x, u32 y, u32 width, u32 height, const void* pixels, u32 pitch) {
    u64 row_bytes = static_cast<u64>(width) * texture.pixelBytes;
    Vulkan_Upload* slot = Upload_Slot(row_bytes * height);
    VkCommandBuffer commands;
    VkBufferImageCopy copy = {};

    if (slot == nullptr) {
        return false;
    }

    for (u32 row = 0; row < height; row++) {
        memcpy(static_cast<u8*>(slot->staging.mapped) + row * row_bytes, static_cast<const u8*>(pixels) + static_cast<u64>(row) * pitch, row_bytes);
    }

    commands = Slot_Begin(*slot);
    Barrier(
        commands,
        texture.image,
        texture.written ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT
    );

    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageOffset = { static_cast<s32>(x), static_cast<s32>(y), 0 };
    copy.imageExtent = { width, height, 1 };
    vk.vkCmdCopyBufferToImage(commands, slot->staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    Barrier(
        commands,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT
    );

    if (!Slot_Submit(*slot, VK_NULL_HANDLE, 0)) {
        return false;
    }

    texture.written = true;
    return true;
}

std::unique_ptr<Renderer_Texture> Vulkan_Renderer::Create_Texture(u32 width, u32 height, Renderer_Format format, const void* pixels) {
    std::unique_ptr<Vulkan_Texture> texture;
    VkImageCreateInfo image = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    bool created;

    if (width == 0 || height == 0 || width > textureSizeLimit || height > textureSizeLimit || width > UINT32_MAX / 8) {
        return nullptr;
    }

    texture = std::make_unique<Vulkan_Texture>(*this);
    texture->width = width;
    texture->height = height;
    texture->format = format;
    texture->vkFormat = format == Renderer_Format_Rgba16 ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    texture->pixelBytes = format == Renderer_Format_Rgba16 ? 8 : 4;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = texture->vkFormat;
    image.extent = { width, height, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    created = vk.vkCreateImage(device, &image, nullptr, &texture->image) == VK_SUCCESS;
    if (created) {
        vk.vkGetImageMemoryRequirements(device, texture->image, &requirements);
        allocation.allocationSize = requirements.size;
        created = Memory_Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, allocation.memoryTypeIndex);
        created = created && vk.vkAllocateMemory(device, &allocation, nullptr, &texture->memory) == VK_SUCCESS;
        created = created && vk.vkBindImageMemory(device, texture->image, texture->memory, 0) == VK_SUCCESS;
    }

    created = created && Finish_Texture(*texture);
    if (created && pixels != nullptr) {
        created = Upload(*texture, 0, 0, width, height, pixels, width * texture->pixelBytes);
    }

    return created ? std::move(texture) : nullptr;
}

void Vulkan_Renderer::Update_Texture(Renderer_Texture& texture, u32 x, u32 y, u32 width, u32 height, const void* pixels, u32 pitch) {
    Vulkan_Texture& target = static_cast<Vulkan_Texture&>(texture);

    if (!target.shared && pixels != nullptr && width != 0 && height != 0 && x <= target.width && y <= target.height &&
        width <= target.width - x && height <= target.height - y && static_cast<u64>(width) * target.pixelBytes <= pitch) {
        Upload(target, x, y, width, height, pixels, pitch);
    }
}

VkSemaphore Vulkan_Renderer::Shared_Semaphore(u64 handle, bool d3d12_fence) {
    VkSemaphoreTypeCreateInfo type = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    VkSemaphoreCreateInfo info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore semaphore = VK_NULL_HANDLE;
    bool imported = false;
#if defined(_WIN32)
    VkImportSemaphoreWin32HandleInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
#else
    VkImportSemaphoreFdInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR };
#endif

    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    info.pNext = &type;
    if (vk.vkCreateSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
#if defined(_WIN32)
    import.semaphore = semaphore;
    import.handleType = d3d12_fence ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT : VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    import.handle = reinterpret_cast<HANDLE>(handle);
    imported = vk.vkImportSemaphoreWin32HandleKHR(device, &import) == VK_SUCCESS;
#else
    (void)d3d12_fence;
    import.semaphore = semaphore;
    import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    import.fd = Duplicate_Handle(handle);
    if (import.fd >= 0) {
        imported = vk.vkImportSemaphoreFdKHR(device, &import) == VK_SUCCESS;
        if (!imported) {
            close(import.fd);
        }
    }
#endif
    if (!imported) {
        vk.vkDestroySemaphore(device, semaphore, nullptr);
        return VK_NULL_HANDLE;
    }
    return semaphore;
}

std::unique_ptr<Renderer_Texture> Vulkan_Renderer::Import_Texture(const ModLoader_Gpu_Image& shared, u32 width, u32 height) {
    bool known_format = shared.vkFormat == VK_FORMAT_R8G8B8A8_UNORM || shared.vkFormat == VK_FORMAT_R16G16B16A16_UNORM;
    bool d3d12 = shared.handleType == MODLOADER_HANDLE_D3D12;
    bool same_device = d3d12 ? luidValid && memcmp(shared.deviceUuid, luid, VK_LUID_SIZE) == 0 : memcmp(shared.deviceUuid, uuid, VK_UUID_SIZE) == 0;
    std::unique_ptr<Vulkan_Texture> texture;
    bool created = false;
    VkExternalMemoryImageCreateInfo external_info = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
    VkImageCreateInfo image = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkMemoryRequirements requirements;
    VkMemoryDedicatedAllocateInfo dedicated = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
#if defined(_WIN32)
    VkImportMemoryWin32HandleInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
    bool known_handle = shared.handleType == MODLOADER_HANDLE_OPAQUE_WIN32 || d3d12;
    external_info.handleTypes = d3d12 ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    VkImportMemoryFdInfoKHR import = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR };
    bool known_handle = shared.handleType == MODLOADER_HANDLE_OPAQUE_FD;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

    if (!external || !same_device || !known_format || !known_handle || width == 0 || height == 0 || width > textureSizeLimit || height > textureSizeLimit) {
        return nullptr;
    }

    texture = std::make_unique<Vulkan_Texture>(*this);
    texture->width = width;
    texture->height = height;
    texture->vkFormat = static_cast<VkFormat>(shared.vkFormat);
    texture->format = texture->vkFormat == VK_FORMAT_R16G16B16A16_UNORM ? Renderer_Format_Rgba16 : Renderer_Format_Rgba8;
    texture->pixelBytes = texture->format == Renderer_Format_Rgba16 ? 8 : 4;
    texture->shared = true;
    texture->written = true;
    texture->externalLayout = d3d12 ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture->ready = Shared_Semaphore(shared.readySemaphoreHandle, d3d12);
    texture->release = Shared_Semaphore(shared.releaseSemaphoreHandle, d3d12);
    image.pNext = &external_info;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = texture->vkFormat;
    image.extent = { width, height, 1 };
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    created = texture->ready != VK_NULL_HANDLE && texture->release != VK_NULL_HANDLE;
    created = created && vk.vkCreateImage(device, &image, nullptr, &texture->image) == VK_SUCCESS;
    if (created) {
        vk.vkGetImageMemoryRequirements(device, texture->image, &requirements);
        import.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(external_info.handleTypes);
        dedicated.image = texture->image;
        import.pNext = &dedicated;
        allocation.pNext = &import;
        allocation.allocationSize = shared.memorySize;
        created = Memory_Type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, allocation.memoryTypeIndex);
#if defined(_WIN32)
        import.handle = reinterpret_cast<HANDLE>(shared.memoryHandle);
        created = created && vk.vkAllocateMemory(device, &allocation, nullptr, &texture->memory) == VK_SUCCESS;
#else
        if (created) {
            import.fd = Duplicate_Handle(shared.memoryHandle);
            created = import.fd >= 0;
            if (created) {
                created = vk.vkAllocateMemory(device, &allocation, nullptr, &texture->memory) == VK_SUCCESS;
                if (!created) {
                    close(import.fd);
                }
            }
        }
#endif
        created = created && vk.vkBindImageMemory(device, texture->image, texture->memory, 0) == VK_SUCCESS;
    }
    if (!created || !Finish_Texture(*texture)) {
        Log_Warning("renderer", "cannot import shared frame");
        return nullptr;
    }
    return texture;
}

void Vulkan_Renderer::Ownership_Barrier(VkCommandBuffer commands, const Vulkan_Texture& texture, bool acquire) {
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

    barrier.dstAccessMask = acquire ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.oldLayout = acquire ? texture.externalLayout : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = acquire ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : texture.externalLayout;
    barrier.srcQueueFamilyIndex = acquire ? VK_QUEUE_FAMILY_EXTERNAL : queueFamily;
    barrier.dstQueueFamilyIndex = acquire ? queueFamily : VK_QUEUE_FAMILY_EXTERNAL;
    barrier.image = texture.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vk.vkCmdPipelineBarrier(
        commands,
        acquire ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        acquire ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );
}

void Vulkan_Renderer::Release_Texture(Renderer_Texture& texture, u64 value) {
    Vulkan_Texture& shared = static_cast<Vulkan_Texture&>(texture);
    VkSemaphoreSignalInfo signal = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
    Vulkan_Upload* slot;

    if (!shared.shared || value == 0) {
        return;
    }

    if (!shared.acquired) {
        signal.semaphore = shared.release;
        signal.value = value;
        vk.vkSignalSemaphore(device, &signal);
        return;
    }

    slot = Upload_Slot(0);
    if (slot != nullptr) {
        Ownership_Barrier(Slot_Begin(*slot), shared, false);
        Slot_Submit(*slot, shared.release, value);
        shared.acquired = false;
    }
}
