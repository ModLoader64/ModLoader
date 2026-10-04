#include "ui/rml.h"
#include "ui/handle_table.h"
#include "ui/windows.h"
#include "RmlUi/Core.h"
#include "modloader_rml.h"
#include <string.h>
#include <unordered_map>

namespace {

constexpr u32 gMaxMisuses = 8;

class Module_Listener;

struct Element_Entry {
    Rml::Element* element;
    Module* owner;
};

struct Model_Entry {
    Rml::Context* context;
    Module* owner;
    Rml::DataModelConstructor constructor;
    std::vector<std::unique_ptr<Rml::VariableDefinition>> definitions;
    std::vector<u64> listeners;
};

Handle_Table<Element_Entry> sElements;
std::unordered_map<Rml::Element*, u32> sElementIndex;
Handle_Table<Module_Listener*> sListeners;
Handle_Table<Model_Entry> sModels;
u32 sMisuses;

u32 Element_Handle(Module* owner, Rml::Element* element) {
    auto found = element != nullptr ? sElementIndex.find(element) : sElementIndex.end();
    u32 handle;

    if (element == nullptr || owner == nullptr) {
        return 0;
    }

    if (found != sElementIndex.end()) {
        return sElements.At(found->second).owner == owner ? sElements.Handle(found->second) : 0;
    }

    handle = sElements.Add({ element, owner });
    if (handle != 0) {
        sElementIndex[element] = sElements.Index(handle);
    }

    return handle;
}

Rml::Element* Element_Find(const Module* module, u32 handle) {
    Element_Entry* entry = module != nullptr ? sElements.Find(handle) : nullptr;

    return entry != nullptr && entry->owner == module && Ui_Windows_Owner(entry->element->GetContext()) == module ? entry->element : nullptr;
}

class Element_Watch final : public Rml::Plugin {
public:
    int GetEventClasses() override {
        return EVT_ELEMENT;
    }

    void OnElementDestroy(Rml::Element* element) override {
        auto found = sElementIndex.find(element);

        if (found != sElementIndex.end()) {
            sElements.Remove(found->second);
            sElementIndex.erase(found);
        }
    }
};

Element_Watch sWatch;

bool Record_Append(std::vector<u8>& record, const Rml::String& text) {
    if (text.size() >= SIZE_MAX - record.size()) {
        return false;
    }
    usize offset = record.size();
    record.resize(offset + text.size() + 1);
    memcpy(record.data() + offset, text.c_str(), text.size() + 1);
    return true;
}

bool Event_Record(Module* module, Rml::Event& event, const Rml::VariantList* arguments, std::vector<u8>& out) {
    ModLoader_Rml_Event record = {};
    out.resize(sizeof(record));
    Text_Copy(record.type, event.GetType());
    record.target = Element_Handle(module, event.GetTargetElement());
    record.current = Element_Handle(module, event.GetCurrentElement());

    switch (event.GetPhase()) {
    case Rml::EventPhase::None:
        record.phase = MODLOADER_RML_EVENT_PHASE_NONE;
        break;
    case Rml::EventPhase::Capture:
        record.phase = MODLOADER_RML_EVENT_PHASE_CAPTURE;
        break;
    case Rml::EventPhase::Target:
        record.phase = MODLOADER_RML_EVENT_PHASE_TARGET;
        break;
    case Rml::EventPhase::Bubble:
        record.phase = MODLOADER_RML_EVENT_PHASE_BUBBLE;
        break;
    }

    record.modifiers |= event.GetParameter<int>("ctrl_key", 0) != 0 ? MODLOADER_RML_MODIFIER_CTRL : 0;
    record.modifiers |= event.GetParameter<int>("shift_key", 0) != 0 ? MODLOADER_RML_MODIFIER_SHIFT : 0;
    record.modifiers |= event.GetParameter<int>("alt_key", 0) != 0 ? MODLOADER_RML_MODIFIER_ALT : 0;
    record.modifiers |= event.GetParameter<int>("meta_key", 0) != 0 ? MODLOADER_RML_MODIFIER_META : 0;
    record.mouseX = event.GetParameter<float>("mouse_x", 0.0f);
    record.mouseY = event.GetParameter<float>("mouse_y", 0.0f);
    record.wheelX = event.GetParameter<float>("wheel_delta_x", 0.0f);
    record.wheelY = event.GetParameter<float>("wheel_delta_y", 0.0f);
    record.button = event.GetParameter<int>("button", MODLOADER_RML_EVENT_BUTTON_NONE);
    record.key = static_cast<u32>(event.GetParameter<int>("key_identifier", 0));

    for (const auto& [name, value] : event.GetParameters()) {
        if (!Record_Append(out, name) || !Record_Append(out, value.Get<Rml::String>())) {
            return false;
        }
    }

    if (out.size() - sizeof(record) > UINT32_MAX || (arguments != nullptr && arguments->size() > UINT32_MAX)) {
        return false;
    }

    record.parameterSize = static_cast<u32>(out.size() - sizeof(record));
    for (usize index = 0; arguments != nullptr && index < arguments->size(); index++) {
        if (!Record_Append(out, (*arguments)[index].Get<Rml::String>())) {
            return false;
        }
        record.argumentCount++;
    }

    memcpy(out.data(), &record, sizeof(record));
    return true;
}

void Deliver(Module* owner, u64 listener, Rml::Event& event, const Rml::VariantList* arguments) {
    std::vector<u8> record;
    u32 flags;

    if (owner == nullptr || Ui_Windows_Owner(event.GetCurrentElement()->GetContext()) != owner) {
        return;
    }

    if (!Event_Record(owner, event, arguments, record)) {
        return;
    }

    flags = owner->runtime.Ui_Event(*owner, listener, record.data(), record.size());
    if ((flags & MODLOADER_RML_STOP_IMMEDIATE) != 0) {
        event.StopImmediatePropagation();
    }
    else if ((flags & MODLOADER_RML_STOP) != 0) {
        event.StopPropagation();
    }
}

class Module_Listener final : public Rml::EventListener {
public:
    Module_Listener(Module* listening, u64 value, Rml::Element* listened, Rml::String event_type, bool captures) : owner(listening)
        , user(value)
        , context(listened->GetContext())
        , element(listened)
        , type(std::move(event_type))
        , capture(captures) {
    }

    void ProcessEvent(Rml::Event& event) override {
        Deliver(owner, user, event, nullptr);
    }

    void OnDetach(Rml::Element*) override {
        if (Ui_Windows_Owner(context) == owner) {
            owner->runtime.Ui_Event(*owner, user, nullptr, 0);
        }

        sListeners.Remove(index);
        delete this;
    }

    Module* owner;
    u64 user;
    Rml::Context* context;
    Rml::Element* element;
    Rml::String type;
    bool capture;
    u32 index = 0;
};

class Module_Scalar final : public Rml::VariableDefinition {
public:
    Module_Scalar(Module* bound, u32 scalar_type, u32 scalar_size) : Rml::VariableDefinition(Rml::DataVariableType::Scalar)
        , owner(bound)
        , type(scalar_type)
        , size(scalar_size) {
    }

    bool Get(void* pointer, Rml::Variant& variant) override {
        const u8* memory = owner->Memory_As<u8>(reinterpret_cast<uintptr_t>(pointer), size);

        if (memory == nullptr) {
            return false;
        }

        switch (type) {
        case MODLOADER_RML_S32:
            variant = Load<s32>(memory);
            return true;
        case MODLOADER_RML_U32:
            variant = Load<u32>(memory);
            return true;
        case MODLOADER_RML_F32:
            variant = Load<f32>(memory);
            return true;
        case MODLOADER_RML_F64:
            variant = Load<f64>(memory);
            return true;
        case MODLOADER_RML_BOOL:
            variant = memory[0] != 0;
            return true;
        default:
            variant = Rml::String(reinterpret_cast<const char*>(memory), strnlen(reinterpret_cast<const char*>(memory), size));
            return true;
        }
    }

    bool Set(void* pointer, const Rml::Variant& variant) override {
        u8* memory = owner->Memory_As<u8>(reinterpret_cast<uintptr_t>(pointer), size);
        Rml::String text;

        if (memory == nullptr) {
            return false;
        }

        switch (type) {
        case MODLOADER_RML_S32:
            Store(memory, variant.Get<int>());
            return true;
        case MODLOADER_RML_U32:
            Store(memory, variant.Get<unsigned int>());
            return true;
        case MODLOADER_RML_F32:
            Store(memory, variant.Get<float>());
            return true;
        case MODLOADER_RML_F64:
            Store(memory, variant.Get<double>());
            return true;
        case MODLOADER_RML_BOOL:
            memory[0] = variant.Get<bool>() ? 1 : 0;
            return true;
        default:
            text = variant.Get<Rml::String>();
            Text_Copy({ reinterpret_cast<char*>(memory), size }, text);
            return true;
        }
    }

private:
    template <typename T>
    static T Load(const u8* memory) {
        T value;

        memcpy(&value, memory, sizeof(value));
        return value;
    }

    template <typename T>
    static void Store(u8* memory, T value) {
        memcpy(memory, &value, sizeof(value));
    }

    Module* owner;
    u32 type; // MODLOADER_RML_S32, etc
    u32 size;
};

class Module_Array final : public Rml::VariableDefinition {
public:
    Module_Array(Module* bound, Module_Scalar* item_definition, u64 first_item, u64 item_stride, u64 count_address) : Rml::VariableDefinition(Rml::DataVariableType::Array)
        , owner(bound)
        , item(item_definition)
        , first(first_item)
        , stride(item_stride)
        , count(count_address) {
    }

    int Size(void*) override {
        const u32* items = owner->Memory_As<u32>(count);
        return items != nullptr ? static_cast<int>(*items < static_cast<u32>(INT_MAX) ? *items : INT_MAX) : 0;
    }

    Rml::DataVariable Child(void* pointer, const Rml::DataAddressEntry& address) override {
        if (address.name == "size") {
            return Rml::MakeLiteralIntVariable(Size(pointer));
        }

        if (address.index < 0 || address.index >= Size(pointer) || (stride != 0 && static_cast<u64>(address.index) > (UINT64_MAX - first) / stride)) {
            return {};
        }

        return Rml::DataVariable(item, reinterpret_cast<void*>(static_cast<uintptr_t>(first + address.index * stride)));
    }

private:
    Module* owner;
    Module_Scalar* item;
    u64 first;
    u64 stride;
    u64 count;
};

Model_Entry* Model_Find(const Module* module, u32 handle) {
    Model_Entry* model = module != nullptr ? sModels.Find(handle) : nullptr;
    return model != nullptr && model->owner == module && Ui_Windows_Owner(model->context) == module ? model : nullptr;
}

u32 Scalar_Size(u32 type, u32 size) {
    switch (type) {
    case MODLOADER_RML_S32:
    case MODLOADER_RML_U32:
    case MODLOADER_RML_F32:
        return 4;
    case MODLOADER_RML_F64:
        return 8;
    case MODLOADER_RML_BOOL:
        return 1;
    case MODLOADER_RML_STRING:
        return size;
    default:
        return 0;
    }
}

Module* Ui_Module(wasm_exec_env_t exec_env) {
    Module& module = Module_Of(exec_env);
    if (module.uiContext.execEnv == exec_env) {
        return &module;
    }

    if (sMisuses++ < gMaxMisuses) {
        Log_Warning(module.name.c_str(), "RmlUi requires the UI thread");
    }

    return nullptr;
}

std::optional<Rml::String> Module_String(const Module* module, u64 address, u64 length) {
    std::optional<std::string_view> text = module != nullptr ? module->Text(address, length) : std::nullopt;
    return text ? std::optional<Rml::String>(*text) : std::nullopt;
}

s64 Put_Text(const Module& module, const Rml::String& text, u64 buffer, u64 capacity) {
    char* out = capacity != 0 ? module.Memory_As<char>(buffer, capacity) : nullptr;
    if (out != nullptr) {
        Text_Copy({ out, capacity }, text);
    }

    return static_cast<s64>(text.size());
}

Rml::ElementDocument* As_Document(Rml::Element* element) {
    return element != nullptr && element->GetOwnerDocument() == element ? element->GetOwnerDocument() : nullptr;
}

template <typename Load>
auto Working_For(Module* module, Load load) {
    Module* previous = Ui_Windows_Work_For(module);
    auto result = load();

    Ui_Windows_Work_For(previous);
    return result;
}

std::optional<Rml::String> Resource_Path(Module* module, u64 folder_address, u64 folder_length, std::string_view path) {
    auto folder = module != nullptr ? module->Owner_Folder(folder_address, folder_length) : std::nullopt;
    if (!folder) {
        return std::nullopt;
    }

    bool assets = path.starts_with("assets:/");
    if (assets) {
        path.remove_prefix(8);
    }

    while (path.starts_with('/')) {
        path.remove_prefix(1);
    }

    Rml::String resource = (assets ? "@assets/" : "") + *folder + "/" + std::string(path);
    return Module_File_Path(*module, resource) ? std::optional(std::move(resource)) : std::nullopt;
}

u32 Document_Load(Module* module, u32 window, u64 folder_address, u64 folder_length, u64 address, u64 length, bool from_file) {
    Rml::Context* context = module != nullptr ? Ui_Windows_Context(*module, window) : nullptr;
    std::optional<Rml::String> text = context != nullptr ? Module_String(module, address, length) : std::nullopt;

    if (!text) {
        return 0;
    }

    auto path = Resource_Path(module, folder_address, folder_length, from_file ? *text : "_inline.rml");
    if (!path) {
        return 0;
    }

    return Element_Handle(module, Working_For(module,
        [&] {
            return from_file ? context->LoadDocument(*path) : context->LoadDocumentFromMemory(*text, *path);
        })
    );
}

u64 Element_Find_All(Module* module, u32 root_handle, u64 address, u64 length, u64 out_address, u64 capacity) {
    Rml::Element* root = Element_Find(module, root_handle);
    u32* out = capacity != 0 && root != nullptr ? module->Memory_As<u32>(out_address, capacity) : nullptr;
    std::optional<Rml::String> selector = root != nullptr ? Module_String(module, address, length) : std::nullopt;
    Rml::ElementList found;

    if (!selector || (capacity != 0 && out == nullptr)) {
        return 0;
    }

    root->QuerySelectorAll(found, *selector);
    for (u64 index = 0; index < found.size() && index < capacity; index++) {
        out[index] = Element_Handle(module, found[index]);
    }

    return found.size();
}

Rml::Element* Element_Relative(Rml::Element* element, u32 relation, s32 index) {
    if (element == nullptr) {
        return nullptr;
    }

    switch (relation) {
    case MODLOADER_RML_PARENT:
        return element->GetParentNode();
    case MODLOADER_RML_CHILD:
        return index >= 0 && index < element->GetNumChildren() ? element->GetChild(index) : nullptr;
    case MODLOADER_RML_NEXT:
        return element->GetNextSibling();
    case MODLOADER_RML_PREVIOUS:
        return element->GetPreviousSibling();
    case MODLOADER_RML_DOCUMENT:
        return element->GetOwnerDocument();
    default:
        return nullptr;
    }
}

bool Element_Set(Module* module, u64* slots) {
    Rml::Element* element = Element_Find(module, static_cast<u32>(slots[0]));
    bool valued = slots[4] != 0;
    std::optional<Rml::String> name = element != nullptr ? Module_String(module, slots[2], slots[3]) : std::nullopt;
    std::optional<Rml::String> value = valued ? Module_String(module, slots[4], slots[5]) : Rml::String();
    Rml::ElementFormControl* control;

    if (!name || !value) {
        return false;
    }

    switch (static_cast<u32>(slots[1])) {
    case MODLOADER_RML_INNER_RML:
        element->SetInnerRML(*value);
        return true;
    case MODLOADER_RML_ATTRIBUTE:
        if (valued) {
            element->SetAttribute(*name, *value);
        }
        else {
            element->RemoveAttribute(*name);
        }
        return true;
    case MODLOADER_RML_PROPERTY:
        if (!valued) {
            element->RemoveProperty(*name);
        }
        return !valued || element->SetProperty(*name, *value);
    case MODLOADER_RML_VALUE:
        control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
        if (control != nullptr) {
            control->SetValue(*value);
        }
        return control != nullptr;
    case MODLOADER_RML_CLASS:
        element->SetClass(*name, valued);
        return true;
    case MODLOADER_RML_ID:
        element->SetId(*value);
        return true;
    default:
        return false;
    }
}

s64 Element_Get(Module* module, u64* slots) {
    Rml::Element* element = Element_Find(module, static_cast<u32>(slots[0]));
    std::optional<Rml::String> name = element != nullptr ? Module_String(module, slots[2], slots[3]) : std::nullopt;
    std::optional<Rml::String> text;
    Rml::ElementFormControl* control;
    const Rml::Property* property;

    if (!name) {
        return -1;
    }

    switch (static_cast<u32>(slots[1])) {
    case MODLOADER_RML_INNER_RML:
        text = element->GetInnerRML();
        break;
    case MODLOADER_RML_ATTRIBUTE:
        text = element->HasAttribute(*name) ? std::optional(element->GetAttribute<Rml::String>(*name, "")) : std::nullopt;
        break;
    case MODLOADER_RML_PROPERTY:
        property = element->GetProperty(*name);
        text = property != nullptr ? std::optional(property->ToString()) : std::nullopt;
        break;
    case MODLOADER_RML_VALUE:
        control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
        text = control != nullptr ? std::optional(control->GetValue()) : std::nullopt;
        break;
    case MODLOADER_RML_CLASS:
        text = element->IsClassSet(*name) ? "1" : "";
        break;
    case MODLOADER_RML_TAG:
        text = element->GetTagName();
        break;
    case MODLOADER_RML_ID:
        text = element->GetId();
        break;
    default:
        break;
    }

    return text ? Put_Text(*module, *text, slots[4], slots[5]) : -1;
}

bool Element_Action(Rml::Element* element, u32 action, u32 argument) {
    Rml::ElementDocument* document = As_Document(element);

    if (element == nullptr) {
        return false;
    }

    switch (action) {
    case MODLOADER_RML_FOCUS:
        return element->Focus(true);
    case MODLOADER_RML_BLUR:
        element->Blur();
        return true;
    case MODLOADER_RML_CLICK:
        element->Click();
        return true;
    case MODLOADER_RML_SCROLL_INTO_VIEW:
        element->ScrollIntoView(argument != 0);
        return true;
    case MODLOADER_RML_SHOW:
        if (document != nullptr && argument == 0) {
            document->Hide();
        }
        if (document != nullptr && argument != 0) {
            document->Show(argument == 2 ? Rml::ModalFlag::Modal : Rml::ModalFlag::None);
        }
        return document != nullptr;
    case MODLOADER_RML_CLOSE:
        if (document != nullptr) {
            document->Close();
        }
        return document != nullptr;
    default:
        return false;
    }
}

u32 Element_Listen(Module* module, u32 element_handle, u64 address, u64 length, bool capture, u64 user) {
    Rml::Element* element = Element_Find(module, element_handle);
    std::optional<Rml::String> type = element != nullptr && user != 0 ? Module_String(module, address, length) : std::nullopt;
    Module_Listener* listener;
    u32 handle;

    if (!type || type->empty()) {
        return 0;
    }

    listener = new Module_Listener(module, user, element, *type, capture);
    handle = sListeners.Add(listener);
    if (handle == 0) {
        delete listener;
        return 0;
    }

    listener->index = sListeners.Index(handle);
    element->AddEventListener(*type, listener, capture);
    return handle;
}

u32 Model_Create(Module* module, u32 window, u64 address, u64 length) {
    Rml::Context* context = module != nullptr ? Ui_Windows_Context(*module, window) : nullptr;
    std::optional<Rml::String> name = context != nullptr ? Module_String(module, address, length) : std::nullopt;
    Rml::DataModelConstructor constructor = name ? context->CreateDataModel(*name) : Rml::DataModelConstructor();
    u32 handle;

    if (!name) {
        return 0;
    }

    if (!constructor) {
        Log_Warning(module->name.c_str(), "data model already exists: %s", name->c_str());
        return 0;
    }

    handle = sModels.Add({ context, module, constructor, {}, {} });
    if (handle == 0) {
        context->RemoveDataModel(*name);
    }

    return handle;
}

bool Model_Bind(Module* module, u64* slots) {
    Model_Entry* model = Model_Find(module, static_cast<u32>(slots[0]));
    u32 type = static_cast<u32>(slots[3]);
    u64 address = slots[4];
    u32 size = Scalar_Size(type, static_cast<u32>(slots[5]));
    u64 stride = static_cast<u32>(slots[6]);
    u64 count = slots[7];
    std::optional<Rml::String> name = model != nullptr && size != 0 && address != 0 ? Module_String(module, slots[1], slots[2]) : std::nullopt;
    Module_Scalar* scalar;
    Rml::VariableDefinition* bound;

    if (!name || name->empty()) {
        return false;
    }

    scalar = new Module_Scalar(module, type, size);
    model->definitions.emplace_back(scalar);
    bound = scalar;

    if (count != 0) {
        bound = model->definitions.emplace_back(new Module_Array(module, scalar, address, stride != 0 ? stride : size, count)).get();
    }

    return model->constructor.BindCustomDataVariable(*name, Rml::DataVariable(bound, reinterpret_cast<void*>(static_cast<uintptr_t>(address))));
}

bool Model_Bind_Event(Module* module, u32 model_handle, u64 address, u64 length, u64 user) {
    Model_Entry* model = Model_Find(module, model_handle);
    std::optional<Rml::String> name = model != nullptr && user != 0 ? Module_String(module, address, length) : std::nullopt;
    bool bound;

    if (!name || name->empty()) {
        return false;
    }

    bound = model->constructor.BindEventCallback(*name, [module, user](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
        Deliver(module, user, event, &arguments);
    });
    
    if (bound) {
        model->listeners.push_back(user);
    }

    return bound;
}

NativeSymbol sNatives[] = {
    Native("rml_document_load", "(iIIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Document_Load(Ui_Module(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3], slots[4], static_cast<u32>(slots[5]) != 0);
    }),
    Native("rml_font_load", "(IIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        std::optional<Rml::String> text = Module_String(module, slots[2], slots[3]);
        auto path = text ? Resource_Path(module, slots[0], slots[1], *text) : std::nullopt;
        bool fallback = static_cast<u32>(slots[4]) != 0;

        slots[0] = path && Working_For(module, [&] { return Rml::LoadFontFace(*path, fallback); }) ? 1 : 0;
    }),
    Native("rml_element_find", "(iiII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        Rml::Element* root = Element_Find(module, static_cast<u32>(slots[0]));
        std::optional<Rml::String> text = root != nullptr ? Module_String(module, slots[2], slots[3]) : std::nullopt;

        slots[0] = text ? Element_Handle(module, static_cast<u32>(slots[1]) != 0 ? root->QuerySelector(*text) : root->GetElementById(*text)) : 0;
    }),
    Native("rml_element_find_all", "(iIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Element_Find_All(Ui_Module(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3], static_cast<u32>(slots[4]));
    }),
    Native("rml_element_relative", "(iii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);

        slots[0] = Element_Handle(
            module,
            Element_Relative(Element_Find(module, static_cast<u32>(slots[0])), static_cast<u32>(slots[1]), static_cast<s32>(slots[2]))
        );
    }),
    Native("rml_element_child_count", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Rml::Element* element = Element_Find(Ui_Module(exec_env), static_cast<u32>(slots[0]));

        slots[0] = element != nullptr ? static_cast<u32>(element->GetNumChildren()) : 0;
    }),
    Native("rml_element_append", "(iII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        Rml::Element* parent = Element_Find(module, static_cast<u32>(slots[0]));
        Rml::ElementDocument* document = parent != nullptr ? parent->GetOwnerDocument() : nullptr;
        std::optional<Rml::String> tag = document != nullptr ? Module_String(module, slots[1], slots[2]) : std::nullopt;
        Rml::ElementPtr child = tag ? document->CreateElement(*tag) : nullptr;

        slots[0] = Element_Handle(module, child ? parent->AppendChild(std::move(child)) : nullptr);
    }),
    Native("rml_element_remove", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Rml::Element* element = Element_Find(Ui_Module(exec_env), static_cast<u32>(slots[0]));
        Rml::ElementDocument* document = As_Document(element);
        Rml::Element* parent = element != nullptr ? element->GetParentNode() : nullptr;

        if (document != nullptr) {
            document->Close();
        }
        else if (parent != nullptr) {
            parent->RemoveChild(element);
        }
    }),
    Native("rml_element_set", "(iiIIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Element_Set(Ui_Module(exec_env), slots) ? 1 : 0;
    }),
    Native("rml_element_get", "(iiIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Element_Get(Ui_Module(exec_env), slots));
    }),
    Native("rml_element_action", "(iii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Element_Action(Element_Find(Ui_Module(exec_env), static_cast<u32>(slots[0])), static_cast<u32>(slots[1]), static_cast<u32>(slots[2])) ? 1 : 0;
    }),
    Native("rml_element_box", "(iI)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        Rml::Element* element = Element_Find(module, static_cast<u32>(slots[0]));
        ModLoader_Rml_Box* out = element != nullptr ? module->Memory_As<ModLoader_Rml_Box>(slots[1]) : nullptr;
        Rml::Vector2f offset;
        Rml::Vector2f size;

        slots[0] = out != nullptr ? 1 : 0;
        if (out != nullptr) {
            offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            size = element->GetBox().GetSize(Rml::BoxArea::Border);
            *out = {
                offset.x, offset.y, size.x, size.y, element->GetScrollLeft(), element->GetScrollTop(), element->GetScrollWidth(), element->GetScrollHeight()
            };
        }
    }),
    Native("rml_element_scroll", "(iff)", [](wasm_exec_env_t exec_env, u64* slots) {
        Rml::Element* element = Element_Find(Ui_Module(exec_env), static_cast<u32>(slots[0]));
        f32 left;
        f32 top;

        memcpy(&left, &slots[1], sizeof(left));
        memcpy(&top, &slots[2], sizeof(top));
        if (element != nullptr) {
            element->SetScrollLeft(left);
            element->SetScrollTop(top);
        }
    }),
    Native("rml_element_listen", "(iIIiI)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Element_Listen(Ui_Module(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], static_cast<u32>(slots[3]) != 0, slots[4]);
    }),
    Native("rml_element_unlisten", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        Module_Listener** listener = module != nullptr ? sListeners.Find(static_cast<u32>(slots[0])) : nullptr;

        if (listener != nullptr && (*listener)->owner == module) {
            (*listener)->element->RemoveEventListener((*listener)->type, *listener, (*listener)->capture);
        }
    }),
    Native("rml_model_create", "(iII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Model_Create(Ui_Module(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2]);
    }),
    Native("rml_model_bind", "(iIIiIiiI)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Model_Bind(Ui_Module(exec_env), slots) ? 1 : 0;
    }),
    Native("rml_model_bind_event", "(iIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Model_Bind_Event(Ui_Module(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3]) ? 1 : 0;
    }),
    Native("rml_model_dirty", "(iII)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module* module = Ui_Module(exec_env);
        Model_Entry* model = Model_Find(module, static_cast<u32>(slots[0]));
        std::optional<Rml::String> name = model != nullptr ? Module_String(module, slots[1], slots[2]) : std::nullopt;
        Rml::DataModelHandle handle = model != nullptr ? model->constructor.GetModelHandle() : Rml::DataModelHandle();

        if (name && name->empty()) {
            handle.DirtyAllVariables();
        }
        else if (name) {
            handle.DirtyVariable(*name);
        }
    }),
};

} // namespace

void Rml_Register_Natives() {
    Register_Natives(sNatives, "RmlUi's");
}

void Rml_Start() {
    Rml::RegisterPlugin(&sWatch);
}

void Rml_Stop() {
    Rml::UnregisterPlugin(&sWatch);
    sElements.Clear();
    sElementIndex.clear();
    sListeners.Clear();
    sModels.Clear();
    sMisuses = 0;
}

void Rml_Context_Removed(Rml::Context* context) {
    sModels.For_Each([&](u32 index, Model_Entry& model) {
        if (model.context != context) {
            return;
        }
        if (Ui_Windows_Owner(context) == model.owner) {
            for (u64 listener : model.listeners) {
                model.owner->runtime.Ui_Event(*model.owner, listener, nullptr, 0);
            }
        }
        sModels.Remove(index);
    });
}
