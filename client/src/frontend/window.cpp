#include "frontend/window.h"
#include "frontend/images.h"
#include "base.h"

#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_GL3.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEffectInstancer.h>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {


constexpr s32 gPreferredWidth = 1120;
constexpr s32 gPreferredHeight = 700;
constexpr s32 gMinimumWidth = 960;
constexpr s32 gMinimumHeight = 600;
constexpr float gResizeBorder = 6;
constexpr float gResizeGrip = 18;
constexpr s32 gLiveResizeExpose = 1;
#ifdef _WIN32
constexpr wchar_t gWindowProperty[] = L"ModLoader.Frontend";
#endif

void Element_Bounds(Rml::ElementDocument* document, const char* identifier, SDL_FRect& bounds) {
    bounds = {};
    if (document == nullptr) {
        return;
    }

    Rml::Element* element = document->GetElementById(identifier);
    if (element != nullptr) {
        Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        bounds = {offset.x, offset.y, element->GetOffsetWidth(), element->GetOffsetHeight()};
    }
}

float Window_Scale(SDL_Window* window) {
    float density = SDL_GetWindowPixelDensity(window);
    return SDL_GetWindowDisplayScale(window) / (density > 0 ? density : 1);
}

class Brand_Gradient final : public Rml::FontEffect {
    Rml::SharedPtr<Rml::FontEffect> outline;
    Rml::Colourb topColor;
    Rml::Colourb bottomColor;

public:
    Brand_Gradient(Rml::SharedPtr<Rml::FontEffect> mask, const Rml::Colourb& top, const Rml::Colourb& bottom)
        : outline(std::move(mask)), topColor(top), bottomColor(bottom) {
        SetLayer(Layer::Front);
    }

    bool HasUniqueTexture() const override {
        return true;
    }

    bool GetGlyphMetrics(Rml::Vector2i& origin, Rml::Vector2i& dimensions, const Rml::FontGlyph& glyph) const override {
        return outline->GetGlyphMetrics(origin, dimensions, glyph);
    }

    void GenerateGlyphTexture(Rml::byte* pixels, Rml::Vector2i dimensions, s32 stride, const Rml::FontGlyph& glyph) const override {
        outline->GenerateGlyphTexture(pixels, dimensions, stride, glyph);
        const s32 gradient_height = std::max(1, dimensions.y - 1);
        for (s32 row = 0; row < dimensions.y; row++) {
            float blend = static_cast<float>(row) / gradient_height;
            Rml::Colourb color;

            for (s32 channel = 0; channel < 3; channel++) {
                color[channel] = static_cast<u8>(topColor[channel] + (bottomColor[channel] - topColor[channel]) * blend);
            }

            Rml::byte* pixel = pixels + row * stride;
            for (s32 column = 0; column < dimensions.x; column++, pixel += 4) {
                for (s32 channel = 0; channel < 3; channel++) {
                    pixel[channel] = static_cast<u8>((color[channel] * pixel[3] + UINT8_MAX / 2) / UINT8_MAX);
                }
            }
        }
    }
};

class Brand_Gradient_Instancer final : public Rml::FontEffectInstancer {
    Rml::PropertyId topColor;
    Rml::PropertyId bottomColor;
    Rml::SharedPtr<Rml::FontEffect> outline;

public:
    Brand_Gradient_Instancer() {
        topColor = RegisterProperty("top", "white", true).AddParser("color").GetId();
        bottomColor = RegisterProperty("bottom", "black", true).AddParser("color").GetId();
        RegisterShorthand("font-effect", "top, bottom", Rml::ShorthandType::FallThrough);

        Rml::FontEffectInstancer* instancer = Rml::Factory::GetFontEffectInstancer("outline");
        Rml::PropertyDictionary properties;
        instancer->GetPropertySpecification().ParsePropertyDeclaration(properties, "font-effect", "1px white");
        outline = instancer->InstanceFontEffect("outline", properties);
    }

    Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String&, const Rml::PropertyDictionary& properties) override {
        return Rml::MakeShared<Brand_Gradient>(outline, properties.GetProperty(topColor)->Get<Rml::Colourb>(),
            properties.GetProperty(bottomColor)->Get<Rml::Colourb>());
    }
};

class Frontend_System final : public SystemInterface_SDL {
public:
    explicit Frontend_System(SDL_Window* window) : SystemInterface_SDL(window) {}

    void JoinPath(Rml::String& translated, const Rml::String& document, const Rml::String& path) override {
        if (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
            translated = path;
            return;
        }
        SystemInterface_SDL::JoinPath(translated, document, path);
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        Log_Level level = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? Log_Level::Error :
            type == Rml::Log::LT_WARNING ? Log_Level::Warning : Log_Level::Debug;
        Log_Write(level, "frontend", "%s", message.c_str());
        return true;
    }
};

class Frontend_Renderer final : public RenderInterface_GL3 {
public:
    Frontend_Images images {*this};

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        return images.Load_Texture(dimensions, source);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        if (!images.Owns_Texture(texture)) {
            RenderInterface_GL3::ReleaseTexture(texture);
        }
    }
};

}

struct Frontend_Window::Backend {
    SDL_Window* window = nullptr;
    SDL_GLContext graphics = nullptr;
    std::unique_ptr<Frontend_System> system;
    std::unique_ptr<Frontend_Renderer> renderer;
    std::unique_ptr<Brand_Gradient_Instancer> brandFont;
    TextInputMethodEditor_SDL textInput;
    Rml::Context* context = nullptr;
    bool sdlInitialized = false;
    bool graphicsInitialized = false;
    bool rmlInitialized = false;
    bool customChrome = false;
    bool drawing = false;
    bool polling = false;
    bool uploadsPending = false;
    SDL_FRect dragBounds = {};
    SDL_FRect controlBounds = {};
#ifdef _WIN32
    HWND nativeWindow = nullptr;
    WNDPROC previousProcedure = nullptr;

    static LRESULT CALLBACK Window_Procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
        Backend* state = static_cast<Backend*>(GetPropW(window, gWindowProperty));
        LRESULT result = CallWindowProcW(state->previousProcedure, window, message, parameter, data);
        if (message == WM_STYLECHANGING && parameter == static_cast<WPARAM>(GWL_STYLE)) {
            // SDL restores WS_POPUP on restore; can be treated as exclusive fullscreen
            reinterpret_cast<STYLESTRUCT*>(data)->styleNew &= ~static_cast<DWORD>(WS_POPUP);
            return 0;
        }
        return result;
    }
#endif

    static SDL_HitTestResult SDLCALL Hit_Test(SDL_Window* window, const SDL_Point* point, void* user) {
        Backend& state = *static_cast<Backend*>(user);
        s32 width = 0;
        s32 height = 0;
        SDL_GetWindowSize(window, &width, &height);
        float scale = Window_Scale(window);
        float edge = gResizeBorder * scale;
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)) {
            bool left = point->x < edge;
            bool right = point->x >= width - edge;
            bool top = point->y < edge;
            bool bottom = point->y >= height - edge;
            if (point->x >= width - gResizeGrip * scale && point->y >= height - gResizeGrip * scale) {
                return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            }

            if (top && left) {
                return SDL_HITTEST_RESIZE_TOPLEFT;
            }

            if (top && right) {
                return SDL_HITTEST_RESIZE_TOPRIGHT;
            }

            if (bottom && left) {
                return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            }

            if (left) {
                return SDL_HITTEST_RESIZE_LEFT;
            }

            if (right) {
                return SDL_HITTEST_RESIZE_RIGHT;
            }

            if (top) {
                return SDL_HITTEST_RESIZE_TOP;
            }

            if (bottom) {
                return SDL_HITTEST_RESIZE_BOTTOM;
            }
        }

        float density = SDL_GetWindowPixelDensity(window);
        SDL_FPoint position = {point->x * density, point->y * density};
        if (SDL_PointInRectFloat(&position, &state.dragBounds) && !SDL_PointInRectFloat(&position, &state.controlBounds)) {
            return SDL_HITTEST_DRAGGABLE;
        }
        return SDL_HITTEST_NORMAL;
    }

    static bool SDLCALL Live_Update(void* user, SDL_Event* event) {
        Backend& state = *static_cast<Backend*>(user);
        if (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == gLiveResizeExpose && SDL_IsMainThread() &&
            state.polling && event->window.windowID == SDL_GetWindowID(state.window)) {
            state.Draw();
        }
        return true;
    }

    void Update_Minimum_Size(bool set_initial_size = false) {
        float scale = Window_Scale(window);
        s32 preferred_width = static_cast<s32>(gPreferredWidth * scale);
        s32 preferred_height = static_cast<s32>(gPreferredHeight * scale);
        SDL_Rect usable = {};
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable)) {
            float fit = std::min({1.0f, static_cast<float>(usable.w) / preferred_width, static_cast<float>(usable.h) / preferred_height});
            preferred_width = static_cast<s32>(preferred_width * fit);
            preferred_height = static_cast<s32>(preferred_height * fit);
        }

        SDL_SetWindowMinimumSize(window, std::min(static_cast<s32>(gMinimumWidth * scale), preferred_width), std::min(static_cast<s32>(gMinimumHeight * scale), preferred_height));
        if (set_initial_size) {
            SDL_SetWindowSize(window, preferred_width, preferred_height);
        }
    }

    void Draw() {
        if (drawing) {
            return;
        }

        drawing = true;
        s32 width = 0;
        s32 height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (context->GetDimensions() != Rml::Vector2i(width, height)) {
            renderer->SetViewport(width, height);
            context->SetDimensions({width, height});
        }

        context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(window));
        context->Update();
        if (customChrome) {
            Rml::ElementDocument* document = context->GetDocument(0);
            Element_Bounds(document, "window-drag", dragBounds);
            Element_Bounds(document, "window-controls", controlBounds);
        }

        renderer->Clear();
        renderer->BeginFrame();
        context->Render();
        renderer->EndFrame();
        SDL_GL_SwapWindow(window);
        drawing = false;
    }

    ~Backend() {
        if (sdlInitialized) {
            SDL_RemoveEventWatch(Live_Update, this);
        }

        if (rmlInitialized) {
            Rml::Shutdown();
        }

        Rml::SetTextInputHandler(nullptr);
        Rml::SetSystemInterface(nullptr);
        Rml::SetRenderInterface(nullptr);
        renderer.reset();
        system.reset();
        if (graphicsInitialized) {
            RmlGL3::Shutdown();
        }

        if (graphics != nullptr) {
            SDL_GL_DestroyContext(graphics);
        }

#ifdef _WIN32
        if (previousProcedure != nullptr) {
            SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProcedure));
            RemovePropW(nativeWindow, gWindowProperty);
        }
#endif

        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }

        if (sdlInitialized) {
            SDL_Quit();
        }
    }
};

Frontend_Window::Frontend_Window() = default;
Frontend_Window::~Frontend_Window() = default;

bool Frontend_Window::Open(const std::string& asset_directory) {
    backend = std::make_unique<Backend>();
    auto Fail = [this](const char* message) {
        Log_Error("frontend", "%s", message);
        backend.reset();
        return false;
    };

    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return Fail(SDL_GetError());
    }
    backend->sdlInitialized = true;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    float content_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (content_scale <= 0) {
        content_scale = 1;
    }

    backend->window = SDL_CreateWindow(
        "ModLoader",
        static_cast<s32>(gPreferredWidth * content_scale),
        static_cast<s32>(gPreferredHeight * content_scale),
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (backend->window == nullptr) {
        return Fail(SDL_GetError());
    }

    backend->customChrome = SDL_SetWindowHitTest(backend->window, Backend::Hit_Test, backend.get());
    if (backend->customChrome && !SDL_SetWindowBordered(backend->window, false)) {
        SDL_SetWindowHitTest(backend->window, nullptr, nullptr);
        backend->customChrome = false;
    }
#ifdef _WIN32
    if (backend->customChrome) {
        HWND native_window = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(backend->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!SetPropW(native_window, gWindowProperty, backend.get())) {
            return Fail("Could not configure the frontend window.");
        }
        backend->nativeWindow = native_window;
        backend->previousProcedure = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(native_window, GWLP_WNDPROC));
        SetWindowLongPtrW(native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Backend::Window_Procedure));
        LONG_PTR style = GetWindowLongPtrW(native_window, GWL_STYLE);
        SetWindowLongPtrW(native_window, GWL_STYLE, style & ~static_cast<LONG_PTR>(WS_POPUP));
        SetWindowPos(native_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif

    backend->Update_Minimum_Size(true);
    SDL_SetWindowPosition(backend->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    backend->graphics = SDL_GL_CreateContext(backend->window);
    if (backend->graphics == nullptr) {
        return Fail(SDL_GetError());
    }

    SDL_GL_SetSwapInterval(1);
    Rml::String graphics_message;
    if (!RmlGL3::Initialize(&graphics_message)) {
        return Fail(graphics_message.c_str());
    }

    backend->graphicsInitialized = true;
    backend->renderer = std::make_unique<Frontend_Renderer>();
    if (!*backend->renderer) {
        return Fail("Could not initialize the launcher OpenGL renderer.");
    }

    backend->system = std::make_unique<Frontend_System>(backend->window);
    Rml::SetFileInterface(nullptr);
    Rml::SetSystemInterface(backend->system.get());
    Rml::SetRenderInterface(backend->renderer.get());
    Rml::SetTextInputHandler(&backend->textInput);
    if (!Rml::Initialise()) {
        return Fail("Could not initialize the launcher user interface.");
    }

    backend->rmlInitialized = true;
    backend->brandFont = std::make_unique<Brand_Gradient_Instancer>();
    Rml::Factory::RegisterFontEffectInstancer("brand-gradient", backend->brandFont.get());

    s32 width = 0;
    s32 height = 0;
    SDL_GetWindowSizeInPixels(backend->window, &width, &height);
    backend->renderer->SetViewport(width, height);
    backend->context = Rml::CreateContext("frontend", {width, height});
    if (backend->context == nullptr) {
        return Fail("Could not create the launcher user interface context.");
    }

    backend->context->SetDensityIndependentPixelRatio(SDL_GetWindowDisplayScale(backend->window));
    for (const char* font : {"Montserrat-Regular", "Montserrat-SemiBold", "Montserrat-Bold", "Teko-Bold"}) {
        if (!Rml::LoadFontFace(Path_Join(asset_directory, std::string(font) + ".ttf"))) {
            return Fail("Could not load the launcher fonts.");
        }
    }

    SDL_AddEventWatch(Backend::Live_Update, backend.get());
    return true;
}

void Frontend_Wake() {
    SDL_Event event = {};
    event.type = SDL_EVENT_USER;
    SDL_PushEvent(&event);
}

bool Frontend_Window::Poll(bool wait) {
    backend->polling = true;
    const SDL_WindowID window_id = SDL_GetWindowID(backend->window);
    double delay = wait && !backend->uploadsPending ? backend->context->GetNextUpdateDelay() : 0;
    s32 timeout = std::isinf(delay) ? -1 : static_cast<s32>(std::ceil(delay * SDL_MS_PER_SECOND));
    SDL_Event event;
    for (bool received = SDL_WaitEventTimeout(&event, timeout); received; received = SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window_id)) {
            backend->polling = false;
            return false;
        }

        if (event.type == SDL_EVENT_TEXT_EDITING) {
            backend->textInput.HandleEdit(event.edit);
            continue;
        }

        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            continue;
        }

        backend->drawing = true;
        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && event.window.windowID == window_id) {
            backend->Update_Minimum_Size();
        }

        RmlSDL::InputEventHandler(backend->context, backend->window, event);
        backend->drawing = false;
    }
    backend->polling = false;
    backend->uploadsPending = backend->renderer->images.Poll();
    return true;
}

void Frontend_Window::Draw() {
    backend->Draw();
}

Frontend_Image_State Frontend_Window::Prepare_Image(const std::string& source, bool priority, bool pinned) {
    return backend->renderer->images.Prepare(source, priority, pinned);
}

void Frontend_Window::Refresh_Images() {
    Rml::ReleaseTextures();
    backend->renderer->images.Clear(true);
}

SDL_Window* Frontend_Window::Window() {
    return backend ? backend->window : nullptr;
}

Rml::Context* Frontend_Window::Context() {
    return backend ? backend->context : nullptr;
}

bool Frontend_Window::Custom_Chrome() const {
    return backend && backend->customChrome;
}
