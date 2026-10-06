#pragma once
#include "ui_atlas.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <string>

namespace ui {
// Blends a colour over one packed pixel (straight alpha), the same way the shape raster does.
inline void blendPacked(uint32_t& d,const detail::Col& c,float coverage,bool bgra) {
    detail::Px p=detail::unpack(d,bgra);
    detail::over(p,c,coverage);
    d=detail::pack(p,bgra,1.0f);
}

// Draws anti-aliased text into a packed RGBA/BGRA buffer with GDI. `format` takes DrawText flags
// (DT_CENTER, DT_WORDBREAK, ...); DT_VCENTER also centres wrapped text vertically.
inline void drawText(std::vector<uint32_t>& px,uint32_t stride,bool bgra,int x,int y,int w,int h,const std::wstring& text,
    int fontPixels,int weight,const detail::Col& color,UINT format) {
    if (text.empty() || w<=0 || h<=0) return;
    HDC dc=CreateCompatibleDC(nullptr);
    if (!dc) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if (!bitmap || !bits) { DeleteDC(dc); return; }
    HGDIOBJ oldBitmap=SelectObject(dc,bitmap);
    HFONT font=CreateFontW(-fontPixels,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    HGDIOBJ oldFont=SelectObject(dc,font);
    std::memset(bits,0,static_cast<size_t>(w)*h*4);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
    RECT area{0,0,w,h};
    if (format & DT_VCENTER && !(format & DT_SINGLELINE)) {
        RECT measured{0,0,w,0};
        DrawTextW(dc,text.c_str(),-1,&measured,(format & ~static_cast<UINT>(DT_VCENTER))|DT_CALCRECT|DT_NOPREFIX);
        area.top=std::max(0L,(static_cast<LONG>(h)-measured.bottom)/2); format&=~static_cast<UINT>(DT_VCENTER);
    }
    DrawTextW(dc,text.c_str(),-1,&area,format|DT_NOPREFIX);
    GdiFlush();
    const auto* glyphs=static_cast<const uint32_t*>(bits);
    for (int j=0;j<h;++j) for (int i=0;i<w;++i) {
        const float coverage=((glyphs[static_cast<size_t>(j)*w+i]>>8)&255)/255.0f;   // white text: any channel is the coverage
        const int px_x=x+i, px_y=y+j;
        if (coverage<=0 || px_x<0 || px_y<0 || px_x>=static_cast<int>(stride) || static_cast<size_t>(px_y)*stride+px_x>=px.size()) continue;
        blendPacked(px[static_cast<size_t>(px_y)*stride+px_x],color,coverage,bgra);
    }
    SelectObject(dc,oldFont); DeleteObject(font);
    SelectObject(dc,oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
}
}
