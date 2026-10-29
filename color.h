#pragma once
#include <cstdint>
#include <string_view>

namespace lens {
struct Color { unsigned char r, g, b, a; };
constexpr Color kDefaultBorder{55, 125, 220, 255};
inline Color parseColor(std::wstring_view text) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != L'#') return kDefaultBorder;
    std::uint32_t value = 0;
    for (size_t i=1; i<text.size(); ++i) {
        wchar_t c = text[i];
        unsigned digit;
        if (c >= L'0' && c <= L'9') digit = c-L'0';
        else if (c >= L'a' && c <= L'f') digit = c-L'a'+10;
        else if (c >= L'A' && c <= L'F') digit = c-L'A'+10;
        else return kDefaultBorder;
        value = (value << 4) | digit;
    }
    if (text.size() == 7) value = (value << 8) | 255;
    return {static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
            static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
}
}
