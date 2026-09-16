#include "json_text.hpp"

namespace squarestar::text {

std::string EscapeJsonStringValue(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c >= 0x20)
                out.push_back(static_cast<char>(c));
            break;
        }
    }
    return out;
}

}
