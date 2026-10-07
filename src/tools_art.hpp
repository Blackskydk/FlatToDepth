#pragma once
#include "tools_help.hpp"
#include "tools_panel.hpp"
#include "ui_text.hpp"

// Everything the tools panel texture's look depends on; a new value means a re-render and re-upload.
struct ToolsVisual {
    size_t game=0;
    int hover=-1;                      // button under a laser
    bool closeHover=false,pressed=false;
    bool moveHover=false;              // a laser is on the bar that carries the panel
    bool carrying=false;               // the panel is being carried
    bool faded=false;                  // no laser is on the panel: it turns see-through so the game shows behind it
    bool swapEyes=false;
    int curve=0;                       // 0 off .. 3 high
    bool glow=false;
    int floatWindow=0;                 // 0 off .. 3 high
    bool rumble=true;
    bool curveAvailable=true;          // the runtime offers cylinder layers
    bool rumbleAvailable=true;         // the controllers can vibrate
    std::wstring status;               // header line: what the last press did, or a hint
    bool warn=false;                   // status in a warning colour
    std::wstring help;                 // under the buttons: what the hovered button does (empty: the general hint)
    std::vector<std::wstring> keyDetail;   // per shortcut of the game: how many steps it has or which one it is on ("2/4", "switched")
    bool operator==(const ToolsVisual&) const=default;
};

namespace tools {
inline const wchar_t* levelName(int level) {
    static const wchar_t* names[]{L"Off",L"Low",L"Medium",L"High"};
    return names[std::clamp(level,0,3)];
}
// level/levels: a button with more than on and off shows how far it is turned up (level 0 to levels) as dots as well as in words.
struct Label { std::wstring title,caption; bool on=false,dim=false; int level=-1,levels=0; };
inline Label describe(const Item& item,const ToolsVisual& v) {
    switch (item.kind) {
    case Kind::Key: {
        if (v.game>=Games.size() || item.key>=Games[v.game].keys.size()) return {L"?",L""};
        const GameKey& key=Games[v.game].keys[item.key];
        const std::wstring name=L"F"+std::to_wstring(key.vk-VK_F1+1);
        const std::wstring detail=item.key<v.keyDetail.size() ? v.keyDetail[item.key] : std::wstring();
        return {key.label,detail.empty() ? name : name+L" \u00b7 "+detail};
    }
    case Kind::SwapEyes: return {L"Swap eyes",v.swapEyes ? L"Swapped" : L"Normal",v.swapEyes};
    case Kind::Curve: return {L"Curved screen",v.curveAvailable ? levelName(v.curve) : L"Not available",v.curveAvailable && v.curve>0,!v.curveAvailable,v.curveAvailable ? std::clamp(v.curve,0,3) : -1,3};
    case Kind::Glow: return {L"Ambient glow",v.glow ? L"On" : L"Off",v.glow};
    case Kind::FloatWindow: return {L"Float window",levelName(v.floatWindow),v.floatWindow>0,false,std::clamp(v.floatWindow,0,3),3};
    case Kind::Rumble: return {L"Rumble",v.rumbleAvailable ? (v.rumble ? L"On" : L"Off") : L"No haptics",v.rumbleAvailable && v.rumble,!v.rumbleAvailable};
    case Kind::Recenter: return {L"Recenter",L"Both grips + A"};
    }
    return {};
}

constexpr float FadedAlpha=0.5f;     // how see-through the panel is while nothing points at it

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
    // The panel itself; the bar that carries it hangs below, with a gap.
    paint(0,0,TexW,static_cast<int>(BodyH),[&](Px& p,float x,float y) {
        const float d=roundRect(x,y,TexW/2.0f,BodyH/2.0f,TexW/2.0f-6,BodyH/2.0f-6,56);
        over(p,{16,18,26,0.94f},cover(d));
        over(p,{255,255,255,0.22f},cover(std::fabs(d+3.0f)-3.0f));
    });
    drawText(pixels,TexW,bgra,48,24,440,84,L"FlatToDepth",56,600,white,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
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
        // Off is blue, on is green; a button with levels moves from one to the other as it is turned up.
        const Col offFill{40,56,100,0.96f},onFill{24,102,90,0.96f};
        const float turnedUp=label.levels>0 && label.level>=0 ? static_cast<float>(label.level)/label.levels : (label.on ? 1.0f : 0.0f);
        Col fillColor=label.dim ? Col{44,46,54,0.9f} : Col{offFill.r+(onFill.r-offFill.r)*turnedUp,offFill.g+(onFill.g-offFill.g)*turnedUp,offFill.b+(onFill.b-offFill.b)*turnedUp,0.96f};
        if (hot) { fillColor.r=std::min(255.0f,fillColor.r+30); fillColor.g=std::min(255.0f,fillColor.g+30); fillColor.b=std::min(255.0f,fillColor.b+30); }
        if (down) fillColor=accent;
        paint(static_cast<int>(b.x)-4,static_cast<int>(b.y)-4,static_cast<int>(b.x+b.w)+4,static_cast<int>(b.y+b.h)+4,[&](Px& p,float x,float y) {
            const float d=roundRect(x,y,b.x+b.w/2,b.y+b.h/2,b.w/2,b.h/2,36);
            over(p,fillColor,cover(d));
            over(p,hot ? Col{150,205,255,1.0f} : Col{255,255,255,0.18f},cover(std::fabs(d+3.0f)-3.0f));
        });
        // The title in white; under it the state in words, in the colour of the state (on, off, unavailable); for a button with
        // levels, dots at the foot say how far it is turned up.
        const bool levelled=label.levels>0 && label.level>=0;
        const Col text=label.dim ? Col{255,255,255,0.45f} : white;
        const Col stateColor=label.dim ? Col{255,170,130,0.95f} : label.on ? Col{160,255,215,1.0f} : Col{180,194,226,0.95f};
        const int titleY=static_cast<int>(b.y)+(levelled ? 6 : 14),captionY=static_cast<int>(b.y)+(levelled ? 52 : 70);
        const int captionSize=levelled ? 42 : label.caption.size()>11 ? 38 : 46;
        drawText(pixels,TexW,bgra,static_cast<int>(b.x)+14,titleY,static_cast<int>(b.w)-28,52,label.title,40,600,text,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        drawText(pixels,TexW,bgra,static_cast<int>(b.x)+14,captionY,static_cast<int>(b.w)-28,levelled ? 50 : 56,label.caption,captionSize,700,stateColor,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        if (levelled) {
            const float cy=b.y+b.h-20,spacing=34,cx0=b.x+b.w/2-spacing*(label.levels-1)/2.0f;
            paint(static_cast<int>(b.x),static_cast<int>(cy)-12,static_cast<int>(b.x+b.w),static_cast<int>(cy)+12,[&](Px& p,float x,float y) {
                for (int k=0;k<label.levels;++k) {
                    const float d=std::hypot(x-(cx0+k*spacing),y-cy)-9.0f;
                    if (k<label.level) over(p,{160,255,215,1.0f},cover(d)); else over(p,{255,255,255,0.35f},cover(std::fabs(d+1.5f)-1.5f));
                }
            });
        }
    }

    // Under the buttons: what the button a laser is on does.
    {
        const auto box=infoBox();
        paint(static_cast<int>(box.x)-4,static_cast<int>(box.y)-4,static_cast<int>(box.x+box.w)+4,static_cast<int>(box.y+box.h)+4,[&](Px& p,float x,float y) {
            const float d=roundRect(x,y,box.x+box.w/2,box.y+box.h/2,box.w/2,box.h/2,30);
            over(p,{8,10,16,0.75f},cover(d)); over(p,{255,255,255,0.12f},cover(std::fabs(d+2.0f)-2.0f));
        });
        drawText(pixels,TexW,bgra,static_cast<int>(box.x)+24,static_cast<int>(box.y)+8,static_cast<int>(box.w)-48,static_cast<int>(box.h)-16,v.help.empty() ? std::wstring(IdleHelp) : v.help,
            28,400,v.help.empty() ? Col{175,185,205,0.85f} : Col{222,228,242,0.97f},DT_LEFT|DT_VCENTER|DT_WORDBREAK);
    }

    // The bar that carries the panel, like the game window's: it lights up under a laser and turns accent-blue while the panel is carried.
    {
        const auto bar=moveBar();
        const Col fillColor=v.carrying ? accent : v.moveHover ? Col{70,112,190,0.97f} : Col{46,52,70,0.95f};
        paint(static_cast<int>(bar.x)-4,static_cast<int>(bar.y)-4,static_cast<int>(bar.x+bar.w)+4,static_cast<int>(bar.y+bar.h)+4,[&](Px& p,float x,float y) {
            const float d=roundRect(x,y,bar.x+bar.w/2,bar.y+bar.h/2,bar.w/2,bar.h/2,bar.h/2);
            over(p,fillColor,cover(d)); over(p,v.moveHover || v.carrying ? Col{150,205,255,1.0f} : Col{255,255,255,0.28f},cover(std::fabs(d+3.0f)-3.0f));
            // A grip of three short lines.
            for (int k=-1;k<=1;++k) over(p,{255,255,255,v.moveHover || v.carrying ? 0.95f : 0.55f},cover(segment(x,y,bar.x+bar.w/2-60,bar.y+bar.h/2+k*13.0f,bar.x+bar.w/2+60,bar.y+bar.h/2+k*13.0f)-3.0f));
        });
    }
    // Nothing points at the panel: let the game show through, so the effect of a setting can be seen behind it.
    if (v.faded) for (auto& d:pixels) if (d>>24) d=pack(unpack(d,bgra),bgra,FadedAlpha);
}
}
