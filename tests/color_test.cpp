#include "../color.h"
#include <cassert>
#include <iostream>
int main() {
    auto red = lens::parseColor(L"#AA000080");
    assert(red.r == 170 && red.g == 0 && red.b == 0 && red.a == 128);
    auto clear = lens::parseColor(L"#aa000000");
    assert(clear.r == 170 && clear.a == 0);
    auto rgb = lens::parseColor(L"#00ff7F");
    assert(rgb.r == 0 && rgb.g == 255 && rgb.b == 127 && rgb.a == 255);
    for (auto invalid : {L"", L"#", L"#12345", L"#1234567", L"#123456789", L"#XX000080", L"AA000080"}) {
        auto fallback = lens::parseColor(invalid);
        assert(fallback.r == 55 && fallback.g == 125 && fallback.b == 220 && fallback.a == 255);
    }
    std::cout << "RGBA parsing tests passed\n";
}
