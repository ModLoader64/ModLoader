#include <modloader/detail/mounts.h>
#include "../internal.h"
#include "../read_text.h"
#include <cstring>
#include <cstdlib>
#include <vector>

#include <modloader/ui/rml.h>

#include <memory>
#include <new>
#include <utility>

using namespace ModLoader::Rml;

static_assert(sizeof(Element) == sizeof(Handle));

namespace {

struct Listener_Record {
    Listener callback;
    std::weak_ptr<ModLoader::Detail::Subscription_State> lifetime;
    bool retired = false;
};

[[clang::no_destroy]] std::vector<Listener_Record*> sDying;
u32 sDepth;

Listener_Record* Create_Listener(Listener listener) {
    if (!listener) {
        return nullptr;
    }
    return new (std::nothrow) Listener_Record { std::move(listener), {}, false };
}

void Let_Go(Listener_Record* listener) {
    if (listener->retired) {
        return;
    }

    listener->retired = true;
    if (auto state = listener->lifetime.lock()) {
        state->Dismiss();
    }
    if (sDepth == 0) {
        delete listener;
        return;
    }
    sDying.push_back(listener);
}

Handle Add_Listener(Handle element, std::string_view event, Listener listener, bool capture,
    std::weak_ptr<ModLoader::Detail::Subscription_State> lifetime = {}) {
    auto* record = Create_Listener(std::move(listener));
    if (record == nullptr) {
        return 0;
    }

    record->lifetime = std::move(lifetime);
    Handle listening = ModLoader_Host_Rml_Element_Listen(element, event.data(), event.size(), capture ? 1 : 0, reinterpret_cast<u64>(record));
    if (listening == 0) {
        delete record;
    }
    return listening;
}

// nullptr requests removal; an empty string sets an empty value
const char* Given(std::string_view value) {
    return value.data() != nullptr ? value.data() : "";
}

bool Set(Element element, u32 what, std::string_view name, const char* value, u64 value_length) {
    return ModLoader_Host_Rml_Element_Set(element.Handle(), what, name.data(), name.size(), value, value_length) != 0;
}

std::optional<std::string> Get(Element element, u32 what, std::string_view name) {
    return ModLoader::Runtime::Read_Text([&](char* buffer, u64 capacity) {
        return ModLoader_Host_Rml_Element_Get(element.Handle(), what, name.data(), name.size(), buffer, capacity);
    });
}

Element Relative(Element element, u32 relation, u32 index) {
    return Element(ModLoader_Host_Rml_Element_Relative(element.Handle(), relation, index));
}

std::string_view Next_Text(const char*& cursor, const char* end) {
    usize length = strnlen(cursor, static_cast<usize>(end - cursor));
    std::string_view text(cursor, length);

    cursor += length;
    if (cursor < end) {
        ++cursor;
    }
    return text;
}

} // namespace

void ModLoader::Runtime::Rml_Frame_End() {
    if (sDepth != 0) {
        return;
    }

    while (!sDying.empty()) {
        Listener_Record* listener = sDying.back();
        sDying.pop_back();
        delete listener;
    }
}

u32 ModLoader::Runtime::Rml_Event(u64 listener, const u8* buffer, u64 size) {
    Listener_Record* handler = reinterpret_cast<Listener_Record*>(listener);
    ModLoader_Rml_Event raw;
    Rml::Event event = {};
    usize parameter_size;
    const char* cursor;
    const char* end;

    if (handler == nullptr || handler->retired) {
        return 0;
    }

    if (size == 0) {
        Let_Go(handler);
        return 0;
    }

    if (size < sizeof(raw) || size == SIZE_MAX) {
        return 0;
    }

    char* record = static_cast<char*>(malloc(static_cast<usize>(size) + 1));
    if (record == nullptr) {
        return 0;
    }

    __builtin_memcpy(record, buffer, size);
    record[size] = '\0';
    record[sizeof(raw.type) - 1] = '\0';
    __builtin_memcpy(&raw, record, sizeof(raw));
    event.type = record;
    event.target = Element(raw.target);
    event.current = Element(raw.current);
    event.phase = static_cast<Phase>(raw.phase);
    event.modifiers = raw.modifiers;
    event.mouseX = raw.mouseX;
    event.mouseY = raw.mouseY;
    event.wheelX = raw.wheelX;
    event.wheelY = raw.wheelY;
    event.button = raw.button;
    event.key = static_cast<Key>(raw.key);
    parameter_size = raw.parameterSize <= size - sizeof(raw) ? raw.parameterSize : size - sizeof(raw);
    event.parameters = std::span<const char>(record + sizeof(raw), parameter_size);
    event.arguments = std::span<const char>(record + sizeof(raw) + parameter_size, record + size);
    end = event.arguments.data() + event.arguments.size();
    cursor = event.arguments.data();
    while (event.argumentCount < raw.argumentCount && cursor < end) {
        Next_Text(cursor, end);
        event.argumentCount++;
    }

    sDepth++;
    handler->callback(event);
    sDepth--;
    Rml_Frame_End();
    free(record);
    return (event.stop ? MODLOADER_RML_STOP : 0) | (event.stopImmediate ? MODLOADER_RML_STOP_IMMEDIATE : 0);
}

std::optional<std::string_view> Event::Parameter(std::string_view name) const {
    const char* cursor = parameters.data();
    const char* end = parameters.data() + parameters.size();

    while (cursor < end) {
        std::string_view key = Next_Text(cursor, end);

        if (cursor >= end) {
            return std::nullopt;
        }

        if (key == name) {
            return Next_Text(cursor, end);
        }

        Next_Text(cursor, end);
    }
    return std::nullopt;
}

std::optional<std::string_view> Event::Argument(u32 index) const {
    const char* cursor = arguments.data();
    const char* end = arguments.data() + arguments.size();

    if (index >= argumentCount) {
        return std::nullopt;
    }

    for (u32 skipped = 0; skipped < index; skipped++) {
        Next_Text(cursor, end);
    }

    return Next_Text(cursor, end);
}

Document ModLoader::Detail::Rml_Load_Document(std::string_view folder, const Ui::Window& window, std::string_view rml) {
    return Document(ModLoader_Host_Rml_Document_Load(window.Handle(), folder.data(), folder.size(), rml.data(), rml.size(), 0));
}

Document ModLoader::Detail::Rml_Load_Document_File(std::string_view folder, const Ui::Window& window, std::string_view path) {
    return Document(ModLoader_Host_Rml_Document_Load(window.Handle(), folder.data(), folder.size(), path.data(), path.size(), 1));
}

bool ModLoader::Detail::Rml_Load_Font(std::string_view folder, std::string_view path, bool fallback) {
    return ModLoader_Host_Rml_Font_Load(folder.data(), folder.size(), path.data(), path.size(), fallback ? 1 : 0) != 0;
}

void ModLoader::Rml::Unlisten(Rml::Handle listener) {
    ModLoader_Host_Rml_Element_Unlisten(listener);
}

void Element::Show(bool modal) const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_SHOW, modal ? 2 : 1);
}

void Element::Hide() const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_SHOW, 0);
}

void Element::Close() const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_CLOSE, 0);
}

Element Element::By_Id(std::string_view id) const {
    return Element(ModLoader_Host_Rml_Element_Find(handle, 0, id.data(), id.size()));
}

Element Element::Query(std::string_view selector) const {
    return Element(ModLoader_Host_Rml_Element_Find(handle, 1, selector.data(), selector.size()));
}

std::vector<Element> Element::Query_All(std::string_view selector) const {
    u32 count = ModLoader_Host_Rml_Element_Find_All(handle, selector.data(), selector.size(), nullptr, 0);
    std::vector<Element> found(count);
    if (count != 0) {
        ModLoader_Host_Rml_Element_Find_All(handle, selector.data(), selector.size(), reinterpret_cast<Rml::Handle*>(found.data()), count);
    }
    return found;
}

Element Element::Parent() const {
    return Relative(*this, MODLOADER_RML_PARENT, 0);
}

Element Element::Child(u32 index) const {
    return Relative(*this, MODLOADER_RML_CHILD, index);
}

u32 Element::Child_Count() const {
    return ModLoader_Host_Rml_Element_Child_Count(handle);
}

Element Element::Next() const {
    return Relative(*this, MODLOADER_RML_NEXT, 0);
}

Element Element::Previous() const {
    return Relative(*this, MODLOADER_RML_PREVIOUS, 0);
}

Element Element::Owner_Document() const {
    return Relative(*this, MODLOADER_RML_DOCUMENT, 0);
}

Element Element::Append(std::string_view tag) const {
    return Element(ModLoader_Host_Rml_Element_Append(handle, tag.data(), tag.size()));
}

void Element::Remove() const {
    ModLoader_Host_Rml_Element_Remove(handle);
}

void Element::Set_Inner_Rml(std::string_view rml) const {
    Set(*this, MODLOADER_RML_INNER_RML, {}, Given(rml), rml.size());
}

std::string Element::Inner_Rml() const {
    return Get(*this, MODLOADER_RML_INNER_RML, {}).value_or(std::string());
}

void Element::Set_Attribute(std::string_view name, std::string_view value) const {
    Set(*this, MODLOADER_RML_ATTRIBUTE, name, Given(value), value.size());
}

void Element::Remove_Attribute(std::string_view name) const {
    Set(*this, MODLOADER_RML_ATTRIBUTE, name, nullptr, 0);
}

std::optional<std::string> Element::Attribute(std::string_view name) const {
    return Get(*this, MODLOADER_RML_ATTRIBUTE, name);
}

bool Element::Set_Property(std::string_view name, std::string_view value) const {
    return Set(*this, MODLOADER_RML_PROPERTY, name, Given(value), value.size());
}

void Element::Remove_Property(std::string_view name) const {
    Set(*this, MODLOADER_RML_PROPERTY, name, nullptr, 0);
}

std::optional<std::string> Element::Property(std::string_view name) const {
    return Get(*this, MODLOADER_RML_PROPERTY, name);
}

void Element::Set_Class(std::string_view name, bool set) const {
    Set(*this, MODLOADER_RML_CLASS, name, set ? "1" : nullptr, set ? 1 : 0);
}

bool Element::Has_Class(std::string_view name) const {
    return ModLoader_Host_Rml_Element_Get(handle, MODLOADER_RML_CLASS, name.data(), name.size(), nullptr, 0) > 0;
}

void Element::Set_Id(std::string_view id) const {
    Set(*this, MODLOADER_RML_ID, {}, Given(id), id.size());
}

std::string Element::Id() const {
    return Get(*this, MODLOADER_RML_ID, {}).value_or(std::string());
}

std::string Element::Tag() const {
    return Get(*this, MODLOADER_RML_TAG, {}).value_or(std::string());
}

bool Element::Set_Value(std::string_view value) const {
    return Set(*this, MODLOADER_RML_VALUE, {}, Given(value), value.size());
}

std::optional<std::string> Element::Value() const {
    return Get(*this, MODLOADER_RML_VALUE, {});
}

bool Element::Focus() const {
    return ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_FOCUS, 0) != 0;
}

void Element::Blur() const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_BLUR, 0);
}

void Element::Click() const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_CLICK, 0);
}

void Element::Scroll_Into_View(bool top) const {
    ModLoader_Host_Rml_Element_Action(handle, MODLOADER_RML_SCROLL_INTO_VIEW, top ? 1 : 0);
}

void Element::Scroll_To(f32 left, f32 top) const {
    ModLoader_Host_Rml_Element_Scroll(handle, left, top);
}

Box Element::Get_Box() const {
    Box box = {};

    if (ModLoader_Host_Rml_Element_Box(handle, &box) == 0) {
        return {};
    }
    return box;
}

ModLoader::Rml::Handle Element::Listen(std::string_view event, Listener listener, bool capture) const {
    return Add_Listener(handle, event, std::move(listener), capture);
}

ModLoader::Subscription Element::Subscribe(std::string_view event, Listener listener, bool capture) const {
    auto state = std::make_shared<ModLoader::Detail::Subscription_State>();
    Rml::Handle listening = Add_Listener(handle, event, std::move(listener), capture, state);
    if (listening == 0) {
        return {};
    }

    state->cleanup = [listening] {
        Unlisten(listening);
    };
    return ModLoader::Subscription(std::move(state));
}

Model Model::Create(const Ui::Window& window, std::string_view name) {
    return Model(ModLoader_Host_Rml_Model_Create(window.Handle(), name.data(), name.size()));
}

bool Model::Bind_Raw(std::string_view name, Type type, void* variable, u32 size) const {
    return ModLoader_Host_Rml_Model_Bind(handle, name.data(), name.size(), static_cast<u32>(type), variable, size, 0, nullptr) != 0;
}

bool Model::Bind(std::string_view name, s32* variable) const {
    return Bind_Raw(name, Type::S32, variable, 0);
}

bool Model::Bind(std::string_view name, u32* variable) const {
    return Bind_Raw(name, Type::U32, variable, 0);
}

bool Model::Bind(std::string_view name, f32* variable) const {
    return Bind_Raw(name, Type::F32, variable, 0);
}

bool Model::Bind(std::string_view name, f64* variable) const {
    return Bind_Raw(name, Type::F64, variable, 0);
}

bool Model::Bind(std::string_view name, bool* variable) const {
    return Bind_Raw(name, Type::Bool, variable, 0);
}

bool Model::Bind(std::string_view name, std::span<char> text) const {
    return text.size() <= UINT32_MAX && Bind_Raw(name, Type::String, text.data(), static_cast<u32>(text.size()));
}

bool Model::Bind_Array(std::string_view name, Type type, void* first, u32 stride, const u32* count, u32 size) const {
    return count != nullptr && ModLoader_Host_Rml_Model_Bind(handle, name.data(), name.size(), static_cast<u32>(type), first, size, stride, count) != 0;
}

bool Model::Bind_Event(std::string_view name, Listener listener) const {
    auto* record = Create_Listener(std::move(listener));
    bool bound = record != nullptr && ModLoader_Host_Rml_Model_Bind_Event(handle, name.data(), name.size(), reinterpret_cast<u64>(record)) != 0;

    if (!bound) {
        delete record;
    }
    return bound;
}

void Model::Dirty(std::string_view name) const {
    ModLoader_Host_Rml_Model_Dirty(handle, name.data(), name.size());
}
