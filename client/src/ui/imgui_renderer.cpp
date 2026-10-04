#include "ui/imgui_renderer.h"
#include "imgui.h"
#include <SDL3/SDL.h>
#include <stddef.h>

namespace {

static_assert(sizeof(ImDrawVert) == sizeof(Renderer_Vertex) && offsetof(ImDrawVert, uv) == offsetof(Renderer_Vertex, u) && offsetof(ImDrawVert, col) == offsetof(Renderer_Vertex, color));
static_assert(sizeof(ImDrawIdx) == sizeof(u32));

Renderer& Renderer_Of_Context() {
    return *static_cast<Renderer*>(ImGui::GetIO().BackendRendererUserData);
}

Renderer_Texture* Texture_Of(ImTextureID id) {
    return reinterpret_cast<Renderer_Texture*>(static_cast<uintptr_t>(id));
}

void Update_Texture(Renderer& renderer, ImTextureData* texture) {
    Renderer_Texture* handle = Texture_Of(texture->TexID);

    if (texture->Status == ImTextureStatus_WantCreate) {
        handle = renderer.Create_Texture(texture->Width, texture->Height, Renderer_Format_Rgba8, texture->Pixels).release();
        texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(handle)));
        texture->SetStatus(ImTextureStatus_OK);
    }
    else if (texture->Status == ImTextureStatus_WantUpdates && handle != nullptr) {
        for (const ImTextureRect& rect : texture->Updates) {
            renderer.Update_Texture(*handle, rect.x, rect.y, rect.w, rect.h, texture->GetPixelsAt(rect.x, rect.y), texture->GetPitch());
        }
        texture->SetStatus(ImTextureStatus_OK);
    }
    else if (texture->Status == ImTextureStatus_WantDestroy) {
        delete handle;
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
    }
}

void Create_Window(ImGuiViewport* viewport) {
    SDL_Window* window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(reinterpret_cast<intptr_t>(viewport->PlatformHandle)));
    viewport->RendererUserData = window != nullptr ? Renderer_Of_Context().Create_Surface(window).release() : nullptr;
}

void Destroy_Window(ImGuiViewport* viewport) {
    delete static_cast<Renderer_Surface*>(viewport->RendererUserData);
    viewport->RendererUserData = nullptr;
}

void Render_Window(ImGuiViewport* viewport, void*) {
    Renderer& renderer = Renderer_Of_Context();
    Renderer_Surface* surface = static_cast<Renderer_Surface*>(viewport->RendererUserData);
    u32 width;
    u32 height;

    if (surface != nullptr && renderer.Begin(*surface, width, height)) {
        Imgui_Renderer_Draw(viewport->DrawData);
        renderer.End();
    }
}

} // namespace

void Imgui_Renderer_Init(Renderer& renderer) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

    io.BackendRendererName = "modloader";
    io.BackendRendererUserData = &renderer;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasViewports;
    platform.Renderer_TextureMaxWidth = static_cast<int>(renderer.Texture_Size_Limit());
    platform.Renderer_TextureMaxHeight = static_cast<int>(renderer.Texture_Size_Limit());
    platform.Renderer_CreateWindow = Create_Window;
    platform.Renderer_DestroyWindow = Destroy_Window;
    platform.Renderer_RenderWindow = Render_Window;
}

void Imgui_Renderer_Shutdown() {
    Renderer& renderer = Renderer_Of_Context();

    ImGui::DestroyPlatformWindows();
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->RefCount == 1 && texture->TexID != ImTextureID_Invalid) {
            texture->SetStatus(ImTextureStatus_WantDestroy);
            Update_Texture(renderer, texture);
        }
    }

    ImGui::GetIO().BackendRendererUserData = nullptr;
}

void Imgui_Renderer_Update_Textures() {
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        Update_Texture(Renderer_Of_Context(), texture);
    }
}

void Imgui_Renderer_Draw(ImDrawData* data) {
    const f32 scale[16] = {
        data->FramebufferScale.x, 0.0f, 0.0f, 0.0f, 0.0f, data->FramebufferScale.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    Renderer_Mesh mesh;
    std::vector<Renderer_Draw> draws;
    ImVec2 low;
    ImVec2 high;

    if (data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f) {
        return;
    }

    mesh.translation[0] = -data->DisplayPos.x;
    mesh.translation[1] = -data->DisplayPos.y;
    mesh.transform = scale;
    for (const ImDrawList* list : data->CmdLists) {
        draws.clear();
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                continue; // modules cannot hand the client callbacks to run
            }

            low = ImVec2(
                (command.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x,
                (command.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y
            );
            high = ImVec2(
                (command.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x,
                (command.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y
            );

            if (high.x <= low.x || high.y <= low.y) {
                continue;
            }
            draws.push_back(
                {
                    Texture_Of(command.GetTexID()),
                    { static_cast<s32>(low.x), static_cast<s32>(low.y), static_cast<s32>(high.x - low.x), static_cast<s32>(high.y - low.y) },
                    command.IdxOffset,
                    command.ElemCount,
                    command.VtxOffset,
                }
            );
        }
        mesh.vertices = { reinterpret_cast<const Renderer_Vertex*>(list->VtxBuffer.Data), static_cast<usize>(list->VtxBuffer.Size) };
        mesh.indices = { list->IdxBuffer.Data, static_cast<usize>(list->IdxBuffer.Size) };
        mesh.draws = draws;
        Renderer_Of_Context().Draw_Mesh(mesh);
    }
}
