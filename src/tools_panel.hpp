#pragma once
#include "games.hpp"
#include "window_control.hpp"

// The in-headset tools panel: buttons for the Geo-11 fix's shortcuts (there is no keyboard in the headset) and for
// FlatToDepth's own settings, and under them a box that says what the button a laser is on does. It floats in front of you; point a
// laser at a button and pull the trigger or squeeze the grip. A bar underneath, like the game window's, carries it: hold the trigger or a
// grip on the bar and the panel follows your hand in every direction and turns to face you, and the thumbstick pushes it away or pulls it
// closer. It remembers where you put it. Pure maths, so it is unit-tested without a headset.
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

constexpr float PanelW=1.2f,Distance=1.5f,Drop=0.2f;           // metres; in front of you, a little below eye level
constexpr float MinDistance=0.4f,MaxDistance=5.0f;             // how near and far it is remembered
constexpr float MinHandDistance=0.3f,MaxHandDistance=6.0f,MinHeadDistance=0.5f;   // how near and far a hand may carry it
constexpr uint32_t TexW=1536,TexH=844;
constexpr float BodyH=748;                                     // the rounded panel itself, texture pixels; the bar hangs below it
constexpr float PanelH=PanelW*TexH/TexW;
constexpr float PxPerMeter=TexW/PanelW;
constexpr int CloseTarget=1000;                                // hover/click id of the close button
constexpr int MoveTarget=1001;                                 // ... and of the bar that carries the panel

struct PxRect { float x,y,w,h; };                              // texture pixels, origin top-left
inline PxRect cell(size_t index) {
    constexpr float margin=28,gap=16,height=140,top=132,columns=static_cast<float>(Columns);
    constexpr float width=(static_cast<float>(TexW)-2*margin-(columns-1)*gap)/columns;
    return {margin+static_cast<float>(index%Columns)*(width+gap),top+static_cast<float>(index/Columns)*(height+gap),width,height};
}
inline PxRect closeButton() { return {TexW-28.0f-84,24,84,84}; }
// Where the explanation of the hovered button is written, under the buttons.
inline PxRect infoBox() { return {28,596,TexW-56.0f,140}; }
// The bar under the panel that carries it.
inline PxRect moveBar() { return {(TexW-560.0f)/2,768,560,64}; }
inline bool inside(const PxRect& r,float px,float py,float pad=0) { return px>=r.x-pad && px<=r.x+r.w+pad && py>=r.y-pad && py<=r.y+r.h+pad; }
inline float pxX(float x) { return x*PxPerMeter+TexW/2.0f; }   // panel metres (origin at the centre, Y up) to texture pixels
inline float pxY(float y) { return TexH/2.0f-y*PxPerMeter; }
// Where the panel opens, relative to the head's heading: right, up, and forward (negative Z), in metres.
constexpr XrVector3f DefaultOffset{0,-Drop,-Distance};
}

class ToolsPanel {
public:
    using Hand=WindowControl::Hand;
    struct Output {
        int hover=-1;                          // button under a laser (index into the item list)
        bool closeHover=false;
        bool moveHover=false;                  // a laser is on the bar (or is carrying the panel)
        bool moving=false;                     // the panel is being carried right now
        bool moved=false;                      // a carry ended this frame: remember where it is
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
    // Where the panel opens, relative to the head's heading (see tools::DefaultOffset); the carried position is remembered here.
    const XrVector3f& offset() const { return offset_; }
    void setOffset(XrVector3f offset) { offset_=clampOffset(offset); }
    void setPushPullRate(float rate) { pushPullRate_=std::clamp(rate,0.1f,5.0f); }
    // At the remembered offset from the head, at its current heading, so the panel is wherever you are looking when it opens.
    void place(const XrPosef& head) {
        const auto yaw=pose::yawRotation(head.orientation);
        pose_={yaw,pose::add(head.position,pose::rotate(yaw,offset_))};
    }
    void show(const XrPosef& head) { place(head); open_=true; drag_=-1; reset(); }
    void hide() { open_=false; drag_=-1; last_[0]=last_[1]=-1; }
    void toggle(const XrPosef& head) { if (open_) hide(); else show(head); }
    // Buttons held right now must be released before they can click.
    void reset() { prevClick_[0]=prevClick_[1]=true; }

    // `head` is where the head is now (the panel turns to face it while being carried); dt is the frame time.
    Output update(const Hand (&hands)[2],size_t itemCount,const XrPosef& head=XrPosef{{0,0,0,1},{0,0,0}},float dt=1.0f/90) {
        Output out;
        if (!open_) return out;
        dt=std::clamp(dt,0.0f,0.1f);
        for (int i=0;i<2;++i) {
            const bool click=hands[i].valid && hands[i].click;
            if (drag_==i) {
                if (click) { carry(i,hands[i],head,dt,out); prevClick_[i]=true; last_[i]=tools::MoveTarget; continue; }
                finishCarry(head,out);                       // let go: fall through and treat the hand as free again
            }
            int target=-1;
            PlaneHit hit;
            if (hands[i].valid) {
                hit=hitPlane(hands[i].aim,pose_);
                if (hit.ok && std::fabs(hit.x)<=tools::PanelW/2+0.05f && std::fabs(hit.y)<=tools::PanelH/2+0.05f) {
                    const float px=tools::pxX(hit.x),py=tools::pxY(hit.y);
                    if (tools::inside(tools::closeButton(),px,py,10)) target=tools::CloseTarget;
                    else if (tools::inside(tools::moveBar(),px,py,16)) target=tools::MoveTarget;
                    else for (size_t k=0;k<itemCount && k<tools::MaxItems;++k) if (tools::inside(tools::cell(k),px,py,6)) { target=static_cast<int>(k); break; }
                    const auto dir=pose::rotate(hands[i].aim.orientation,{0,0,-1});
                    out.pointer[i]=true;
                    out.rayStart[i]=pose::add(hands[i].aim.position,pose::scale(dir,0.03f)); out.rayEnd[i]=hit.world;
                }
            }
            out.pressed[i]=click && target>=0;
            if (target==tools::CloseTarget) out.closeHover=true;
            else if (target==tools::MoveTarget) out.moveHover=true;
            else if (target>=0 && out.hover<0) out.hover=target;
            if (target>=0 && target!=last_[i]) out.entered[i]=true;
            last_[i]=target;
            if (click && !prevClick_[i] && target>=0) {
                if (target==tools::CloseTarget) out.close=true;
                else if (target==tools::MoveTarget) { if (drag_<0) beginCarry(i,hands[i],hit,out); }
                else if (out.clicked<0) out.clicked=target;
                if (out.clickHand<0) out.clickHand=i;
            }
            prevClick_[i]=click;
        }
        return out;
    }
private:
    XrPosef pose_{{0,0,0,1},{0,0,-tools::Distance}};
    XrVector3f offset_=tools::DefaultOffset;
    bool open_=false;
    bool prevClick_[2]{};
    int last_[2]{-1,-1};
    float pushPullRate_=1.1f;
    // Carrying works as it does for the game window: the panel keeps its place in the hand's own frame, so it moves and swings with the
    // hand in every direction; the thumbstick scales that place, and the panel keeps facing the head.
    int drag_=-1;                              // the hand carrying the panel, or -1
    XrVector3f grab_{};                        // the panel's position in the hand's frame, as it was taken hold of
    float scale_=1;                            // how far the thumbstick has pushed it along that

    static XrVector3f clampOffset(XrVector3f o) {
        const float length=pose::length(o);
        if (length>tools::MaxDistance) o=pose::scale(o,tools::MaxDistance/length);
        else if (length<tools::MinDistance) o=length>1e-4f ? pose::scale(o,tools::MinDistance/length) : tools::DefaultOffset;
        return o;
    }
    static float deadzone(float v) {
        const float a=std::fabs(v);
        return a<0.2f ? 0.0f : std::copysign(std::min(1.0f,(a-0.2f)/0.8f),v);
    }
    void beginCarry(int hand,const Hand& h,const PlaneHit&,Output& out) {
        drag_=hand; scale_=1;
        grab_=pose::unrotate(h.aim.orientation,pose::sub(pose_.position,h.aim.position));
        out.moving=true;
    }
    void carry(int hand,const Hand& h,const XrPosef& head,float dt,Output& out) {
        const float stick=deadzone(h.stickY);
        if (stick!=0) {
            // Exponential, so pushing feels the same near and far; a push that would go too far or too near is refused, not clamped,
            // so the stick cannot wind up.
            const float next=scale_*std::exp(pushPullRate_*stick*dt);
            const float reach=pose::length(grab_)*next;
            const auto centre=pose::add(h.aim.position,pose::rotate(h.aim.orientation,pose::scale(grab_,next)));
            const bool nearer=next<scale_;
            const bool ok=nearer ? (reach>=tools::MinHandDistance && pose::length(pose::sub(centre,head.position))>=tools::MinHeadDistance) : reach<=tools::MaxHandDistance;
            if (ok) scale_=next;
        }
        pose_.position=pose::add(h.aim.position,pose::rotate(h.aim.orientation,pose::scale(grab_,scale_)));
        pose_.orientation=pose::nlerp(pose_.orientation,pose::facing(pose_.position,head.position,pose_.orientation),1-std::exp(-14*dt));
        const auto dir=pose::rotate(h.aim.orientation,{0,0,-1});
        out.moving=true; out.moveHover=true; out.pointer[hand]=true; out.pressed[hand]=true;
        out.rayStart[hand]=pose::add(h.aim.position,pose::scale(dir,0.03f));
        out.rayEnd[hand]=pose::add(h.aim.position,pose::scale(dir,std::max(0.1f,pose::length(grab_)*scale_)));
    }
    // Remembers where the panel ended up, in the head's own heading, so it opens there next time.
    void finishCarry(const XrPosef& head,Output& out) {
        drag_=-1;
        const auto yaw=pose::yawRotation(head.orientation);
        offset_=clampOffset(pose::unrotate(yaw,pose::sub(pose_.position,head.position)));
        out.moved=true;
    }
};

// The panel (its own texture, sheet 2) with the laser and cursor on top (from the shared atlas).
inline std::vector<UiQuad> buildToolsQuads(const ToolsPanel& panel,const ToolsPanel::Output& o,const XrPosef& head) {
    std::vector<UiQuad> q;
    q.push_back({panel.pose(),{tools::PanelW,tools::PanelH},{0,0,tools::TexW,tools::TexH},2});
    for (int i=0;i<2;++i) if (o.pointer[i]) appendPointerQuads(q,i,o.rayStart[i],o.rayEnd[i],panel.pose().orientation,head);
    return q;
}
