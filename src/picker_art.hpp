#pragma once
#include "game_picker.hpp"
#include "ui_text.hpp"

// One game in the menu.
struct PickerEntry {
    std::wstring title,caption;    // caption: the game's subtitle; a game that is not installed says so instead
    bool installed=true;           // false: dimmed, marked "Not installed", not selectable
    bool operator==(const PickerEntry&) const=default;
};
// Everything the menu texture's look depends on; a new value means a re-render and re-upload.
struct PickerVisual {
    GamePicker::Mode mode=GamePicker::Mode::Choose;
    int hover=-1;                  // tile under a laser (entry number)
    bool backHover=false,prevHover=false,nextHover=false,pressed=false;
    size_t page=0;
    std::vector<PickerEntry> entries;
    std::wstring message;          // Message mode: the status card's text
    std::wstring hint;             // footer line
    bool warn=false;               // footer in a warning colour
    bool operator==(const PickerVisual&) const=default;
};

namespace picker {
// Renders the 2048x1024 menu texture (straight alpha, sRGB). bgra=true when the runtime only offers BGRA.
inline void render(std::vector<uint32_t>& pixels,const PickerVisual& v,bool bgra) {
    using namespace ui; using namespace ui::detail;
    pixels.assign(static_cast<size_t>(TexW)*TexH,0);
    // Draws shapes over whatever is already in the rectangle (the atlas helper replaces instead).
    auto paint=[&](int x0,int y0,int x1,int y1,auto shade) {
        for (int y=std::max(0,y0);y<std::min(static_cast<int>(TexH),y1);++y) for (int x=std::max(0,x0);x<std::min(static_cast<int>(TexW),x1);++x) {
            uint32_t& d=pixels[static_cast<size_t>(y)*TexW+x];
            Px p=unpack(d,bgra); shade(p,x+0.5f,y+0.5f); d=pack(p,bgra,1.0f);
        }
    };
    auto box=[&](const Box& b,float pad,int& x0,int& y0,int& x1,int& y1) {
        x0=static_cast<int>(px(b.cx-b.hw-pad)); x1=static_cast<int>(px(b.cx+b.hw+pad))+1;
        y0=static_cast<int>(py(b.cy+b.hh+pad)); y1=static_cast<int>(py(b.cy-b.hh-pad))+1;
    };
    // A rounded button with a label; dimmed when it cannot be used.
    auto button=[&](const Box& b,const wchar_t* label,bool hot,bool down,bool usable) {
        int x0,y0,x1,y1; box(b,0.0f,x0,y0,x1,y1);
        const float cx=px(b.cx), cy=py(b.cy), hw=b.hw*PxPerMeter, hh=b.hh*PxPerMeter;
        const Col accent{64,150,255,1.0f};
        const Col fill=!usable ? Col{46,52,70,0.45f} : hot ? (down ? accent : Col{70,112,190,0.97f}) : Col{46,52,70,0.95f};
        paint(x0-4,y0-4,x1+4,y1+4,[&](Px& p,float x,float y) {
            const float d=roundRect(x,y,cx,cy,hw,hh,hh);
            over(p,fill,cover(d)); over(p,hot && usable ? Col{150,205,255,1.0f} : Col{255,255,255,usable ? 0.22f : 0.1f},cover(std::fabs(d+2.5f)-2.5f));
        });
        drawText(pixels,TexW,bgra,x0,y0,x1-x0,y1-y0,label,46,600,Col{255,255,255,usable ? 0.96f : 0.35f},DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    };
    const Col accent{64,150,255,1.0f}, white{255,255,255,0.96f};
    paint(0,0,TexW,TexH,[&](Px& p,float x,float y) {
        const float d=roundRect(x,y,TexW/2.0f,TexH/2.0f,TexW/2.0f-8,TexH/2.0f-8,64);
        over(p,{16,18,26,0.94f},cover(d));
        over(p,{255,255,255,0.22f},cover(std::fabs(d+3.0f)-3.0f));
    });
    drawText(pixels,TexW,bgra,90,44,1200,110,v.mode==GamePicker::Mode::Choose ? L"Choose a game" : L"FlatToDepth",76,600,white,DT_LEFT|DT_SINGLELINE|DT_VCENTER);

    const size_t total=v.entries.size(),pages=pageCount(total);
    const bool paging=v.mode==GamePicker::Mode::Choose && pages>1;
    if (v.mode==GamePicker::Mode::Choose) {
        // (Not "small": rpcndr.h in the Windows headers #defines that to char.)
        const bool compact=grid(total);
        const size_t first=compact ? std::min(v.page,pages-1)*PerPage : 0,last=compact ? std::min(total,first+PerPage) : total;
        static constexpr Col palette[]{{26,70,140,0.96f},{24,102,90,0.96f},{112,62,130,0.96f}};
        for (size_t g=first;g<last;++g) {
            const Box b=tile(g,total);
            const PickerEntry& entry=v.entries[g];
            const bool hot=v.hover==static_cast<int>(g), down=hot && v.pressed, ok=entry.installed;
            Col fillColor=ok ? palette[g%std::size(palette)] : Col{52,54,62,0.9f};
            if (hot) { fillColor.r=std::min(255.0f,fillColor.r+30); fillColor.g=std::min(255.0f,fillColor.g+30); fillColor.b=std::min(255.0f,fillColor.b+30); }
            if (down) fillColor=accent;
            int x0,y0,x1,y1; box(b,0.0f,x0,y0,x1,y1);
            const float cx=px(b.cx), cy=py(b.cy), hw=b.hw*PxPerMeter, hh=b.hh*PxPerMeter;
            paint(x0-4,y0-4,x1+4,y1+4,[&](Px& p,float x,float y) {
                const float d=roundRect(x,y,cx,cy,hw,hh,compact ? 36.0f : 48.0f);
                over(p,fillColor,cover(d));
                over(p,hot ? Col{150,205,255,1.0f} : Col{255,255,255,0.18f},cover(std::fabs(d+3.0f)-3.0f));
            });
            const Col text=ok ? white : Col{255,255,255,0.45f};
            const std::wstring caption=ok ? entry.caption : L"Not installed";
            const Col captionColor=ok ? Col{255,255,255,0.75f} : Col{255,170,130,0.95f};
            if (compact) {
                drawText(pixels,TexW,bgra,x0+20,y0+24,x1-x0-40,176,entry.title,50,600,text,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
                drawText(pixels,TexW,bgra,x0+20,y0+214,x1-x0-40,64,caption,36,400,captionColor,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
            } else {
                drawText(pixels,TexW,bgra,x0+40,y0+110,x1-x0-80,230,entry.title,68,600,text,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
                drawText(pixels,TexW,bgra,x0+40,y0+350,x1-x0-80,70,caption,44,400,captionColor,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
            }
        }
        if (paging) {
            const size_t page=std::min(v.page,pages-1);
            drawText(pixels,TexW,bgra,TexW-90-480,44,480,110,L"Page "+std::to_wstring(page+1)+L" of "+std::to_wstring(pages),46,400,Col{200,208,225,0.92f},DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
            button(prevButton(),L"< Previous",v.prevHover,v.pressed,page>0);
            button(nextButton(),L"Next >",v.nextHover,v.pressed,page+1<pages);
        }
    } else {   // Message and Status cards
        drawText(pixels,TexW,bgra,140,256,TexW-280,512,v.message,76,600,white,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        if (v.mode==GamePicker::Mode::Message) button(backButton(),L"Back to games",v.backHover,v.pressed,true);
    }
    // Footer: hint, or a warning when something went wrong. In Message mode it sits above the back button; with page
    // buttons it keeps clear of them.
    const float footerY=v.mode==GamePicker::Mode::Message ? -0.43f : -0.72f;
    const int footerX=paging ? 360 : 100;
    drawText(pixels,TexW,bgra,footerX,static_cast<int>(py(footerY))-50,TexW-2*footerX,100,v.hint,paging ? 42 : 46,400,
        v.warn ? Col{255,160,120,1.0f} : Col{200,208,225,0.92f},DT_CENTER|DT_VCENTER|DT_WORDBREAK);
}
}
