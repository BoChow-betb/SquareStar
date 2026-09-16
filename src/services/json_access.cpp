#include "services/json_access.hpp"

#include <cstddef>
#include <limits>

namespace squarestar::json {

void DocumentDeleter::operator()(yyjson_doc* document) noexcept {
    if (document)
        yyjson_doc_free(document);
    squarestar::json_memory::Release(allocationOwner);
}

Document ParseJson(const std::string& text) {
    if (text.empty() || text.size() > kMaxJsonDocumentBytes)
        return {};
    yyjson_alc allocator{};
    squarestar::json_memory::AllocatorOwner owner{};
    const yyjson_alc* selected =
        squarestar::json_memory::Initialize(allocator, owner);
    yyjson_doc* document = yyjson_read_opts(const_cast<char*>(text.data()),
                                            text.size(),
                                            YYJSON_READ_NOFLAG,
                                            selected,
                                            nullptr);
    if (!document) {
        squarestar::json_memory::Release(owner);
        return {};
    }
    return Document(document, DocumentDeleter{owner});
}

Document ParseJsonInSitu(std::string& text) {
    if (text.empty() || text.size() > kMaxJsonDocumentBytes)
        return {};
    const std::size_t payloadSize = text.size();
    text.resize(payloadSize + YYJSON_PADDING_SIZE, '\0');
    yyjson_alc allocator{};
    squarestar::json_memory::AllocatorOwner owner{};
    const yyjson_alc* selected =
        squarestar::json_memory::Initialize(allocator, owner);
    yyjson_doc* document = yyjson_read_opts(
        text.data(), payloadSize, YYJSON_READ_INSITU, selected, nullptr);
    if (!document) {
        squarestar::json_memory::Release(owner);
        return {};
    }
    return Document(document, DocumentDeleter{owner});
}

yyjson_val* JsonPath(yyjson_val* value, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (!value || !yyjson_is_obj(value))
            return nullptr;
        value = yyjson_obj_get(value, key);
    }
    return value;
}

template <typename T, typename Predicate, typename Getter>
bool JsonScalar(yyjson_val* object,
                const char* key,
                T& out,
                Predicate predicate,
                Getter getter) {
    if (!object || !yyjson_is_obj(object))
        return false;
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!value || !predicate(value))
        return false;
    out = getter(value);
    return true;
}

bool JsonNumber(yyjson_val* object, const char* key, double& out) {
    return JsonScalar(object,
                      key,
                      out,
                      [](yyjson_val* value) { return yyjson_is_num(value); },
                      [](yyjson_val* value) { return yyjson_get_num(value); });
}

bool JsonUint(yyjson_val* object, const char* key, uint64_t& out) {
    return JsonScalar(object,
                      key,
                      out,
                      [](yyjson_val* value) {
                          return yyjson_is_uint(value) ||
                                 (yyjson_is_sint(value) && yyjson_get_sint(value) >= 0);
                      },
                      [](yyjson_val* value) {
                          return yyjson_is_uint(value)
                                     ? yyjson_get_uint(value)
                                     : static_cast<uint64_t>(yyjson_get_sint(value));
                      });
}

bool JsonInt(yyjson_val* object, const char* key, int64_t& out) {
    return JsonScalar(object,
                      key,
                      out,
                      [](yyjson_val* value) {
                          return yyjson_is_sint(value) ||
                                 (yyjson_is_uint(value) &&
                                  yyjson_get_uint(value) <=
                                      static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
                      },
                      [](yyjson_val* value) {
                          return yyjson_is_sint(value)
                                     ? yyjson_get_sint(value)
                                     : static_cast<int64_t>(yyjson_get_uint(value));
                      });
}

bool JsonBool(yyjson_val* object, const char* key, bool& out) {
    return JsonScalar(object,
                      key,
                      out,
                      [](yyjson_val* value) { return yyjson_is_bool(value); },
                      [](yyjson_val* value) { return yyjson_get_bool(value); });
}

bool JsonString(yyjson_val* object,
                const char* key,
                std::string& out,
                std::size_t maxBytes) {
    if (!object || !yyjson_is_obj(object))
        return false;
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!value || !yyjson_is_str(value))
        return false;
    const char* text = yyjson_get_str(value);
    const std::size_t length = yyjson_get_len(value);
    if (!text || length > maxBytes)
        return false;
    out.assign(text, length);
    return true;
}

}
