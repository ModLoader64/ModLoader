#include "vulkan.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <algorithm>

namespace {
constexpr u64 gFirstChunk = 1 << 20;
} // namespace

Vulkan_Surface::~Vulkan_Surface() {
    const Vulkan_Functions& vk = renderer.vk;

    vk.vkDeviceWaitIdle(renderer.device);
    Release_Swapchain();
    for (Vulkan_Frame& frame : frames) {
        for (Vulkan_Buffer& chunk : frame.chunks) {
            renderer.Destroy_Buffer(chunk);
        }

        vk.vkDestroyFence(renderer.device, frame.fence, nullptr);
        vk.vkDestroySemaphore(renderer.device, frame.acquired, nullptr);
        if (frame.commands != VK_NULL_HANDLE) {
            vk.vkFreeCommandBuffers(renderer.device, renderer.pool, 1, &frame.commands);
        }
    }

    if (swapchain != VK_NULL_HANDLE) {
        vk.vkDestroySwapchainKHR(renderer.device, swapchain, nullptr);
    }

    if (surface != VK_NULL_HANDLE) {
        vk.vkDestroySurfaceKHR(renderer.instance, surface, nullptr);
    }
}

void Vulkan_Surface::Release_Swapchain() {
    for (VkImageView view : views) {
        renderer.vk.vkDestroyImageView(renderer.device, view, nullptr);
    }

    for (VkSemaphore semaphore : finished) {
        renderer.vk.vkDestroySemaphore(renderer.device, semaphore, nullptr);
    }

    views.clear();
    finished.clear();
    images.clear();
}

bool Vulkan_Surface::Create_Swapchain() {
    const Vulkan_Functions& vk = renderer.vk;
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkPresentModeKHR> modes;
    u32 mode_count = 0;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkSwapchainKHR old = swapchain;
    VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkSemaphoreCreateInfo semaphore = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    s32 width = 0;
    s32 height = 0;
    u32 image_count;

    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.physical, surface, &capabilities) != VK_SUCCESS) {
        return false;
    }

    if (capabilities.currentExtent.width != 0xFFFFFFFF) {
        width = capabilities.currentExtent.width;
        height = capabilities.currentExtent.height;
    }

    if (width <= 0 || height <= 0) {
        return false;
    }

    if (static_cast<u32>(width) < capabilities.minImageExtent.width || static_cast<u32>(width) > capabilities.maxImageExtent.width ||
        static_cast<u32>(height) < capabilities.minImageExtent.height || static_cast<u32>(height) > capabilities.maxImageExtent.height) {
        return false;
    }

    if (!vsync) {
        if (vk.vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physical, surface, &mode_count, nullptr) != VK_SUCCESS) {
            return false;
        }

        modes.resize(mode_count);
        if (vk.vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physical, surface, &mode_count, modes.data()) != VK_SUCCESS) {
            return false;
        }

        modes.resize(mode_count);
        for (VkPresentModeKHR wanted : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR }) {
            if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
                mode = wanted;
                break;
            }
        }
    }

    vk.vkDeviceWaitIdle(renderer.device);
    Release_Swapchain();
    image_count = std::max(mode == VK_PRESENT_MODE_FIFO_KHR ? 2u : 3u, capabilities.minImageCount);
    info.surface = surface;
    info.minImageCount = capabilities.maxImageCount != 0 ? std::min(image_count, capabilities.maxImageCount) : image_count;
    info.imageFormat = format;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = { static_cast<u32>(width), static_cast<u32>(height) };
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;

    if (vk.vkCreateSwapchainKHR(renderer.device, &info, nullptr, &swapchain) != VK_SUCCESS) {
        swapchain = VK_NULL_HANDLE;
    }

    if (old != VK_NULL_HANDLE) {
        vk.vkDestroySwapchainKHR(renderer.device, old, nullptr);
    }

    if (swapchain == VK_NULL_HANDLE) {
        return false;
    }

    if (vk.vkGetSwapchainImagesKHR(renderer.device, swapchain, &image_count, nullptr) != VK_SUCCESS || image_count == 0) {
        return false;
    }

    images.resize(image_count);
    if (vk.vkGetSwapchainImagesKHR(renderer.device, swapchain, &image_count, images.data()) != VK_SUCCESS) {
        return false;
    }

    images.resize(image_count);
    views.resize(image_count);
    finished.resize(image_count);
    for (u32 index = 0; index < image_count; index++) {
        view.image = images[index];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vk.vkCreateImageView(renderer.device, &view, nullptr, &views[index]) != VK_SUCCESS ||
            vk.vkCreateSemaphore(renderer.device, &semaphore, nullptr, &finished[index]) != VK_SUCCESS) {
            Release_Swapchain();
            return false;
        }
    }

    extent = info.imageExtent;
    stale = false;
    return true;
}

bool Vulkan_Frame::Space(Vulkan_Renderer& renderer, u64 size, VkBuffer& out_buffer, u64& out_offset, void*& out_data) {
    Vulkan_Buffer* chunk = chunkCount != 0 ? &chunks[chunkCount - 1] : nullptr;
    u64 capacity = chunk != nullptr && chunk->size <= UINT64_MAX / 2 ? chunk->size * 2 : gFirstChunk;

    if (size > UINT64_MAX - 15) {
        return false;
    }

    size = (size + 15) & ~15ull;
    if (chunk == nullptr || size > chunk->size - used) {
        if (chunkCount == chunks.size()) {
            chunks.emplace_back();
        }

        chunk = &chunks[chunkCount];
        if (chunk->size < size) {
            while (capacity < size) {
                capacity = capacity <= UINT64_MAX / 2 ? capacity * 2 : size;
            }
            renderer.Destroy_Buffer(*chunk);
            if (!renderer.Create_Buffer(capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, *chunk)) {
                return false;
            }
        }
        chunkCount++;
        used = 0;
    }

    out_buffer = chunk->buffer;
    out_offset = used;
    out_data = static_cast<u8*>(chunk->mapped) + used;
    used += size;
    return true;
}

void Vulkan_Renderer::Begin_Rendering(VkAttachmentLoadOp load) {
    VkCommandBuffer commands = current->frames[current->frameIndex].commands;
    VkRenderingAttachmentInfo attachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    VkViewport viewport = {};

    attachment.imageView = current->views[current->imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = load;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    info.renderArea = { { 0, 0 }, current->extent };
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &attachment;
    vk.vkCmdBeginRendering(commands, &info);
    viewport.width = static_cast<f32>(current->extent.width);
    viewport.height = static_cast<f32>(current->extent.height);
    viewport.maxDepth = 1.0f;
    vk.vkCmdSetViewport(commands, 0, 1, &viewport);
    rendering = true;
}

void Vulkan_Renderer::Ensure_Rendering() {
    if (!rendering) {
        Begin_Rendering(VK_ATTACHMENT_LOAD_OP_CLEAR);
    }
}

bool Vulkan_Renderer::Begin(Renderer_Surface& drawn, u32& out_width, u32& out_height) {
    Vulkan_Surface& surface = static_cast<Vulkan_Surface&>(drawn);
    Vulkan_Frame& frame = surface.frames[surface.frameIndex];
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    s32 width = 0;
    s32 height = 0;
    VkResult result;
    bool vsync = !framePresented;

    if (SDL_GetWindowFlags(surface.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
        return false;
    }

    SDL_GetWindowSizeInPixels(surface.window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }

    surface.stale = surface.stale || surface.vsync != vsync || static_cast<u32>(width) != surface.extent.width || static_cast<u32>(height) != surface.extent.height;
    surface.vsync = vsync;
    if (surface.stale && !surface.Create_Swapchain()) {
        return false;
    }

    vk.vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    result = vk.vkAcquireNextImageKHR(device, surface.swapchain, UINT64_MAX, frame.acquired, VK_NULL_HANDLE, &surface.imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        surface.stale = true;
        return false;
    }

    currentPipelines = Pipelines_For(surface.format);
    if ((result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) || currentPipelines == nullptr) {
        return false;
    }

    vk.vkResetFences(device, 1, &frame.fence);
    frame.chunkCount = frame.chunkCount != 0 ? 1 : 0;
    frame.used = 0;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk.vkResetCommandBuffer(frame.commands, 0);
    vk.vkBeginCommandBuffer(frame.commands, &begin);
    Barrier(
        frame.commands,
        surface.images[surface.imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    );
    current = &surface;
    rendering = false;
    waits.assign(1, frame.acquired);
    waitValues.assign(1, 0);
    waitStages.assign(1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    out_width = surface.extent.width;
    out_height = surface.extent.height;
    return true;
}

void Vulkan_Renderer::Draw_Image(const Renderer_Image& image) {
    VkCommandBuffer commands = current->frames[current->frameIndex].commands;
    Vulkan_Texture* texture = static_cast<Vulkan_Texture*>(image.texture);
    VkRect2D scissor = { { 0, 0 }, current->extent };
    Image_Constants constants;

    if (texture == nullptr || !texture->written || image.destination[2] <= 0.0f || image.destination[3] <= 0.0f) {
        return;
    }

    if (texture->shared && !texture->acquired) {
        if (waits.size() == UINT32_MAX) {
            return;
        }

        if (rendering) {
            vk.vkCmdEndRendering(commands);
            rendering = false;
            Ownership_Barrier(commands, *texture, true);
            Begin_Rendering(VK_ATTACHMENT_LOAD_OP_LOAD);
        }
        else {
            Ownership_Barrier(commands, *texture, true);
        }

        waits.push_back(texture->ready);
        waitValues.push_back(image.readyValue);
        waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        texture->acquired = true;
    }

    Ensure_Rendering();
    constants = Image_Constants_For(image, current->extent.width, current->extent.height);
    vk.vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipelines->image);
    vk.vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &texture->descriptor, 0, nullptr);
    vk.vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    vk.vkCmdSetScissor(commands, 0, 1, &scissor);
    vk.vkCmdDraw(commands, 4, 1, 0, 0);
}

void Vulkan_Renderer::Draw_Mesh(const Renderer_Mesh& mesh) {
    Vulkan_Frame& frame = current->frames[current->frameIndex];
    u64 vertex_bytes = mesh.vertices.size_bytes();
    u64 index_bytes = mesh.indices.size_bytes();
    f32 transform[16];
    VkBuffer buffer;
    u64 offset;
    void* data;
    VkDeviceSize vertex_offset;
    const Vulkan_Texture* texture;
    VkRect2D scissor;
    s64 right;
    s64 bottom;

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.draws.empty() || !frame.Space(*this, vertex_bytes + index_bytes, buffer, offset, data)) {
        return;
    }

    Ensure_Rendering();
    memcpy(data, mesh.vertices.data(), vertex_bytes);
    memcpy(static_cast<u8*>(data) + vertex_bytes, mesh.indices.data(), index_bytes);
    Mesh_Transform(mesh, current->extent.width, current->extent.height, transform);
    vertex_offset = offset;
    vk.vkCmdBindPipeline(frame.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, mesh.premultiplied ? currentPipelines->premultiplied : currentPipelines->straight);
    vk.vkCmdBindVertexBuffers(frame.commands, 0, 1, &buffer, &vertex_offset);
    vk.vkCmdBindIndexBuffer(frame.commands, buffer, offset + vertex_bytes, VK_INDEX_TYPE_UINT32);
    vk.vkCmdPushConstants(frame.commands, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform), transform);
    for (const Renderer_Draw& draw : mesh.draws) {
        texture = static_cast<const Vulkan_Texture*>(draw.texture != nullptr ? draw.texture : white.get());
        scissor.offset = { std::max(draw.scissor[0], 0), std::max(draw.scissor[1], 0) };
        right = std::min<s64>(static_cast<s64>(draw.scissor[0]) + draw.scissor[2], current->extent.width);
        bottom = std::min<s64>(static_cast<s64>(draw.scissor[1]) + draw.scissor[3], current->extent.height);

        if (right <= scissor.offset.x || bottom <= scissor.offset.y || !texture->written || (texture->shared && !texture->acquired)) {
            continue;
        }

        scissor.extent = { static_cast<u32>(right - scissor.offset.x), static_cast<u32>(bottom - scissor.offset.y) };
        vk.vkCmdSetScissor(frame.commands, 0, 1, &scissor);
        vk.vkCmdBindDescriptorSets(frame.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &texture->descriptor, 0, nullptr);
        vk.vkCmdDrawIndexed(frame.commands, draw.indexCount, 1, draw.indexOffset, static_cast<s32>(draw.vertexOffset), 0);
    }
}

void Vulkan_Renderer::End() {
    Vulkan_Surface& surface = *current;
    Vulkan_Frame& frame = surface.frames[surface.frameIndex];
    u64 finished_value = 0;
    VkTimelineSemaphoreSubmitInfo timeline = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    VkResult result;

    Ensure_Rendering();
    vk.vkCmdEndRendering(frame.commands);
    Barrier(
        frame.commands,
        surface.images[surface.imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0
    );
    
    vk.vkEndCommandBuffer(frame.commands);
    timeline.waitSemaphoreValueCount = static_cast<u32>(waits.size());
    timeline.pWaitSemaphoreValues = waitValues.data();
    timeline.signalSemaphoreValueCount = 1;
    timeline.pSignalSemaphoreValues = &finished_value;
    submit.pNext = &timeline;
    submit.waitSemaphoreCount = static_cast<u32>(waits.size());
    submit.pWaitSemaphores = waits.data();
    submit.pWaitDstStageMask = waitStages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.commands;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &surface.finished[surface.imageIndex];
    vk.vkQueueSubmit(queue, 1, &submit, frame.fence);
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &surface.finished[surface.imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &surface.swapchain;
    present.pImageIndices = &surface.imageIndex;
    result = vk.vkQueuePresentKHR(queue, &present);
    framePresented = framePresented || result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
    surface.stale = surface.stale || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR;
    surface.frameIndex = (surface.frameIndex + 1) % gFramesInFlight;
    current = nullptr;
    rendering = false;
}
