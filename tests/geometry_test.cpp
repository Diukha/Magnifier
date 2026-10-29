#include "../geometry.h"
#include <cassert>
#include <iostream>
int main() {
    lens::State s;
    lens::Rect monitor{-1920,0,1920,1080};
    for (int x : {-1920,-1919,-960,-1}) for (int y : {0,1,540,1079}) {
        for (float z : {1.0f,2.0f,12.0f}) {
            auto output = lens::window(s,monitor,x,y);
            auto src = lens::source(output,monitor,x,y,z);
            assert(output.x>=monitor.x && output.x+output.w<=0);
            assert(output.y>=0 && output.y+output.h<=1080);
            assert(src.x>=monitor.x && src.x+src.w<=0);
            assert(src.y>=0 && src.y+src.h<=1080);
            assert(x>=src.x && x<src.x+src.w);
            assert(y>=src.y && y<src.y+src.h);
        }
    }
    s.resize(-99999,99999); assert(s.width==160 && s.height==1600);
    auto small=lens::window(s,{0,0,120,90},0,0);
    assert(small.w==120 && small.h==90 && small.x==0 && small.y==0);
    s.wheel(120); assert(s.zoom>2.39f && s.zoom<2.41f);
    s.wheel(-120); assert(s.zoom>1.99f && s.zoom<2.01f);
    s.wheel(12000); assert(s.zoom==12.0f);
    s.wheel(-24000); assert(s.zoom==1.0f);
    std::cout << "Geometry and zoom tests passed\n";
}
