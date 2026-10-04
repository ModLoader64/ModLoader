#include <modloader/io/json.h>
#include <modloader/detail/mounts.h>
#include <modloader/logger.h>

#include <yyjson.h>
#include <cstdlib>
#include <utility>

using namespace ModLoader::Json;

Document::Document(Document&& other) noexcept : raw(std::exchange(other.raw, nullptr)) {
}

Document& Document::operator=(Document&& other) noexcept {
    if (this != &other) {
        yyjson_mut_doc_free(raw);
        raw = std::exchange(other.raw, nullptr);
    }
    return *this;
}

Document::~Document() {
    yyjson_mut_doc_free(raw);
}

Document Document::Create() {
    Document document(yyjson_mut_doc_new(nullptr));

    if (document.raw != nullptr) {
        yyjson_mut_doc_set_root(document.raw, yyjson_mut_obj(document.raw));
    }
    return document;
}

Document Document::Parse(std::string_view text) {
    yyjson_read_err error = {};
    yyjson_read_flag flags = YYJSON_READ_ALLOW_TRAILING_COMMAS | YYJSON_READ_ALLOW_COMMENTS;
    yyjson_doc* read = yyjson_read_opts(const_cast<char*>(text.data()), text.size(), flags, nullptr, &error);
    Document document;

    if (read == nullptr) {
        Logger::Error("JSON: %s at byte %llu", error.msg, static_cast<unsigned long long>(error.pos));
        return document;
    }
    document.raw = yyjson_doc_mut_copy(read, nullptr);
    yyjson_doc_free(read);
    return document;
}

Document ModLoader::Detail::Json_Load(std::string_view folder, std::string_view path) {
    std::optional<std::vector<u8>> contents = File_Read_All(folder, path);

    if (!contents) {
        return Document();
    }
    return Document::Parse(std::string_view(reinterpret_cast<const char*>(contents->data()), contents->size()));
}

bool ModLoader::Detail::Json_Save(std::string_view folder, std::string_view path, const Json::Document& document) {
    std::string text = document.Write();

    return !text.empty() && File_Write_All(folder, path, text.data(), text.size());
}

std::string Document::Write() const {
    size_t length = 0;
    char* written = raw != nullptr ? yyjson_mut_write(raw, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, &length) : nullptr;
    std::string text = written != nullptr ? std::string(written, length) : std::string();

    free(written);
    return text;
}

Value Document::Root() const {
    return Value(raw, raw != nullptr ? yyjson_mut_doc_get_root(raw) : nullptr);
}

bool Value::Is_Null() const {
    return raw == nullptr || yyjson_mut_is_null(raw);
}

bool Value::Is_Object() const {
    return yyjson_mut_is_obj(raw);
}

bool Value::Is_Array() const {
    return yyjson_mut_is_arr(raw);
}

Value Value::Get(std::string_view key) const {
    return Value(document, yyjson_mut_obj_getn(raw, key.data(), key.size()));
}

bool Value::Get_Bool(std::string_view key, bool fallback) const {
    return Get(key).To_Bool(fallback);
}

s64 Value::Get_Int(std::string_view key, s64 fallback) const {
    return Get(key).To_Int(fallback);
}

f64 Value::Get_Float(std::string_view key, f64 fallback) const {
    return Get(key).To_Float(fallback);
}

std::string_view Value::Get_Text(std::string_view key, std::string_view fallback) const {
    return Get(key).To_Text(fallback);
}

void Value::Put(std::string_view key, yyjson_mut_val* value) const {
    if (!yyjson_mut_is_obj(raw) || value == nullptr) {
        return;
    }
    yyjson_mut_obj_remove_keyn(raw, key.data(), key.size());
    yyjson_mut_obj_add(raw, yyjson_mut_strncpy(document, key.data(), key.size()), value);
}

void Value::Set_Bool(std::string_view key, bool value) const {
    Put(key, yyjson_mut_bool(document, value));
}

void Value::Set_Int(std::string_view key, s64 value) const {
    Put(key, yyjson_mut_sint(document, value));
}

void Value::Set_Float(std::string_view key, f64 value) const {
    Put(key, yyjson_mut_real(document, value));
}

void Value::Set_Text(std::string_view key, std::string_view value) const {
    Put(key, yyjson_mut_strncpy(document, value.data(), value.size()));
}

Value Value::Set_Object(std::string_view key) const {
    yyjson_mut_val* value = yyjson_mut_obj(document);

    Put(key, value);
    return Value(document, value);
}

Value Value::Set_Array(std::string_view key) const {
    yyjson_mut_val* value = yyjson_mut_arr(document);

    Put(key, value);
    return Value(document, value);
}

void Value::Remove(std::string_view key) const {
    yyjson_mut_obj_remove_keyn(raw, key.data(), key.size());
}

usize Value::Count() const {
    return yyjson_mut_is_arr(raw) ? yyjson_mut_arr_size(raw) : 0;
}

Value Value::At(usize index) const {
    return Value(document, yyjson_mut_is_arr(raw) ? yyjson_mut_arr_get(raw, index) : nullptr);
}

Value Value::Append_Object() const {
    yyjson_mut_val* value = yyjson_mut_obj(document);

    if (!yyjson_mut_arr_append(raw, value)) {
        return Value();
    }
    return Value(document, value);
}

void Value::Append_Int(s64 value) const {
    yyjson_mut_arr_append(raw, yyjson_mut_sint(document, value));
}

void Value::Append_Text(std::string_view value) const {
    yyjson_mut_arr_append(raw, yyjson_mut_strncpy(document, value.data(), value.size()));
}

bool Value::To_Bool(bool fallback) const {
    return yyjson_mut_is_bool(raw) ? yyjson_mut_get_bool(raw) : fallback;
}

s64 Value::To_Int(s64 fallback) const {
    if (yyjson_mut_is_int(raw)) {
        return yyjson_mut_get_int(raw);
    }
    return yyjson_mut_is_real(raw) ? static_cast<s64>(yyjson_mut_get_real(raw)) : fallback;
}

f64 Value::To_Float(f64 fallback) const {
    return yyjson_mut_is_num(raw) ? yyjson_mut_get_num(raw) : fallback;
}

std::string_view Value::To_Text(std::string_view fallback) const {
    return yyjson_mut_is_str(raw) ? std::string_view(yyjson_mut_get_str(raw), yyjson_mut_get_len(raw)) : fallback;
}

