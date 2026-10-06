#pragma once
#include "tools_panel.hpp"
#include "ui_text.hpp"

// Everything the tools panel texture's look depends on; a new value means a re-render and re-upload.
struct ToolsVisual {
    size_t game=0;
    int hover=-1;                      // button under a laser
    bool closeHover=false,pressed=false;
    bool swapEyes=false;
    int curve=0;                       // 0 off .. 3 high
    bool glow=false;
    int floatWindow=0;                 // 0 off .. 3 high
    bool rumble=true;
    bool curveAvailable=true;          // the runtime offers cylinder layers
    bool rumbleAvailable=true;         // the controllers can vibrate
    std::wstring status;               // header line: what the last press did, or a hint
    bool warn=false;                   // status in a warning colour
    bool operator==(const ToolsVisual&) const=default;
};

namespace tools {
inline const wchar_t* levelName(int level) {
    static const wchar_t* names[]{L"Off",L"Low",L"Medium",L"High"};
    return names[std::clamp(level,0,3)];
}
struct Label { std::wstring title,caption; bool on=false,dim=false; };
inline Label describe(const Item& item,const ToolsVisual& v) {
    switch (item.kind) {
    case Kind::Key: {
        if (v.game>=Games.size() || item.key>=Games[v.game].keys.size()) return {L"?",L""};
        const GameKey& key=Games[v.game].keys[item.key];
        return {key.label,L"F"+std::to_wstring(key.vk-VK_F1+1)};
    }
    case Kind::SwapEyes: return {L"Swap eyes",v.swapEyes ? L"Swapped" : L"Normal",v.swapEyes};
    case Kind::Curve: return {L"Curved screen",v.curveAvailable ? levelName(v.curve) : L"Not available",v.curveAvailable && v.curve>0,!v.curveAvailable};
    case Kind::Glow: return {L"Ambient glow",v.glow ? L"On" : L"Off",v.glow};
    case Kind::FloatWindow: return {L"Float window",levelName(v.floatWindow),v.floatWindow>0};
    case Kind::Rumble: return {L"Rumble",v.rumbleAvailable ? (v.rumble ? L"On" : L"Off") : L"No haptics",v.rumbleAvailable && v.rumble,!v.rumbleAvailable};
    case Kind::Recenter: return {L"Recenter",L"Both grips + A"};
    }
    return {};
}

// Renders the TexW x TexH panel texture (straight alpha, sRGB). bgra=true when the runtime only offers BGRA.
inline void render(std::vector<uint32_t>& pixels,const ToolsVisual& v,bool bgra) {
    using namespace ui; using namespace ui::detail;
    pixels.assign(static_cast<size_t>(TexW)*TexH,0);
    auto paint=[&](int x0,int y0,int x1,int y1,auto shade) {
        for (int y=std::max(0,y0);y<std::min(static_cast<int>(TexH),y1);++y) for (int x=std::max(0,x0);x<std::min(static_cast<int>(TexW),x1);++x) {
            uint32_t& d=pixels[static_cast<size_t>(y)*TexW+x];
            Px p=unpack(d,bgra); shade(p,x+0.5f,y+0.5f); d=pack(p,bgra,1.0f);
        }
    };
    const Col accent{64,150,255,1.0f},white{255,255,255,0.96f};
    paint(0,0,TexW,TexH,[&](Px& p,float x,float y) {
        const float d=roundRect(x,y,TexW/2.0f,TexH/2.0f,TexW/2.0f-6,TexH/2.0f-6,56);
        over(p,{16,18,26,0.94f},cover(d));
        over(p,{255,255,255,0.22f},cover(std::fabs(d+3.0f)-3.0f));
    });
    drawText(pixels,TexW,bgra,40,24,470,84,L"FlatToDepth",56,600,white,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    const auto close=closeButton();
    drawText(pixels,TexW,bgra,520,24,static_cast<int>(close.x)-520-16,84,v.status.empty() ? L"Point a laser at a button and pull the trigger." : v.status,
        34,400,v.warn ? Col{255,160,120,1.0f} : Col{200,208,225,0.92f},DT_LEFT|DT_VCENTER|DT_WORDBREAK);

    // Close button: a round disc with a cross.
    {
        const float cx=close.x+close.w/2,cy=close.y+close.h/2,r=close.w/2;
        const Col fillColor=v.closeHover ? (v.pressed ? accent : Col{70,112,190,0.97f}) : Col{46,52,70,0.95f};
        paint(static_cast<int>(close.x)-4,static_cast<int>(close.y)-4,static_cast<int>(close.x+close.w)+4,static_cast<int>(close.y+close.h)+4,[&](Px& p,float x,float y) {
            const float d=std::hypot(x-cx,y-cy)-r;
            over(p,fillColor,cover(d)); over(p,v.closeHover ? Col{150,205,255,1.0f} : Col{255,255,255,0.22f},cover(std::fabs(d+2.5f)-2.5f));
            const float cross=std::min(segment(x,y,cx-17,cy-17,cx+17,cy+17),segment(x,y,cx-17,cy+17,cx+17,cy-17))-3.5f;
            over(p,white,cover(cross));
        });
    }

    const auto list=items(v.game);
    for (size_t i=0;i<list.size();++i) {
        const auto b=cell(i);
        const Label label=describe(list[i],v);
        const bool hot=v.hover==static_cast<int>(i),down=hot && v.pressed;
        Col fillColor=label.dim ? Col{44,46,54,0.9f} : label.on ? Col{24,102,90,0.96f} : Col{40,56,100,0.96f};
        if (hot) { fillColor.r=std::min(255.0f,fillColor.r+30); fillColor.g=std::min(255.0f,fillColor.g+30); fillColor.b=std::min(255.0f,fillColor.b+30); }
        if (down) fillColor=accent;
        paint(static_cast<int>(b.x)-4,static_cast<int>(b.y)-4,static_cast<int>(b.x+b.w)+4,static_cast<int>(b.y+b.h)+4,[&](Px& p,float x,float y) {
            const float d=roundRect(x,y,b.x+b.w/2,b.y+b.h/2,b.w/2,b.h/2,36);
            over(p,fillColor,cover(d));
            over(p,hot ? Col{150,205,255,1.0f} : Col{255,255,255,0.18f},cover(std::fabs(d+3.0f)-3.0f));
        });
        const Col text=label.dim ? Col{255,255,255,0.45f} : white;
        drawText(pixels,TexW,bgra,static_cast<int>(b.x)+14,static_cast<int>(b.y)+20,static_cast<int>(b.w)-28,58,label.title,44,600,text,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        drawText(pixels,TexW,bgra,static_cast<int>(b.x)+14,static_cast<int>(b.y)+84,static_cast<int>(b.w)-28,44,label.caption,36,400,
            label.dim ? Col{255,170,130,0.9f} : Col{255,255,255,0.78f},DT_CENTER|DT_SINGLELINE|DT_VCENTER);
    }
}
}
