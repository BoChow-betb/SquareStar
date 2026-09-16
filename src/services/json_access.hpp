#pragma once

#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <memory>

#include "services/yyjson_document_heap.hpp"
#include <string>

#include "yyjson.h"

namespace squarestar::json {

inline constexpr std::size_t kMaxJsonDocumentBytes = 16 * 1024 * 1024;

struct DocumentDeleter {
    squarestar::json_memory::AllocatorOwner allocationOwner{};
    void operator()(yyjson_doc* document) noexcept;
};

using Document = std::unique_ptr<yyjson_doc, DocumentDeleter>;

Document ParseJson(const std::string& text);
Document ParseJsonInSitu(std::string& text);
yyjson_val* JsonPath(yyjson_val* value, std::initializer_list<const char*> keys);
bool JsonNumber(yyjson_val* object, const char* key, double& out);
bool JsonUint(yyjson_val* object, const char* key, uint64_t& out);
bool JsonInt(yyjson_val* object, const char* key, int64_t& out);
bool JsonBool(yyjson_val* object, const char* key, bool& out);
bool JsonString(yyjson_val* object,
                const char* key,
                std::string& out,
                std::size_t maxBytes = static_cast<std::size_t>(-1));

} // namespace squarestar::json
