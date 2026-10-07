#include "guest_layout.h"
#include "base/json.h"

#include <algorithm>
#include <unordered_set>

namespace {

std::string Text(yyjson_val* object, const char* name) {
    const char* value = yyjson_get_str(yyjson_obj_get(object, name));
    return value != nullptr ? value : "";
}

std::optional<u64> Number(yyjson_val* object, const char* name) {
    yyjson_val* value = yyjson_obj_get(object, name);
    if (yyjson_is_uint(value)) {
        return yyjson_get_uint(value);
    }
    return std::nullopt;
}

bool Aligned_Size(u64 size, u64 alignment, u64& result) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || size > UINT64_MAX - (alignment - 1)) {
        return false;
    }
    result = (size + alignment - 1) & ~(alignment - 1);
    return true;
}

bool Shift(u64& value, u64 before, u64 after) {
    if (after >= before) {
        if (value > UINT64_MAX - (after - before)) {
            return false;
        }
        value += after - before;
    }
    else {
        if (value < before - after) {
            return false;
        }
        value -= before - after;
    }
    return true;
}

bool Shift_Following(Guest_Record_Layout& record, u64 start, u64 before, u64 after, const Guest_Layout_Field* changed = nullptr, bool reflow = false) {
    if (record.isUnion) {
        return false;
    }

    std::vector<Guest_Layout_Field*> following;
    for (Guest_Layout_Field& field : record.fields) {
        if (&field != changed && !field.absent && field.offset >= start) {
            following.push_back(&field);
        }
    }

    std::ranges::stable_sort(following, {}, &Guest_Layout_Field::offset);
    if (reflow) {
        u64 old_cursor = start;
        u64 new_cursor = start;
        if (!Shift(new_cursor, before, after)) {
            return false;
        }

        for (Guest_Layout_Field* field : following) {
            u64 old_offset = field->offset;
            if (old_offset >= old_cursor) {
                u64 aligned;
                if (!Aligned_Size(old_cursor, field->alignment, aligned)) {
                    return false;
                }
                u64 gap = old_offset > aligned ? old_offset - aligned : 0;
                if (new_cursor > UINT64_MAX - gap || !Aligned_Size(new_cursor + gap, field->alignment, field->offset)) {
                    return false;
                }
            }
            else if (!Shift(field->offset, old_cursor, new_cursor)) {
                return false;
            }

            if (field->size > UINT64_MAX - old_offset || field->size > UINT64_MAX - field->offset) {
                return false;
            }
            old_cursor = std::max(old_cursor, old_offset + field->size);
            new_cursor = std::max(new_cursor, field->offset + field->size);
        }

        u64 aligned;
        if (!Aligned_Size(old_cursor, record.alignment, aligned)) {
            return false;
        }

        u64 gap = record.size > aligned ? record.size - aligned : 0;
        if (new_cursor > UINT64_MAX - gap) {
            return false;
        }

        record.size = new_cursor + gap;
        return true;
    }

    for (Guest_Layout_Field* field : following) {
        if (!Shift(field->offset, before, after)) {
            return false;
        }
    }

    return Shift(record.size, before, after);
}

bool Apply_Edit(Guest_Record_Layout& record, const Guest_Layout_Edit& edit) {
    if (edit.kind == Guest_Layout_Edit::Kind::Record) {
        record.size = edit.size.value_or(record.size);
        record.alignment = edit.alignment.value_or(record.alignment);
        return true;
    }

    if (edit.kind == Guest_Layout_Edit::Kind::Insert || edit.kind == Guest_Layout_Edit::Kind::Remove) {
        u64 offset = *edit.offset;
        u64 size = *edit.size;
        if (size > UINT64_MAX - offset) {
            return false;
        }

        if (edit.kind == Guest_Layout_Edit::Kind::Remove) {
            for (Guest_Layout_Field& field : record.fields) {
                if (field.absent) {
                    continue;
                }

                if (field.offset >= offset && field.offset < offset + size) {
                    if (field.size > offset + size - field.offset) {
                        return false;
                    }
                    field.absent = true;
                }
                else if (field.offset < offset && field.size > offset - field.offset) {
                    return false;
                }
            }

            return Shift_Following(record, offset + size, size, 0);
        }

        return Shift_Following(record, offset, 0, size);
    }

    auto found = std::ranges::find(record.fields, edit.field, &Guest_Layout_Field::name);
    if (found == record.fields.end()) {
        return false;
    }

    Guest_Layout_Field& field = *found;
    u64 offset = edit.offset.value_or(field.offset);
    u64 size = edit.absent ? 0 : edit.size.value_or(field.size);
    u64 alignment = edit.alignment.value_or(field.alignment);
    if (edit.alignment && !edit.offset && !Aligned_Size(offset, alignment, offset)) {
        return false;
    }

    if (size != field.size && !edit.absent && field.kind != "array" && field.kind != "record" && field.kind != "union") {
        return false;
    }
    
    if (size > UINT64_MAX - offset || field.size > UINT64_MAX - field.offset) {
        return false;
    }

    if (edit.shiftFollowing && !Shift_Following(record, field.offset + field.size, field.offset + field.size, offset + size, &field, true)) {
        return false;
    }

    field.offset = offset;
    field.size = size;
    field.alignment = alignment;
    field.absent = edit.absent;
    if (edit.alignment) {
        record.alignment = std::max(record.alignment, alignment);
    }
    return true;
}

class Resolver {
public:
    const std::unordered_map<std::string, Guest_Record_Layout>& baseline;
    const Guest_Layout_Profile& profile;
    std::unordered_map<std::string, Guest_Record_Layout> resolved;
    std::unordered_set<std::string> resolving;
    std::string& error;

    bool Record(const std::string& name) {
        if (resolved.contains(name)) {
            return true;
        }

        auto original = baseline.find(name);
        if (original == baseline.end() || !resolving.insert(name).second) {
            error = "missing record: " + name;
            return false;
        }

        Guest_Record_Layout record = original->second;
        for (Guest_Layout_Field& field : record.fields) {
            if (field.record.empty()) {
                continue;
            }

            if (!Record(field.record)) {
                return false;
            }

            const Guest_Record_Layout& child = resolved.at(field.record);
            if (field.count != 0 && child.size > UINT64_MAX / field.count) {
                return false;
            }

            u64 size = child.size * field.count;
            u64 old_end = field.offset + field.size;
            u64 child_alignment = child.alignment;
            if (field.alignment < baseline.at(field.record).alignment) {
                child_alignment = std::min(child_alignment, field.alignment);
            }

            if (child_alignment != field.alignment) {
                field.alignment = child_alignment;
                record.alignment = std::max(record.alignment, field.alignment);
                if (!Aligned_Size(field.offset, field.alignment, field.offset)) {
                    return false;
                }
            }

            if (size > UINT64_MAX - field.offset ||
                ((size != field.size || field.offset + size != old_end) && !record.isUnion &&
                !Shift_Following(record, old_end, old_end, field.offset + size, &field, true))) {
                return false;
            }
            field.size = size;
        }

        std::optional<u64> explicit_size;
        for (const Guest_Layout_Edit& edit : profile.edits) {
            if (edit.record != name) {
                continue;
            }

            if (!Apply_Edit(record, edit)) {
                error = "invalid layout edit: " + name + (edit.field.empty() ? "" : "." + edit.field);
                return false;
            }

            if (edit.kind == Guest_Layout_Edit::Kind::Record && edit.size) {
                explicit_size = edit.size;
            }
        }

        u64 end = 0;
        for (const Guest_Layout_Field& field : record.fields) {
            if (!field.absent) {
                if (field.size > UINT64_MAX - field.offset) {
                    return false;
                }
                end = std::max(end, field.offset + field.size);
            }
        }
        
        if (explicit_size) {
            if (*explicit_size < end) {
                error = "record size does not contain its fields: " + name;
                return false;
            }
            record.size = *explicit_size;
        }
        else if (!Aligned_Size(record.isUnion ? end : std::max(record.size, end), record.alignment, record.size)) {
            return false;
        }

        resolving.erase(name);
        resolved.emplace(name, std::move(record));
        return true;
    }
};

} // namespace

bool Guest_Layouts::Register(std::string_view description, std::string& error) {
    Json_Document document(yyjson_read(description.data(), description.size(), 0));
    yyjson_val* object = document ? yyjson_doc_get_root(document.get()) : nullptr;
    Guest_Record_Layout record;
    record.name = Text(object, "record");
    record.size = Number(object, "size").value_or(0);
    record.alignment = Number(object, "align").value_or(1);
    record.isUnion = yyjson_get_bool(yyjson_obj_get(object, "union"));
    yyjson_val* fields = yyjson_obj_get(object, "fields");
    if (record.name.empty() || !yyjson_is_arr(fields)) {
        error = "bogus";
        return false;
    }

    size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(fields, index, count, value) {
        Guest_Layout_Field field;
        field.name = Text(value, "name");
        field.record = Text(value, "record");
        field.kind = Text(value, "kind");
        field.offset = Number(value, "offset").value_or(0);
        field.size = Number(value, "size").value_or(0);
        field.alignment = Number(value, "align").value_or(1);
        field.count = Number(value, "count").value_or(1);
        record.fields.push_back(std::move(field));
    }

    std::lock_guard guard(lock);
    auto [found, inserted] = records.emplace(record.name, record);
    if (!inserted && found->second != record) {
        error = "wtf?: " + record.name;
        return false;
    }

    return true;
}

u32 Guest_Layouts::Load(const Module& owner, std::string_view json, std::string& error) {
    Json_Document document(yyjson_read(json.data(), json.size(), 0));
    yyjson_val* object = document ? yyjson_doc_get_root(document.get()) : nullptr;
    yyjson_val* changes = yyjson_obj_get(object, "changes");
    if (Number(object, "version") != gLayoutVersion || !yyjson_is_arr(changes)) {
        error = "layout version mismatch?";
        return 0;
    }

    Guest_Layout_Profile profile;
    profile.owner = &owner;
    size_t index, count;
    yyjson_val* value;
    yyjson_arr_foreach(changes, index, count, value) {
        Guest_Layout_Edit edit;
        edit.record = Text(value, "record");
        edit.field = Text(value, "field");
        edit.kind = edit.field.empty() ? Guest_Layout_Edit::Kind::Record : Guest_Layout_Edit::Kind::Field;
        edit.offset = Number(value, "offset");
        edit.size = Number(value, "size");
        edit.alignment = Number(value, "align");
        edit.shiftFollowing = yyjson_get_bool(yyjson_obj_get(value, "shift_following"));
        edit.absent = yyjson_get_bool(yyjson_obj_get(value, "absent"));
        yyjson_val* insertion = yyjson_obj_get(value, "insert");
        yyjson_val* removal = yyjson_obj_get(value, "remove");
        if (insertion != nullptr || removal != nullptr) {
            edit.kind = insertion != nullptr ? Guest_Layout_Edit::Kind::Insert : Guest_Layout_Edit::Kind::Remove;
            yyjson_val* range = insertion != nullptr ? insertion : removal;
            edit.offset = Number(range, "offset");
            edit.size = Number(range, "size");
            if (!edit.offset || !edit.size) {
                error = "layout insertion/removal requires offset and size";
                return 0;
            }
        }

        if (edit.record.empty()) {
            error = "layout edit requires a record";
            return 0;
        }
        profile.edits.push_back(std::move(edit));
    }

    std::lock_guard guard(lock);
    if (nextHandle == UINT32_MAX || !Resolve(profile, error)) {
        return 0;
    }

    u32 handle = ++nextHandle;
    profiles.emplace(handle, std::move(profile));
    return handle;
}

bool Guest_Layouts::Refresh(std::string& error) {
    std::lock_guard guard(lock);
    if (activeProfile.owner == nullptr) {
        return true;
    }

    auto updated = Resolve(activeProfile, error);
    if (!updated) {
        return false;
    }

    active = std::move(updated);
    return true;
}

Guest_Layout_Snapshot Guest_Layouts::Resolve(const Guest_Layout_Profile& profile, std::string& error) const {
    Resolver resolver { records, profile, {}, {}, error };
    auto values = std::make_shared<Guest_Layout_Values>();
    for (const auto& [name, record] : records) {
        if (!resolver.Record(name)) {
            if (error.empty()) {
                error = "layout size overflow: " + name;
            }
            return nullptr;
        }
    }

    for (const auto& [name, record] : resolver.resolved) {
        std::string prefix = std::string(gLayoutPrefix) + name;
        values->emplace(prefix + ".size", record.size);
        values->emplace(prefix + ".align", record.alignment);
        for (const Guest_Layout_Field& field : record.fields) {
            values->emplace(prefix + "." + field.name + ".offset", field.absent ? UINT64_MAX : field.offset);
            values->emplace(prefix + "." + field.name + ".size", field.absent ? UINT64_MAX : field.size);
        }
    }

    return values;
}

Guest_Layout_Snapshot Guest_Layouts::Select(const Module& owner, u32 handle, std::string& error) {
    std::lock_guard guard(lock);
    Guest_Layout_Profile baseline;
    const Guest_Layout_Profile* profile = &baseline;
    if (handle != 0) {
        auto found = profiles.find(handle);
        if (found == profiles.end() || found->second.owner != &owner) {
            error = "unknown layout profile";
            return nullptr;
        }
        profile = &found->second;
    }

    auto snapshot = Resolve(*profile, error);
    if (snapshot) {
        activeProfile = *profile;
        active = snapshot;
    }

    return snapshot;
}

void Guest_Layouts::Unload(const Module& owner, u32 handle) {
    std::lock_guard guard(lock);
    auto found = profiles.find(handle);
    if (found != profiles.end() && found->second.owner == &owner) {
        profiles.erase(found);
    }
}

void Guest_Layouts::Release(const Module& owner) {
    std::lock_guard guard(lock);
    if (activeProfile.owner == &owner) {
        active = std::make_shared<Guest_Layout_Values>();
        activeProfile = {};
    }
    std::erase_if(profiles, [&](const auto& entry) { return entry.second.owner == &owner; });
}

Guest_Layout_Snapshot Guest_Layouts::Snapshot() {
    std::lock_guard guard(lock);
    return active;
}
