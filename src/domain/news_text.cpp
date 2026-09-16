#include "domain/news_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace squarestar::news {
namespace {

void AppendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.append("\xEF\xBF\xBD");
    }
}

bool DecodeOne(std::string_view input, std::size_t& offset, std::uint32_t& codepoint) {
    const auto first = static_cast<unsigned char>(input[offset]);
    if (first < 0x80) {
        codepoint = first;
        ++offset;
        return true;
    }
    int continuationCount = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xE0) == 0xC0) {
        continuationCount = 1;
        value = first & 0x1F;
        minimum = 0x80;
    } else if ((first & 0xF0) == 0xE0) {
        continuationCount = 2;
        value = first & 0x0F;
        minimum = 0x800;
    } else if ((first & 0xF8) == 0xF0) {
        continuationCount = 3;
        value = first & 0x07;
        minimum = 0x10000;
    } else {
        ++offset;
        codepoint = 0xFFFD;
        return false;
    }
    if (offset + static_cast<std::size_t>(continuationCount) >= input.size()) {
        ++offset;
        codepoint = 0xFFFD;
        return false;
    }
    for (int i = 1; i <= continuationCount; ++i) {
        const auto next = static_cast<unsigned char>(input[offset + static_cast<std::size_t>(i)]);
        if ((next & 0xC0) != 0x80) {
            ++offset;
            codepoint = 0xFFFD;
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }
    offset += static_cast<std::size_t>(continuationCount + 1);
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        codepoint = 0xFFFD;
        return false;
    }
    codepoint = value;
    return true;
}

int MojibakeMarkers(std::string_view value) {
    static constexpr std::array<std::string_view, 6> markers = {
        "\xC3\x82", "\xC3\x83", "\xC3\xA2", "\xC2\x80", "\xC2\x99",
        "\xC3\xAF\xC2\xBF\xC2\xBD"};
    int count = 0;
    for (const std::string_view marker : markers) {
        std::size_t position = 0;
        while ((position = value.find(marker, position)) != std::string_view::npos) {
            ++count;
            position += marker.size();
        }
    }
    return count;
}

bool Windows1252Byte(std::uint32_t codepoint, unsigned char& value) {
    if (codepoint <= 0xFF) {
        value = static_cast<unsigned char>(codepoint);
        return true;
    }
    static constexpr std::array<std::pair<std::uint32_t, unsigned char>, 27> mapping = {{
        {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84},
        {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02C6, 0x88},
        {0x2030, 0x89}, {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C},
        {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201C, 0x93},
        {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97},
        {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B},
        {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}}};
    const auto found = std::find_if(mapping.begin(), mapping.end(), [&](const auto& item) {
        return item.first == codepoint;
    });
    if (found == mapping.end())
        return false;
    value = found->second;
    return true;
}

std::string RepairWindows1252Mojibake(std::string_view input) {
    std::string recoveredBytes;
    recoveredBytes.reserve(input.size());
    std::size_t offset = 0;
    while (offset < input.size()) {
        std::uint32_t codepoint = 0;
        const bool valid = DecodeOne(input, offset, codepoint);
        unsigned char recovered = 0;
        if (!valid || !Windows1252Byte(codepoint, recovered))
            return std::string(input);
        recoveredBytes.push_back(static_cast<char>(recovered));
    }
    std::string repaired;
    repaired.reserve(recoveredBytes.size());
    offset = 0;
    bool entirelyValid = true;
    while (offset < recoveredBytes.size()) {
        std::uint32_t codepoint = 0;
        if (!DecodeOne(recoveredBytes, offset, codepoint))
            entirelyValid = false;
        AppendUtf8(repaired, codepoint);
    }
    return entirelyValid && MojibakeMarkers(repaired) < MojibakeMarkers(input)
               ? repaired
               : std::string(input);
}

bool ParseNumericEntity(std::string_view value, std::uint32_t& codepoint) {
    if (value.size() < 2 || value.front() != '#')
        return false;
    const bool hex = value.size() > 2 && (value[1] == 'x' || value[1] == 'X');
    const std::size_t start = hex ? 2 : 1;
    if (start == value.size())
        return false;
    std::uint32_t parsed = 0;
    for (std::size_t i = start; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        int digit = -1;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (hex && c >= 'a' && c <= 'f')
            digit = 10 + c - 'a';
        else if (hex && c >= 'A' && c <= 'F')
            digit = 10 + c - 'A';
        if (digit < 0 || parsed > (0x10FFFFu - static_cast<std::uint32_t>(digit)) /
                                       static_cast<std::uint32_t>(hex ? 16 : 10))
            return false;
        parsed = parsed * static_cast<std::uint32_t>(hex ? 16 : 10) +
                 static_cast<std::uint32_t>(digit);
    }
    if (parsed == 0 || parsed > 0x10FFFF || (parsed >= 0xD800 && parsed <= 0xDFFF))
        return false;
    codepoint = parsed;
    return true;
}

std::string DecodeHtmlEntities(std::string_view input) {
    static constexpr std::array<std::pair<std::string_view, std::uint32_t>, 12> named = {{
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
        {"nbsp", ' '}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018},
        {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}}};
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        if (input[i] != '&') {
            output.push_back(input[i++]);
            continue;
        }
        const std::size_t semicolon = input.find(';', i + 1);
        if (semicolon == std::string_view::npos || semicolon - i > 16) {
            output.push_back(input[i++]);
            continue;
        }
        const std::string_view entity = input.substr(i + 1, semicolon - i - 1);
        std::uint32_t codepoint = 0;
        bool found = ParseNumericEntity(entity, codepoint);
        if (!found) {
            const auto namedEntity = std::find_if(
                named.begin(), named.end(), [&](const auto& item) { return item.first == entity; });
            if (namedEntity != named.end()) {
                codepoint = namedEntity->second;
                found = true;
            }
        }
        if (!found) {
            output.push_back(input[i++]);
            continue;
        }
        AppendUtf8(output, codepoint);
        i = semicolon + 1;
    }
    return output;
}

bool LooksLikeMarcArtifact(std::string_view input, std::size_t offset,
                           std::size_t& length) {
    std::size_t i = offset;
    int leadingDigits = 0;
    while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i])) &&
           leadingDigits < 3) {
        ++i;
        ++leadingDigits;
    }
    if (leadingDigits != 3 || i + 2 >= input.size() || input[i] != '$' ||
        !std::isalpha(static_cast<unsigned char>(input[i + 1])))
        return false;
    i += 2;
    int punctuation = 0;
    while (i < input.size() && input[i] == '?') {
        ++i;
        ++punctuation;
    }
    int trailingDigits = 0;
    while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i])) &&
           trailingDigits < 3) {
        ++i;
        ++trailingDigits;
    }
    if (punctuation == 0 && trailingDigits == 0)
        return false;
    length = i - offset;
    return true;
}

std::string CleanAndValidate(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    bool pendingSpace = false;
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t artifactLength = 0;
        if (LooksLikeMarcArtifact(input, offset, artifactLength)) {
            offset += artifactLength;
            pendingSpace = !output.empty();
            continue;
        }
        std::uint32_t codepoint = 0;
        DecodeOne(input, offset, codepoint);
        if (codepoint == 0xFFFD) {
            pendingSpace = !output.empty();
            continue;
        }
        if (codepoint == 0xFEFF || codepoint == 0x200B)
            continue;
        if (codepoint <= 0x20 || codepoint == 0x7F || codepoint == 0xA0) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace) {
            output.push_back(' ');
            pendingSpace = false;
        }
        AppendUtf8(output, codepoint);
    }
    while (!output.empty() && output.back() == ' ')
        output.pop_back();
    return output;
}

}

std::string NormalizeNewsText(std::string_view text) {
    std::string normalized = DecodeHtmlEntities(text);
    if (MojibakeMarkers(normalized) != 0)
        normalized = RepairWindows1252Mojibake(normalized);
    return CleanAndValidate(normalized);
}

}
