#include "base/json.h"
#include "base.h"

bool Json_Write_File(const std::string& path, const yyjson_mut_doc* document) {
    if (!Directory_Create_All(Path_Directory(path))) {
        return false;
    }

    std::string temporary = path + ".tmp";
    bool saved = yyjson_mut_write_file(temporary.c_str(), document, YYJSON_WRITE_PRETTY_TWO_SPACES | YYJSON_WRITE_NEWLINE_AT_END, nullptr, nullptr) && File_Replace(temporary, path);
    if (!saved) {
        remove(temporary.c_str());
    }
    return saved;
}
