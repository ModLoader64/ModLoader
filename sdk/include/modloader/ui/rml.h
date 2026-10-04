#pragma once

#include <modloader/function.h>
#include <modloader/ui/rml_keys.h>
#include <modloader/subscription.h>
#include <modloader/types.h>
#include <modloader/ui/ui.h>

#include <modloader_rml.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ModLoader::Rml {

using Handle = u32;
using Box = ModLoader_Rml_Box;

enum Modifier : u32 {
    Modifier_Ctrl = MODLOADER_RML_MODIFIER_CTRL,
    Modifier_Shift = MODLOADER_RML_MODIFIER_SHIFT,
    Modifier_Alt = MODLOADER_RML_MODIFIER_ALT,
    Modifier_Meta = MODLOADER_RML_MODIFIER_META,
};

enum class Phase : u32 {
    None = MODLOADER_RML_EVENT_PHASE_NONE,
    Capture = MODLOADER_RML_EVENT_PHASE_CAPTURE,
    Target = MODLOADER_RML_EVENT_PHASE_TARGET,
    Bubble = MODLOADER_RML_EVENT_PHASE_BUBBLE,
};

struct Event;
using Listener = Function<void(Event& event)>;

// Borrowed handle; operations fail after the element is removed or its document/window is closed
class Element {
public:
    Element() = default;
    explicit Element(Rml::Handle handle) : handle(handle) {
    }

    // Searches descendants; missing elements return a zero handle Query
    Element By_Id(std::string_view id) const;
    Element Query(std::string_view selector) const; // uses RCSS selectors
    std::vector<Element> Query_All(std::string_view selector) const;
    Element Parent() const;
    Element Child(u32 index) const;
    u32 Child_Count() const;
    Element Next() const;
    Element Previous() const;
    Element Owner_Document() const;

    Element Append(std::string_view tag) const; // Creates a child owned by this element
    void Remove() const; // Removes this element and its descendants/listeners

    void Set_Inner_Rml(std::string_view rml) const;
    std::string Inner_Rml() const;
    void Set_Attribute(std::string_view name, std::string_view value) const;
    void Remove_Attribute(std::string_view name) const;
    std::optional<std::string> Attribute(std::string_view name) const;
    bool Set_Property(std::string_view name, std::string_view value) const;
    void Remove_Property(std::string_view name) const;
    std::optional<std::string> Property(std::string_view name) const;
    void Set_Class(std::string_view name, bool set) const;
    bool Has_Class(std::string_view name) const;
    void Set_Id(std::string_view id) const;
    std::string Id() const;
    std::string Tag() const;
    bool Set_Value(std::string_view value) const;
    std::optional<std::string> Value() const;

    bool Focus() const;
    void Blur() const;
    void Click() const;
    void Scroll_Into_View(bool top = true) const;
    void Scroll_To(f32 left, f32 top) const;
    Box Get_Box() const;

    // returns 0 on failure
    Rml::Handle Listen(std::string_view event, Listener listener, bool capture = false) const;
    [[nodiscard]] Subscription Subscribe(std::string_view event, Listener listener, bool capture = false) const;

    // newly loaded documents start hidden
    void Show(bool modal = false) const;
    void Hide() const;
    void Close() const; // Unloads the document and invalidates its elements/listeners

    Rml::Handle Handle() const {
        return handle;
    }

    bool Is_Valid() const {
        return handle != 0;
    }

    bool Same(Element other) const {
        return handle == other.handle;
    }

private:
    Rml::Handle handle = 0;
};

using Document = Element;

struct Event {
    std::string_view type; // "click", "change", "keydown", "textinput", etc.
    Element target; // where it happened
    Element current; // the element listened to
    Phase phase;
    u32 modifiers; // Modifier flags
    f32 mouseX; // mouse events: pixels of the window
    f32 mouseY;
    f32 wheelX; // mousescroll: right and down are positive
    f32 wheelY;
    s32 button; // mouse events: 0 left, 1 right, 2 middle; -1 none
    Key key; // key events
    bool stop; // set by the listener: the event goes no further
    bool stopImmediate; // not even to the element's other listeners

    std::optional<std::string_view> Parameter(std::string_view name) const;
    std::optional<std::string_view> Argument(u32 index) const;

    std::span<const char> parameters; // "name\0value\0" pairs
    std::span<const char> arguments; // "value\0" each
    u32 argumentCount;
};

void Unlisten(Handle listener); // Safe within the callback

// Parses RML text; returns a hidden document
Document Load_Document(const Ui::Window& window, std::string_view rml);
Document Load_Document_File(const Ui::Window& window, std::string_view path);
bool Load_Font(std::string_view path, bool fallback = false);

enum class Type : u32 {
    S32 = MODLOADER_RML_S32,
    U32 = MODLOADER_RML_U32,
    F32 = MODLOADER_RML_F32,
    F64 = MODLOADER_RML_F64,
    Bool = MODLOADER_RML_BOOL, // one byte
    String = MODLOADER_RML_STRING, // char[size], terminated
};

class Model {
public:
    Model() = default;
    explicit Model(Rml::Handle handle)
        : handle(handle) {
    }

    static Model Create(const Ui::Window& window, std::string_view name);

    bool Bind(std::string_view name, s32* variable) const;
    bool Bind(std::string_view name, u32* variable) const;
    bool Bind(std::string_view name, f32* variable) const;
    bool Bind(std::string_view name, f64* variable) const;
    bool Bind(std::string_view name, bool* variable) const;
    bool Bind(std::string_view name, std::span<char> text) const;
    bool Bind_Array(std::string_view name, Type type, void* first, u32 stride, const u32* count, u32 size = 0) const;
    // data-event-click="name(arguments)" supplies Event::Argument; the window owns the listener
    bool Bind_Event(std::string_view name, Listener listener) const;
    // Empty name marks every variable dirty
    void Dirty(std::string_view name = {}) const;

    Rml::Handle Handle() const {
        return handle;
    }

    bool Is_Valid() const {
        return handle != 0;
    }

private:
    bool Bind_Raw(std::string_view name, Type type, void* variable, u32 size) const;

    Rml::Handle handle = 0;
};

} // namespace ModLoader::Rml
