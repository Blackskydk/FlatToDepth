#pragma once
#include "games.hpp"
#include "window_control.hpp"

// The in-headset tools panel: buttons for the Geo-11 fix's shortcuts (there is no keyboard in the headset) and for
// FlatToDepth's own settings. It floats in front of you where you opened it; point a laser at a button and pull the trigger
// or squeeze the grip. Pure maths, so it is unit-tested without a headset.
namespace tools {
enum class Kind { Key, SwapEyes, Curve, Glow, FloatWindow, Rumble, Recenter };
struct Item {
    Kind kind;
    size_t key=0;      // Kind::Key: index into the game's shortcut list
    bool operator==(const Item&) const=default;
};
constexpr size_t Columns=4,Rows=3,MaxItems=Columns*Rows;
// The buttons for a game: its fix's shortcuts first, then FlatToDepth's own settings.
inline std::vector<Item> items(size_t game) {
    std::vector<Item> v;
    if (game<Games.size()) for (size_t k=0;k<Games[game].keys.size() && v.size()<MaxItems;++k) v.push_back({Kind::Key,k});
    for (const Kind kind:{Kind::SwapEyes,Kind::Curve,Kind::Glow,Kind::FloatWindow,Kind::Rumble,Kind::Recenter})
        if (v.size()<MaxItems) v.push_back({kind,0});
    return v;
}

constexpr float PanelW=2.4f,Distance=1.7f,Drop=0.2f;          // metres; in front of you, a little below eye level
constexpr uint32_t TexW=1536,TexH=640;
constexpr float PanelH=PanelW*TexH/TexW;
constexpr float PxPerMeter=TexW/PanelW;
constexpr int CloseTarget=1000;                                // hover/click id of the close button

struct PxRect { float x,y,w,h; };                              // texture pixels, origin top-left
inline PxRect cell(size_t index) {
    constexpr float margin=28,gap=16,height=140,top=132,columns=static_cast<float>(Columns);
    constexpr float width=(static_cast<float>(TexW)-2*margin-(columns-1)*gap)/columns;
    return {margin+static_cast<float>(index%Columns)*(width+gap),top+static_cast<float>(index/Columns)*(height+gap),width,height};
}
inline PxRect closeButton() { return {TexW-28.0f-84,24,84,84}; }
inline bool inside(const PxRect& r,float px,float py,float pad=0) { return px>=r.x-pad && px<=r.x+r.w+pad && py>=r.y-pad && py<=r.y+r.h+pad; }
inline float pxX(float x) { return x*PxPerMeter+TexW/2.0f; }   // panel metres (origin at the centre, Y up) to texture pixels
inline float pxY(float y) { return TexH/2.0f-y*PxPerMeter; }
}

class ToolsPanel {
public:
    using Hand=WindowControl::Hand;
    struct Output {
        int hover=-1;                          // button under a laser (index into the item list)
        bool closeHover=false;
        int clicked=-1;                        // a fresh click landed on this button this frame
        bool close=false;                      // ... on the close button
        bool pointer[2]{};                     // this hand's laser is on the panel: draw it, and keep the game from it
        bool pressed[2]{};                     // this hand is pressing a button
        bool entered[2]{};                     // this hand's laser just moved onto a button (for a tick)
        int clickHand=-1;                      // which hand made the fresh click, if any
        XrVector3f rayStart[2]{},rayEnd[2]{};
    };
    bool open() const { return open_; }
    const XrPosef& pose() const { return pose_; }
    // In front of the head at its current heading, so the panel is wherever you are looking when it opens.
    void place(const XrPosef& head) {
        const auto yaw=pose::yawRotation(head.orientation);
        pose_={yaw,pose::add(head.position,pose::rotate(yaw,{0,-tools::Drop,-tools::Distance}))};
    }
    void show(const XrPosef& head) { place(head); open_=true; reset(); }
    void hide() { open_=false; last_[0]=last_[1]=-1; }
    void toggle(const XrPosef& head) { if (open_) hide(); else show(head); }
    // Buttons held right now must be released before they can click.
    void reset() { prevClick_[0]=prevClick_[1]=true; }

    Output update(const Hand (&hands)[2],size_t itemCount) {
        Output out;
        if (!open_) return out;
        for (int i=0;i<2;++i) {
            const bool click=hands[i].valid && hands[i].click;
            int target=-1;
            if (hands[i].valid) {
                const auto hit=hitPlane(hands[i].aim,pose_);
                if (hit.ok && std::fabs(hit.x)<=tools::PanelW/2+0.05f && std::fabs(hit.y)<=tools::PanelH/2+0.05f) {
                    const float px=tools::pxX(hit.x),py=tools::pxY(hit.y);
                    if (tools::inside(tools::closeButton(),px,py,10)) target=tools::CloseTarget;
                    else for (size_t k=0;k<itemCount && k<tools::MaxItems;++k) if (tools::inside(tools::cell(k),px,py,6)) { target=static_cast<int>(k); break; }
                    const auto dir=pose::rotate(hands[i].aim.orientation,{0,0,-1});
                    out.pointer[i]=true;
                    out.rayStart[i]=pose::add(hands[i].aim.position,pose::scale(dir,0.03f)); out.rayEnd[i]=hit.world;
                }
            }
            out.pressed[i]=click && target>=0;
            if (target==tools::CloseTarget) out.closeHover=true; else if (target>=0 && out.hover<0) out.hover=target;
            if (target>=0 && target!=last_[i]) out.entered[i]=true;
            last_[i]=target;
            if (click && !prevClick_[i] && target>=0) {
                if (target==tools::CloseTarget) out.close=true; else if (out.clicked<0) out.clicked=target;
                if (out.clickHand<0) out.clickHand=i;
            }
            prevClick_[i]=click;
        }
        return out;
    }
private:
    XrPosef pose_{{0,0,0,1},{0,0,-tools::Distance}};
    bool open_=false;
    bool prevClick_[2]{};
    int last_[2]{-1,-1};
};

// The panel (its own texture, sheet 2) with the laser and cursor on top (from the shared atlas).
inline std::vector<UiQuad> buildToolsQuads(const ToolsPanel& panel,const ToolsPanel::Output& o,const XrPosef& head) {
    std::vector<UiQuad> q;
    q.push_back({panel.pose(),{tools::PanelW,tools::PanelH},{0,0,tools::TexW,tools::TexH},2});
    for (int i=0;i<2;++i) if (o.pointer[i]) appendPointerQuads(q,i,o.rayStart[i],o.rayEnd[i],panel.pose().orientation,head);
    return q;
}
