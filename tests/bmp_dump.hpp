#pragma once
#include <cstdint>
#include <fstream>
#include <vector>

// Writes straight-alpha RGBA pixels as a BMP, composited over a mid-grey backdrop so translucent parts are visible.
inline void writeBmp(const char* path,const std::vector<uint32_t>& rgba,uint32_t w,uint32_t h) {
    std::vector<uint8_t> out(54+static_cast<size_t>(w)*h*4);
    auto put32=[&](size_t at,uint32_t v) { for (int i=0;i<4;++i) out[at+i]=static_cast<uint8_t>(v>>(8*i)); };
    out[0]='B'; out[1]='M'; put32(2,static_cast<uint32_t>(out.size())); put32(10,54); put32(14,40);
    put32(18,w); put32(22,static_cast<uint32_t>(-static_cast<int32_t>(h))); out[26]=1; out[28]=32; put32(34,w*h*4);
    for (size_t i=0;i<rgba.size();++i) {
        // Composite over a mid-grey headset-ish backdrop so translucent parts are visible.
        const float a=((rgba[i]>>24)&255)/255.0f; const uint8_t bg[3]{90,96,110};
        const uint8_t c[3]{static_cast<uint8_t>(rgba[i]&255),static_cast<uint8_t>((rgba[i]>>8)&255),static_cast<uint8_t>((rgba[i]>>16)&255)};
        for (int k=0;k<3;++k) out[54+i*4+(2-k)]=static_cast<uint8_t>(c[k]*a+bg[k]*(1-a));
        out[54+i*4+3]=255;
    }
    std::ofstream f(path,std::ios::binary); f.write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));
}
