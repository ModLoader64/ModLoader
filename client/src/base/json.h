#pragma once

#include <memory>
#include <string>
#include <yyjson.h>

struct Json_Deleter {
    void operator()(yyjson_doc* document) const {
        yyjson_doc_free(document);
    }

    void operator()(yyjson_mut_doc* document) const {
        yyjson_mut_doc_free(document);
    }
};

using Json_Document = std::unique_ptr<yyjson_doc, Json_Deleter>;
using Mutable_Json_Document = std::unique_ptr<yyjson_mut_doc, Json_Deleter>;

bool Json_Write_File(const std::string& path, const yyjson_mut_doc* document);
