#pragma once

#include <modloader/types.h>

#include <string>
#include <string_view>

struct yyjson_mut_doc;
struct yyjson_mut_val;

namespace ModLoader::Json {

class Value {
public:
    Value() = default;
    Value(yyjson_mut_doc* document, yyjson_mut_val* raw) : document(document) , raw(raw) {
    }

    bool Is_Null() const;
    bool Is_Object() const;
    bool Is_Array() const;

    // Getters use the fallback for a missing key or mismatched type
    Value Get(std::string_view key) const; // Missing keys return a null Value
    bool Get_Bool(std::string_view key, bool fallback) const;
    s64 Get_Int(std::string_view key, s64 fallback) const;
    f64 Get_Float(std::string_view key, f64 fallback) const;
    std::string_view Get_Text(std::string_view key, std::string_view fallback) const;

    // Text setters copy into the document
    void Set_Bool(std::string_view key, bool value) const;
    void Set_Int(std::string_view key, s64 value) const;
    void Set_Float(std::string_view key, f64 value) const;
    void Set_Text(std::string_view key, std::string_view value) const;
    Value Set_Object(std::string_view key) const;
    Value Set_Array(std::string_view key) const;
    void Remove(std::string_view key) const;

    // Array operations; non arrays have Count() == 0 and return null Values
    usize Count() const;
    Value At(usize index) const;
    Value Append_Object() const;
    void Append_Int(s64 value) const;
    void Append_Text(std::string_view value) const;

    bool To_Bool(bool fallback) const;
    s64 To_Int(s64 fallback) const;
    f64 To_Float(f64 fallback) const;
    std::string_view To_Text(std::string_view fallback) const;

    yyjson_mut_val* Raw() const {
        return raw;
    }

private:
    void Put(std::string_view key, yyjson_mut_val* value) const;

    yyjson_mut_doc* document = nullptr;
    yyjson_mut_val* raw = nullptr;
};

class Document {
public:
    Document() = default;
    explicit Document(yyjson_mut_doc* raw)
        : raw(raw) {
    }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&& other) noexcept;
    Document& operator=(Document&& other) noexcept;
    ~Document();

    static Document Create(); // Creates an empty object root
    static Document Parse(std::string_view text);

    std::string Write() const;
    Value Root() const;

    bool Is_Valid() const {
        return raw != nullptr;
    }

    // Borrowed; do not free!
    yyjson_mut_doc* Raw() const {
        return raw;
    }

private:
    yyjson_mut_doc* raw = nullptr;
};

Document Load(std::string_view path);
bool Save(std::string_view path, const Document& document);

} // namespace ModLoader::Json



