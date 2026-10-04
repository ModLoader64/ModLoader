#include "presenter.h"

#include "ui/toolbar.h"
#include "ui/ui.h"
#include "ui/windows.h"

#include <SDL3/SDL.h>

#include <string.h>

#include <algorithm>
#include <utility>

namespace {
constexpr u64 gReleaseMargin = 64;
} // namespace

std::unique_ptr<Presenter> Presenter::Create(const Config& config) {
    std::unique_ptr<Presenter> presenter(new Presenter());
    f32 scale;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        Log_Error("window", "SDL_Init failed: %s", SDL_GetError());
        return nullptr;
    }

    presenter->Set_Picture(config.scaling, config.filter);
    presenter->renderer = Renderer::Create();
    scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    scale = scale > 0.0f ? scale : 1.0f;
    if (presenter->renderer != nullptr) {
        presenter->window = SDL_CreateWindow(
            config.title.c_str(),
            static_cast<int>(static_cast<f32>(config.windowWidth) * scale),
            static_cast<int>(static_cast<f32>(config.windowHeight) * scale),
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN
        );
    }

    if (presenter->window != nullptr) {
        presenter->surface = presenter->renderer->Create_Surface(presenter->window);
    }

    if (presenter->surface == nullptr) {
        Log_Error("window", "cannot create the window: %s", SDL_GetError());
        return nullptr;
    }

    presenter->ui = std::make_unique<Ui>(*presenter->renderer, presenter->window, config.dataDirectory, config.theme);
    SDL_AddEventWatch(Live_Update, presenter.get());
    Ui_Windows_Start(*presenter->renderer, presenter->window);
    presenter->wakeEvent = SDL_RegisterEvents(1);
    presenter->Start_Audio();
    return presenter;
}

Presenter::~Presenter() {
    SDL_RemoveEventWatch(Live_Update, this);
    if (audio != nullptr) {
        SDL_DestroyAudioStream(audio);
    }

    if (gamepad != nullptr) {
        SDL_CloseGamepad(gamepad);
    }

    if (ui != nullptr) {
        Ui_Windows_Stop();
        ui.reset();
    }

    if (renderer != nullptr) {
        Release_Frames();
        for (Shared_Import& import : imports) {
            import.texture.reset();
        }
        texture.reset();
        surface.reset();
        renderer.reset();
    }

    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    
    SDL_Quit();
}

void Presenter::Gpu_Uuid(u8 out_uuid[16]) const {
    renderer->Gpu_Uuid(out_uuid);
}

void Presenter::Release_Frames() {
    u64 last = pending.shared ? pending.image.releaseValue : 0;
    pending.ready = false;
    pending.shared = false;

    last = std::max({ last, shownRelease, skippedRelease });
    if (shown != nullptr && last != 0) {
        renderer->Release_Texture(*shown, last + gReleaseMargin);
        released = last + gReleaseMargin;
        shownRelease = 0;
    }
}

void Presenter::Wake() {
    SDL_Event event = {};

    event.type = wakeEvent;
    SDL_PushEvent(&event);
}

void Presenter::Submit_Frame(const ModLoader_Frame& frame) {
    u64 row_bytes = static_cast<u64>(frame.width) * (frame.format == MODLOADER_PIXELS_RGBA16 ? 8 : 4);
    bool was_ready;

    if (frame.width == 0 || frame.height == 0 || (frame.kind == MODLOADER_FRAME_CPU && frame.pixels == nullptr) || frame.kind > MODLOADER_FRAME_GPU_SHARED ||
        frame.format > MODLOADER_PIXELS_RGBA16) {
        return;
    }

    {
        std::lock_guard lock(frameLock);

        if (frame.kind == MODLOADER_FRAME_GPU_SHARED && frame.gpu.releaseSemaphoreHandle != pending.image.releaseSemaphoreHandle) {
            skippedRelease = 0;
        }
        else if (pending.ready && pending.shared && pending.image.releaseValue > skippedRelease) {
            skippedRelease = pending.image.releaseValue;
        }

        pending.shared = frame.kind == MODLOADER_FRAME_GPU_SHARED;
        pending.image = frame.gpu;
        pending.pixels.resize(pending.shared ? 0 : row_bytes * frame.height);
        for (u32 row = 0; !pending.shared && row < frame.height; row++) {
            memcpy(pending.pixels.data() + row * row_bytes, frame.pixels + static_cast<u64>(row) * frame.pitch, row_bytes);
        }

        pending.width = frame.width;
        pending.height = frame.height;
        pending.format = frame.format == MODLOADER_PIXELS_RGBA16 ? Renderer_Format_Rgba16 : Renderer_Format_Rgba8;
        pending.sourceWidth = frame.sourceWidth != 0 ? frame.sourceWidth : frame.width;
        pending.sourceHeight = frame.sourceHeight != 0 ? frame.sourceHeight : frame.height;
        pending.aspect = frame.displayAspectDenominator != 0 ? static_cast<f32>(frame.displayAspectNumerator) / frame.displayAspectDenominator : 4.0f / 3.0f;
        was_ready = pending.ready;
        pending.ready = true;
    }

    if (!was_ready) {
        Wake();
    }
}

void Presenter::Output_Size(u32& out_width, u32& out_height) const {
    out_width = outputWidth;
    out_height = outputHeight;
}

Renderer_Texture* Presenter::Shared_Texture(const ModLoader_Gpu_Image& image, u32 width, u32 height) {
    Shared_Import* slot = &imports[0];

    for (Shared_Import& import : imports) {
        if (import.texture != nullptr && import.imageId == image.imageId && import.texture->width == width && import.texture->height == height) {
            import.lastUse = ++importUses;
            return import.texture.get();
        }
        slot = import.lastUse < slot->lastUse ? &import : slot;
    }

    if (slot->texture != nullptr && slot->texture.get() == shown) {
        return nullptr;
    }
    slot->texture = renderer->Import_Texture(image, width, height);
    slot->imageId = image.imageId;
    slot->lastUse = ++importUses;
    return slot->texture.get();
}

void Presenter::Take_Frame() {
    ModLoader_Gpu_Image image;
    Renderer_Texture* taken;
    u64 release;
    bool shared;
    u32 width;
    u32 height;

    {
        std::lock_guard lock(frameLock);

        if (!pending.ready) {
            return;
        }

        shared = pending.shared;
        image = pending.image;
        width = pending.width;
        height = pending.height;
        release = std::exchange(skippedRelease, 0);
        if (!shared && (texture == nullptr || texture->width != width || texture->height != height || texture->format != pending.format)) {
            texture.reset();
            texture = renderer->Create_Texture(width, height, pending.format, pending.pixels.data());
        }
        else if (!shared) {
            renderer->Update_Texture(*texture, 0, 0, width, height, pending.pixels.data(), width * (pending.format == Renderer_Format_Rgba16 ? 8 : 4));
        }

        sourceWidth = pending.sourceWidth;
        sourceHeight = pending.sourceHeight;
        aspect = pending.aspect;
        pending.ready = false;
    }

    taken = shared ? Shared_Texture(image, width, height) : texture.get();
    if (shared && image.releaseSemaphoreHandle != releaseTimeline) {
        if (shown != nullptr && shownRelease > released) {
            renderer->Release_Texture(*shown, shownRelease);
        }
        shownRelease = 0;
        released = 0;
        releaseTimeline = image.releaseSemaphoreHandle;
    }

    release = std::max(release, shownRelease);
    if (taken == nullptr && shared) {
        release = std::max(release, image.releaseValue);
    }

    Renderer_Texture* releasing = shownRelease != 0 ? shown : taken;
    if (release > released && releasing != nullptr) {
        renderer->Release_Texture(*releasing, release);
        released = release;
    }

    shown = taken;
    shownReady = shared && taken != nullptr ? image.readyValue : 0;
    shownRelease = shared && taken != nullptr ? image.releaseValue : 0;
}

bool Presenter::Present() {
    Renderer_Image image = {};
    u32 width;
    u32 height;
    u32 top;

    renderer->Begin_Frame();
    Take_Frame();
    ui->Update_Textures();
    if (renderer->Begin(*surface, width, height)) {
        if (shown != nullptr) {
            image.texture = shown;
            image.filter = filter;
            image.readyValue = std::exchange(shownReady, 0);
            top = ui->Top_Inset();
            top = top < height ? top : 0;
            Renderer_Place_Image(scaling, width, height - top, sourceWidth, sourceHeight, aspect, image.destination);
            image.destination[1] += static_cast<f32>(top);
            renderer->Draw_Image(image);
            outputWidth = width;
            outputHeight = height - top;
        }
        Ui_Windows_Draw_Overlays(*renderer);
        ui->Draw();
        renderer->End();
    }

    Ui_Windows_Present(*renderer);
    ui->Draw_Windows();
    return renderer->Frame_Presented();
}

bool Presenter::Draw_Frame() {
    // Updating native viewports can send more resize events.
    drawingFrame = true;
    ui->Frame([this] {
        Build_Frame();
    });
    bool presented = Present();
    drawingFrame = false;
    return presented;
}

bool Presenter::Live_Update(void* user, SDL_Event* event) {
    Presenter& presenter = *static_cast<Presenter*>(user);
    bool live = (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == 1) || event->type == SDL_EVENT_WINDOW_MOVED || event->type == SDL_EVENT_WINDOW_RESIZED;

    if (live && SDL_IsMainThread() && !presenter.drawingFrame) {
        presenter.ui->Process_Event(*event);
        presenter.Draw_Frame();
    }
    return true;
}

void Presenter::Set_Status(std::string_view text) {
    std::lock_guard lock(frameLock);
    status = text;
}

void Presenter::Build_Frame() {
    std::string line;
    {
        std::lock_guard lock(frameLock);

        line = status;
    }

    if (!line.empty()) {
        Ui::Status(line.c_str());
    }

    if (uiBuild) {
        uiBuild();
    }
}

void Presenter::Set_Picture(Renderer_Scaling new_scaling, Renderer_Filter new_filter) {
    scaling = new_scaling;
    filter = new_filter;
}

void Presenter::Set_Theme(Ui_Theme theme) {
    ui->Set_Theme(theme);
}

bool Presenter::Pump() {
    bool keep_running = true;
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (Toolbar_Capture_Event(event) || Ui_Windows_Event(event)) {
            continue;
        }

        if (ui->Process_Event(event)) {
            Ui_Windows_Main_Event(event);
        }
        else if (event.type == SDL_EVENT_KEY_DOWN) {
            continue;
        }

        switch (event.type) {
        case SDL_EVENT_QUIT:
            keep_running = false;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            keep_running = keep_running && event.window.windowID != SDL_GetWindowID(window);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (gamepad == nullptr) {
                gamepad = SDL_OpenGamepad(event.gdevice.which);
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad != nullptr && SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!event.key.repeat && event.key.scancode == SDL_SCANCODE_F5) {
                requested = Presenter_Request::Save_State;
            }
            else if (!event.key.repeat && event.key.scancode == SDL_SCANCODE_F7) {
                requested = Presenter_Request::Load_State;
            }
            Hotkey_Event(event);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            Hotkey_Event(event);
            break;
        default:
            break;
        }
    }

    Update_Input();
    if (keep_running) {
        if (!Draw_Frame()) {
            // No visible window can pace rendering.
            SDL_WaitEventTimeout(nullptr, 100);
        }
    }

    return keep_running;
}
