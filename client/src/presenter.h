#pragma once

#include "base.h"
#include "modloader_video.h"
#include "renderer/renderer.h"
#include "ui/theme.h"

#include <atomic>
#include <memory>

class Platform;
struct SDL_Window;
struct SDL_AudioStream;
struct SDL_Gamepad;
class Ui;

enum class Presenter_Request : u32 {
    None = 0,
    Save_State = 1,
    Load_State = 2,
};

// Bindings use settings hotkeys.<name>
enum class Hotkey : u32 {
    Pause,
    Restart,
    Unthrottled,
    Count,
};

class Presenter {
public:
    struct Config {
        std::string title;
        u32 windowWidth = 960;
        u32 windowHeight = 720;
        Renderer_Scaling scaling = Renderer_Scaling_Fit;
        Renderer_Filter filter = Renderer_Filter_Sharp_Bilinear;
        Ui_Theme theme = Ui_Theme::Dark;
        std::string dataDirectory;
    };

    static std::unique_ptr<Presenter> Create(const Config& config);
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    ~Presenter();

    void Gpu_Uuid(u8 out_uuid[16]) const;
    void Release_Frames(); // after emulation stops

    void Submit_Frame(const ModLoader_Frame& frame);
    void Push_Audio(const s16* samples, u32 frame_count, u32 sample_rate);
    void Set_Audio_Speed_Limit(bool enabled);
    bool Poll_Input(u32 port, void* out_state, u64 size) const;
    void Output_Size(u32& out_width, u32& out_height) const;
    Presenter_Request Take_Request() {
        return requested.exchange(Presenter_Request::None);
    }

    void Set_Controls(const Platform& platform);
    void Bind(u32 control, std::string_view binding);
    void Bind_Hotkey(Hotkey hotkey, std::string_view binding);

    u32 Take_Hotkeys() {
        return hotkeysPressed.exchange(0);
    }

    void Set_Rumble(u32 port, u32 strength);

    void Post_Request(Presenter_Request request) {
        requested = request;
    }

    bool Pump();
    void Set_Picture(Renderer_Scaling scaling, Renderer_Filter filter);
    void Set_Theme(Ui_Theme theme);
    void Set_Ui(std::function<void()> build) {
        uiBuild = std::move(build);
    }

    void Wake();
    void Set_Status(std::string_view text);

private:
    enum class Input_Kind : u32 {
        None,
        Key,
        Pad,
        Axis,
    };

    struct Bound_Input {
        Input_Kind kind = Input_Kind::None;
        s32 code = -1; // SDL_Scancode, SDL_GamepadButton or SDL_GamepadAxis
        f32 sign = 1.0f; // active axis direction
    };

    struct Shared_Import {
        u32 imageId = 0;
        std::unique_ptr<Renderer_Texture> texture;
        u64 lastUse = 0;
    };

    struct Pending_Frame {
        std::vector<u8> pixels;
        u32 width = 0;
        u32 height = 0;
        Renderer_Format format = Renderer_Format_Rgba8;
        u32 sourceWidth = 0;
        u32 sourceHeight = 0;
        f32 aspect = 4.0f / 3.0f;
        bool ready = false;
        bool shared = false;
        ModLoader_Gpu_Image image = {};
    };

    Presenter() = default;
    static std::vector<Bound_Input> Parse_Binding(std::string_view binding);
    static bool Live_Update(void* user, union SDL_Event* event);
    void Start_Audio();
    f32 Input_Value(const Bound_Input& input, const bool* keys, bool keyboard) const;
    void Update_Input();
    void Hotkey_Event(const union SDL_Event& event);
    Renderer_Texture* Shared_Texture(const ModLoader_Gpu_Image& image, u32 width, u32 height);
    void Take_Frame();
    bool Present();
    bool Draw_Frame();
    void Build_Frame();

    Renderer_Scaling scaling;
    Renderer_Filter filter;
    SDL_Window* window = nullptr;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Renderer_Surface> surface;
    std::unique_ptr<Renderer_Texture> texture;
    std::unique_ptr<Ui> ui;
    u32 wakeEvent = 0;

    bool drawingFrame = false;
    std::function<void()> uiBuild;

    mutable std::mutex frameLock;
    Pending_Frame pending;
    u64 skippedRelease = 0;
    std::string status;

    Shared_Import imports[6];
    u64 importUses = 0;
    Renderer_Texture* shown = nullptr;
    u64 shownReady = 0;
    u64 shownRelease = 0;
    u64 released = 0;
    u64 releaseTimeline = 0;
    u32 sourceWidth = 0;
    u32 sourceHeight = 0;
    f32 aspect = 4.0f / 3.0f;
    std::atomic<u32> outputWidth = 0;
    std::atomic<u32> outputHeight = 0;

    SDL_AudioStream* audio = nullptr;
    u32 audioRate = 32000;
    bool audioLimited = true;

    SDL_Gamepad* gamepad = nullptr;
    std::atomic<s32> rumbleRequested = -1;
    std::vector<std::vector<Bound_Input>> controls;
    std::vector<Bound_Input> hotkeys[static_cast<u32>(Hotkey::Count)];
    bool hotkeyAxisPast[static_cast<u32>(Hotkey::Count)] = {};
    std::atomic<u32> hotkeysPressed = 0;
    const Platform* platform = nullptr;
    mutable std::mutex inputLock;
    std::vector<u8> inputState;
    std::atomic<Presenter_Request> requested = Presenter_Request::None;
};
