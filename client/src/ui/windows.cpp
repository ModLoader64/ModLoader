#include "ui/windows_internal.h"
#include "ui/handle_table.h"
#include "ui/rml.h"
#include "RmlUi/Core.h"
#include "RmlUi/Debugger.h"
#include <utility>

namespace {

namespace Input = Rml::Input;

constexpr u32 gMaxInput = 1024;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
constexpr u8 gLatoRegular[] = {
#embed "../../../third_party/rmlui/Samples/assets/LatoLatin-Regular.ttf"
};
constexpr u8 gLatoBold[] = {
#embed "../../../third_party/rmlui/Samples/assets/LatoLatin-Bold.ttf"
};
constexpr u8 gRobotoMono[] = {
#embed "../../../third_party/rmlui/Samples/assets/RobotoMono-Regular.ttf"
};
#pragma clang diagnostic pop

struct Key_Pair {
    SDL_Keycode key;
    Input::KeyIdentifier identifier;
};

constexpr Key_Pair gKeys[] = {
    { SDLK_SPACE, Input::KI_SPACE },
    { SDLK_ESCAPE, Input::KI_ESCAPE },
    { SDLK_BACKSPACE, Input::KI_BACK },
    { SDLK_TAB, Input::KI_TAB },
    { SDLK_CLEAR, Input::KI_CLEAR },
    { SDLK_RETURN, Input::KI_RETURN },
    { SDLK_PAUSE, Input::KI_PAUSE },
    { SDLK_CAPSLOCK, Input::KI_CAPITAL },
    { SDLK_PAGEUP, Input::KI_PRIOR },
    { SDLK_PAGEDOWN, Input::KI_NEXT },
    { SDLK_END, Input::KI_END },
    { SDLK_HOME, Input::KI_HOME },
    { SDLK_LEFT, Input::KI_LEFT },
    { SDLK_UP, Input::KI_UP },
    { SDLK_RIGHT, Input::KI_RIGHT },
    { SDLK_DOWN, Input::KI_DOWN },
    { SDLK_INSERT, Input::KI_INSERT },
    { SDLK_DELETE, Input::KI_DELETE },
    { SDLK_HELP, Input::KI_HELP },
    { SDLK_SEMICOLON, Input::KI_OEM_1 },
    { SDLK_PLUS, Input::KI_OEM_PLUS },
    { SDLK_EQUALS, Input::KI_OEM_PLUS },
    { SDLK_COMMA, Input::KI_OEM_COMMA },
    { SDLK_MINUS, Input::KI_OEM_MINUS },
    { SDLK_PERIOD, Input::KI_OEM_PERIOD },
    { SDLK_SLASH, Input::KI_OEM_2 },
    { SDLK_GRAVE, Input::KI_OEM_3 },
    { SDLK_LEFTBRACKET, Input::KI_OEM_4 },
    { SDLK_BACKSLASH, Input::KI_OEM_5 },
    { SDLK_RIGHTBRACKET, Input::KI_OEM_6 },
    { SDLK_APOSTROPHE, Input::KI_OEM_7 },
    { SDLK_KP_ENTER, Input::KI_NUMPADENTER },
    { SDLK_KP_MULTIPLY, Input::KI_MULTIPLY },
    { SDLK_KP_PLUS, Input::KI_ADD },
    { SDLK_KP_MINUS, Input::KI_SUBTRACT },
    { SDLK_KP_PERIOD, Input::KI_DECIMAL },
    { SDLK_KP_DIVIDE, Input::KI_DIVIDE },
    { SDLK_KP_EQUALS, Input::KI_OEM_NEC_EQUAL },
    { SDLK_NUMLOCKCLEAR, Input::KI_NUMLOCK },
    { SDLK_SCROLLLOCK, Input::KI_SCROLL },
    { SDLK_LSHIFT, Input::KI_LSHIFT },
    { SDLK_RSHIFT, Input::KI_RSHIFT },
    { SDLK_LCTRL, Input::KI_LCONTROL },
    { SDLK_RCTRL, Input::KI_RCONTROL },
    { SDLK_LALT, Input::KI_LMENU },
    { SDLK_RALT, Input::KI_RMENU },
    { SDLK_LGUI, Input::KI_LMETA },
    { SDLK_RGUI, Input::KI_RMETA },
};

std::deque<Module_Window> sWindows;
Handle_Table<Module_Window*> sWindowHandles;

std::vector<Module_Window*> Window_Snapshot() {
    std::lock_guard lock(gWindows.lock);
    std::vector<Module_Window*> windows;

    windows.reserve(sWindows.size());
    for (Module_Window& window : sWindows) {
        windows.push_back(&window);
    }

    return windows;
}

Input::KeyIdentifier Key_Identifier(SDL_Keycode key) {
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<Input::KeyIdentifier>(Input::KI_A + (key - SDLK_A));
    }

    if (key >= SDLK_0 && key <= SDLK_9) {
        return static_cast<Input::KeyIdentifier>(Input::KI_0 + (key - SDLK_0));
    }

    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return static_cast<Input::KeyIdentifier>(Input::KI_F1 + (key - SDLK_F1));
    }

    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return static_cast<Input::KeyIdentifier>(Input::KI_NUMPAD1 + (key - SDLK_KP_1));
    }

    if (key == SDLK_KP_0) {
        return Input::KI_NUMPAD0;
    }

    for (const Key_Pair& pair : gKeys) {
        if (pair.key == key) {
            return pair.identifier;
        }
    }

    return Input::KI_UNKNOWN;
}

s32 Key_Modifiers(SDL_Keymod mods) {
    return ((mods & SDL_KMOD_CTRL) != 0 ? Input::KM_CTRL : 0) | ((mods & SDL_KMOD_SHIFT) != 0 ? Input::KM_SHIFT : 0) |
        ((mods & SDL_KMOD_ALT) != 0 ? Input::KM_ALT : 0) | ((mods & SDL_KMOD_GUI) != 0 ? Input::KM_META : 0) |
        ((mods & SDL_KMOD_CAPS) != 0 ? Input::KM_CAPSLOCK : 0) | ((mods & SDL_KMOD_NUM) != 0 ? Input::KM_NUMLOCK : 0) |
        ((mods & SDL_KMOD_SCROLL) != 0 ? Input::KM_SCROLLLOCK : 0);
}

SDL_WindowID Event_Window(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return event.wheel.windowID;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return event.key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return event.text.windowID;
    default:
        return event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST ? event.window.windowID : 0;
    }
}

std::optional<Window_Input> Convert(const SDL_Event& event, SDL_Window* window) {
    f32 density = SDL_GetWindowPixelDensity(window);
    s32 modifiers = Key_Modifiers(SDL_GetModState());

    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        return Window_Input{ Input_Kind::Move, 0, modifiers, event.motion.x * density, event.motion.y * density, {} };
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button > SDL_BUTTON_RIGHT) {
            return std::nullopt;
        }

        return Window_Input{
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? Input_Kind::Down : Input_Kind::Up,
            event.button.button == SDL_BUTTON_RIGHT        ? 1
                : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                           : 0,
            modifiers,
            0.0f,
            0.0f,
            {},
        };
    case SDL_EVENT_MOUSE_WHEEL:
        return Window_Input{ Input_Kind::Wheel, 0, modifiers, event.wheel.x, -event.wheel.y, {} };
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return Window_Input{ Input_Kind::Leave, 0, modifiers, 0.0f, 0.0f, {} };
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return Window_Input{
            event.type == SDL_EVENT_KEY_DOWN ? Input_Kind::Key_Down : Input_Kind::Key_Up,
            Key_Identifier(event.key.key),
            Key_Modifiers(event.key.mod),
            0.0f,
            0.0f,
            {},
        };
    case SDL_EVENT_TEXT_INPUT:
        return Window_Input{ Input_Kind::Text, 0, modifiers, 0.0f, 0.0f, event.text.text };
    default:
        return std::nullopt;
    }
}

void Queue_Input(Module_Window& window, Window_Input input) {
    if (window.input.size() == gMaxInput) {
        window.input.pop_front();
    }
    window.input.push_back(std::move(input));
}

Module_Window* Find_Window(const Module& owner, u32 handle) {
    Module_Window** entry = sWindowHandles.Find(handle);
    Module_Window* window = entry != nullptr ? *entry : nullptr;

    if (window == nullptr || window->state == Window_State::Free || window->owner != &owner) {
        return nullptr;
    }

    return window;
}

bool Window_Open(Module_Window& window) {
    bool shown = window.shown;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN | (shown ? 0 : SDL_WINDOW_HIDDEN);
    const char* name = window.owner != nullptr ? window.owner->name.c_str() : "ui";
    f32 scale = SDL_GetWindowDisplayScale(gWindows.main) / SDL_GetWindowPixelDensity(gWindows.main);
    s32 width;
    s32 height;

    scale = scale > 0.0f ? scale : 1.0f;
    if (!window.overlay) {
        f64 scaled_width = static_cast<f64>(window.width) * scale;
        f64 scaled_height = static_cast<f64>(window.height) * scale;
        u32 size_limit = gWindows.renderer->Texture_Size_Limit();

        if (!(scaled_width >= 1.0 && scaled_height >= 1.0 && scaled_width <= INT32_MAX && scaled_height <= INT32_MAX &&
              scaled_width <= size_limit && scaled_height <= size_limit)) {
            Log_Error(name, "unsupported window size %ux%u", window.width, window.height);
            return false;
        }

        window.window = SDL_CreateWindow(window.title.c_str(), static_cast<int>(scaled_width), static_cast<int>(scaled_height), flags);
        window.surface = window.window != nullptr ? gWindows.renderer->Create_Surface(window.window) : nullptr;
        if (window.surface == nullptr) {
            Log_Error(name, "cannot open window %s: %s", window.title.c_str(), SDL_GetError());
            return false;
        }

        SDL_StartTextInput(window.window);
    }

    SDL_GetWindowSizeInPixels(Shown_In(window), &width, &height);
    window.context = Rml::CreateContext(Text_Format("window%08x", window.handle), Rml::Vector2i(width, height));
    if (window.context == nullptr) {
        Log_Error(name, "cannot create RmlUi context for %s", window.title.c_str());
        return false;
    }

    window.context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(Shown_In(window)));
    window.hidden = !shown;
    return true;
}

void Window_Close(Module_Window& window) {
    Rml::Context* context = window.context;

    if (context != nullptr) {
        if (gWindows.debugged == context) {
            Rml::Debugger::Shutdown();
            gWindows.debugged = nullptr;
        }

        Ui_Windows_Work_For(window.owner);
        Rml::RemoveContext(context->GetName());
        Rml_Context_Removed(context);
        Ui_Windows_Work_For(nullptr);
    }

    window.surface.reset();
    if (window.window != nullptr) {
        SDL_DestroyWindow(window.window);
    }

    window.input.clear();
    window.frame = {};
    std::lock_guard lock(gWindows.lock);
    sWindowHandles.Remove(sWindowHandles.Index(window.handle));
    window.handle = 0;
    window.owner = nullptr;
    window.title.clear();
    window.window = nullptr;
    window.context = nullptr;
    window.state = Window_State::Free;
}

bool Window_Ready(Module_Window& window) {
    Window_State asked = Window_State::Asked;

    if (window.state != Window_State::Asked) {
        return window.state == Window_State::Open;
    }

    if (!Window_Open(window)) {
        window.state = Window_State::Closing;
        return false;
    }

    return window.state.compare_exchange_strong(asked, Window_State::Open);
}

void Window_Update(Module_Window& window) {
    bool shown = window.shown;
    s32 width;
    s32 height;

    if (window.window != nullptr && shown && window.hidden) {
        SDL_ShowWindow(window.window);
    }

    if (window.window != nullptr && !shown && !window.hidden) {
        SDL_HideWindow(window.window);
    }

    window.hidden = !shown;
    SDL_GetWindowSizeInPixels(Shown_In(window), &width, &height);
    window.width = static_cast<u32>(width);
    window.height = static_cast<u32>(height);
    window.context->SetDimensions(Rml::Vector2i(width, height));
    window.context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(Shown_In(window)));
}

void Toggle_Debugger(Rml::Context* context) {
    if (gWindows.debugged == context) {
        Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
        return;
    }

    if (gWindows.debugged != nullptr) {
        Rml::Debugger::Shutdown();
    }

    gWindows.debugged = Rml::Debugger::Initialise(context) ? context : nullptr;
    if (gWindows.debugged != nullptr) {
        Rml::Debugger::SetVisible(true);
    }
}

void Window_Feed(Module_Window& window) {
    Rml::Context* context = window.context;

    for (const Window_Input& input : window.input) {
        switch (input.kind) {
        case Input_Kind::Move:
            context->ProcessMouseMove(static_cast<int>(input.x), static_cast<int>(input.y), input.modifiers);
            break;
        case Input_Kind::Down:
            context->ProcessMouseButtonDown(input.value, input.modifiers);
            break;
        case Input_Kind::Up:
            context->ProcessMouseButtonUp(input.value, input.modifiers);
            break;
        case Input_Kind::Wheel:
            context->ProcessMouseWheel(Rml::Vector2f(input.x, input.y), input.modifiers);
            break;
        case Input_Kind::Leave:
            context->ProcessMouseLeave();
            break;
        case Input_Kind::Key_Down:
            if (input.value == Input::KI_F8) {
                Toggle_Debugger(context);
                break;
            }
            context->ProcessKeyDown(static_cast<Input::KeyIdentifier>(input.value), input.modifiers);
            if (input.value == Input::KI_RETURN || input.value == Input::KI_NUMPADENTER) {
                context->ProcessTextInput('\n');
            }
            break;
        case Input_Kind::Key_Up:
            context->ProcessKeyUp(static_cast<Input::KeyIdentifier>(input.value), input.modifiers);
            break;
        case Input_Kind::Text:
            context->ProcessTextInput(input.text);
            break;
        }
    }
    window.input.clear();
}

bool Text_Focus(Rml::Context* context) {
    Rml::Element* focus = context->GetFocusElement();
    Rml::String type = focus != nullptr ? focus->GetAttribute<Rml::String>("type", "text") : "";
    return focus != nullptr && (focus->GetTagName() == "textarea" || (focus->GetTagName() == "input" && (type == "text" || type == "password")));
}

u32 Window_Create(Module& module, u64 title_address, u64 title_length, u32 width, u32 height) {
    std::optional<std::string_view> title = title_address != 0 ? module.Text(title_address, title_length) : std::nullopt;
    Module_Window* window = nullptr;

    if (title_address != 0 && !title) {
        return 0;
    }

    {
        std::lock_guard lock(gWindows.lock);

        for (Module_Window& candidate : sWindows) {
            if (candidate.state == Window_State::Free) {
                window = &candidate;
                break;
            }
        }

        if (window == nullptr) {
            sWindows.emplace_back();
            window = &sWindows.back();
        }

        window->handle = sWindowHandles.Add(window);
        if (window->handle != 0) {
            window->owner = &module;
            window->overlay = title_address == 0;
            window->title = title.value_or("");
            window->width = width != 0 ? width : 640;
            window->height = height != 0 ? height : 480;
            window->shown = true;
            window->hidden = false;
            window->state = Window_State::Asked;
            return window->handle;
        }
    }

    return 0;
}

NativeSymbol sNatives[] = {
    Native("ui_window_create", "(IIii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Window_Create(Module_Of(exec_env), slots[0], slots[1], static_cast<u32>(slots[2]), static_cast<u32>(slots[3]));
    }),
    Native("ui_window_destroy", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        std::lock_guard lock(gWindows.lock);
        Module_Window* window = Find_Window(Module_Of(exec_env), static_cast<u32>(slots[0]));

        if (window != nullptr && window->state != Window_State::Closing) {
            window->state = Window_State::Closing;
        }
    }),
    Native("ui_window_show", "(ii)", [](wasm_exec_env_t exec_env, u64* slots) {
        std::lock_guard lock(gWindows.lock);
        Module_Window* window = Find_Window(Module_Of(exec_env), static_cast<u32>(slots[0]));

        if (window != nullptr) {
            window->shown = static_cast<u32>(slots[1]) != 0;
        }
    }),
    Native("ui_window_shown", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        std::lock_guard lock(gWindows.lock);
        Module_Window* window = Find_Window(Module_Of(exec_env), static_cast<u32>(slots[0]));

        slots[0] = window != nullptr && window->shown ? 1 : 0;
    }),
};

} // namespace

Windows_State gWindows;

SDL_Window* Shown_In(const Module_Window& window) {
    return window.window != nullptr ? window.window : gWindows.main;
}

void Ui_Windows_Register_Natives() {
    Register_Natives(sNatives, "UI's window");
    Rml_Register_Natives();
}

void Ui_Windows_Start(Renderer& renderer, SDL_Window* main_window) {
    gWindows.renderer = &renderer;
    gWindows.main = main_window;
    gWindows.uiThread = Thread_Current_Id();
    Rml_Interfaces_Install();

    if (!Rml::Initialise()) {
        Log_Error("ui", "cannot initialize RmlUi");
        return;
    }

    Rml::LoadFontFace({ gLatoRegular, sizeof(gLatoRegular) }, "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, true);
    Rml::LoadFontFace({ gLatoBold, sizeof(gLatoBold) }, "LatoLatin", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);
    Rml::LoadFontFace({ gRobotoMono, sizeof(gRobotoMono) }, "RobotoMono", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    Rml_Start();
    gWindows.started = true;
}

void Ui_Windows_Stop() {
    if (!gWindows.started) {
        return;
    }

    for (Module_Window* entry : Window_Snapshot()) {
        Module_Window& window = *entry;
        if (window.state != Window_State::Free) {
            Window_Close(window);
        }
    }

    Rml_Stop();
    Rml::Shutdown();
    for (SDL_Cursor*& cursor : gWindows.cursors) {
        SDL_DestroyCursor(cursor);
        cursor = nullptr;
    }

    gWindows.started = false;
}

void Ui_Windows_Release_Owner(const Module& owner) {
    std::lock_guard lock(gWindows.lock);

    for (Module_Window& window : sWindows) {
        if (window.state != Window_State::Free && window.owner == &owner) {
            window.owner = nullptr;
            window.state = Window_State::Closing;
        }
    }
}

void Ui_Windows_Frame_Begin() {
    if (!gWindows.started) {
        return;
    }

    for (Module_Window* entry : Window_Snapshot()) {
        Module_Window& window = *entry;
        if (window.state == Window_State::Closing) {
            Window_Close(window);
        }

        if (!Window_Ready(window) || window.owner == nullptr) {
            continue;
        }

        Window_Update(window);
        Ui_Windows_Work_For(window.owner);
        gWindows.current = &window;
        Window_Feed(window);
        gWindows.current = nullptr;
    }
    Ui_Windows_Work_For(nullptr);
}

void Ui_Windows_Frame_End() {
    bool wants_keyboard = false;

    if (!gWindows.started) {
        return;
    }
    
    for (Module_Window* entry : Window_Snapshot()) {
        Module_Window& window = *entry;
        if (window.state != Window_State::Open || window.context == nullptr) {
            continue;
        }

        window.frame.Clear();

        if (window.hidden) {
            continue;
        }

        Ui_Windows_Work_For(window.owner);
        gWindows.current = &window;
        window.context->Update();
        window.scissored = false;
        window.transformed = false;
        window.context->Render();
        gWindows.current = nullptr;
        wants_keyboard = wants_keyboard || (window.overlay && Text_Focus(window.context));
    }

    Ui_Windows_Work_For(nullptr);
    gWindows.wantsKeyboard = wants_keyboard;
}

bool Ui_Windows_Event(const SDL_Event& event) {
    SDL_WindowID id = Event_Window(event);
    std::optional<Window_Input> input;

    if (id == 0 || !gWindows.started) {
        return false;
    }

    for (Module_Window* entry : Window_Snapshot()) {
        Module_Window& window = *entry;
        if (window.window == nullptr || SDL_GetWindowID(window.window) != id) {
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            window.shown = false;
            SDL_HideWindow(window.window);
            window.hidden = true;
        }
        else if ((input = Convert(event, window.window))) {
            Queue_Input(window, std::move(*input));
        }

        return true;
    }

    return false;
}

void Ui_Windows_Main_Event(const SDL_Event& event) {
    std::optional<Window_Input> input = gWindows.started && Event_Window(event) == SDL_GetWindowID(gWindows.main) ? Convert(event, gWindows.main) : std::nullopt;

    for (Module_Window* entry : Window_Snapshot()) {
        Module_Window& window = *entry;
        if (input && window.context != nullptr && window.overlay && !window.hidden) {
            Queue_Input(window, *input);
        }
    }
}

bool Ui_Windows_Wants_Keyboard() {
    return gWindows.wantsKeyboard;
}

void Ui_Windows_Draw_Overlays(Renderer& renderer) {
    for (Module_Window* entry : Window_Snapshot()) {
        const Module_Window& window = *entry;
        if (window.context != nullptr && window.overlay && !window.hidden) {
            window.frame.Draw(renderer);
        }
    }
}

void Ui_Windows_Present(Renderer& renderer) {
    u32 width;
    u32 height;

    for (Module_Window* entry : Window_Snapshot()) {
        const Module_Window& window = *entry;
        if (window.surface != nullptr && !window.hidden && renderer.Begin(*window.surface, width, height)) {
            window.frame.Draw(renderer);
            renderer.End();
        }
    }
}

Rml::Context* Ui_Windows_Context(const Module& owner, u32 handle) {
    Module_Window* window;

    {
        std::lock_guard lock(gWindows.lock);
        window = gWindows.started && Thread_Current_Id() == gWindows.uiThread ? Find_Window(owner, handle) : nullptr;
    }

    return window != nullptr && Window_Ready(*window) ? window->context : nullptr;
}

Module* Ui_Windows_Owner(Rml::Context* context) {
    for (Module_Window* entry : Window_Snapshot()) {
        const Module_Window& window = *entry;
        if (context != nullptr && window.context == context) {
            return window.owner;
        }
    }
    
    return nullptr;
}

Module* Ui_Windows_Work_For(Module* module) {
    return std::exchange(gWindows.worker, module);
}
