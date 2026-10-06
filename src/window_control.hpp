#pragma once
#include "ui_atlas.hpp"
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace pose {
inline XrVector3f add(XrVector3f a,XrVector3f b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline XrVector3f sub(XrVector3f a,XrVector3f b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline XrVector3f scale(XrVector3f a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(XrVector3f a,XrVector3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float length(XrVector3f a) { return std::sqrt(dot(a,a)); }
inline XrVector3f cross(XrVector3f a,XrVector3f b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline XrVector3f normalize(XrVector3f v) { const float n=length(v); return n>1e-9f ? scale(v,1/n) : XrVector3f{0,0,0}; }
inline XrQuaternionf inverse(XrQuaternionf q) { return {-q.x,-q.y,-q.z,q.w}; }
inline XrQuaternionf multiply(XrQuaternionf a,XrQuaternionf b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline XrVector3f rotate(XrQuaternionf q,XrVector3f p) {
    auto t=scale(cross({q.x,q.y,q.z},p),2);
    return add(p,add(scale(t,q.w),cross({q.x,q.y,q.z},t)));
}
inline XrVector3f unrotate(XrQuaternionf q,XrVector3f p) { return rotate(inverse(q),p); }
inline XrQuaternionf normalize(XrQuaternionf q) {
    float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return n>1e-6f ? XrQuaternionf{q.x/n,q.y/n,q.z/n,q.w/n} : XrQuaternionf{0,0,0,1};
}
inline XrQuaternionf between(XrVector3f from,XrVector3f to) {
    from=scale(from,1/std::max(length(from),1e-6f)); to=scale(to,1/std::max(length(to),1e-6f));
    float d=std::clamp(dot(from,to),-1.0f,1.0f);
    if (d<-0.9999f) {
        auto axis=cross(from,std::abs(from.x)<0.8f ? XrVector3f{1,0,0} : XrVector3f{0,1,0});
        axis=scale(axis,1/length(axis)); return {axis.x,axis.y,axis.z,0};
    }
    auto c=cross(from,to); return normalize({c.x,c.y,c.z,1+d});
}
// Rotation whose local X, Y, Z axes are the given orthonormal vectors.
inline XrQuaternionf fromBasis(XrVector3f r,XrVector3f u,XrVector3f f) {
    const float m00=r.x,m01=u.x,m02=f.x, m10=r.y,m11=u.y,m12=f.y, m20=r.z,m21=u.z,m22=f.z;
    const float trace=m00+m11+m22;
    XrQuaternionf q;
    if (trace>0) { const float s=std::sqrt(trace+1)*2; q={(m21-m12)/s,(m02-m20)/s,(m10-m01)/s,0.25f*s}; }
    else if (m00>m11 && m00>m22) { const float s=std::sqrt(1+m00-m11-m22)*2; q={0.25f*s,(m01+m10)/s,(m02+m20)/s,(m21-m12)/s}; }
    else if (m11>m22) { const float s=std::sqrt(1+m11-m00-m22)*2; q={(m01+m10)/s,0.25f*s,(m12+m21)/s,(m02-m20)/s}; }
    else { const float s=std::sqrt(1+m22-m00-m11)*2; q={(m02+m20)/s,(m12+m21)/s,0.25f*s,(m10-m01)/s}; }
    return normalize(q);
}
// The heading of an orientation as a rotation about the vertical axis alone.
inline XrQuaternionf yawRotation(XrQuaternionf q) {
    const float yaw=std::atan2(2*(q.x*q.z+q.w*q.y),1-2*(q.x*q.x+q.y*q.y));
    return {0,std::sin(yaw/2),0,std::cos(yaw/2)};
}
inline XrQuaternionf nlerp(XrQuaternionf a,XrQuaternionf b,float t) {
    const float sign=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0 ? -1.0f : 1.0f;
    return normalize({a.x+(sign*b.x-a.x)*t,a.y+(sign*b.y-a.y)*t,a.z+(sign*b.z-a.z)*t,a.w+(sign*b.w-a.w)*t});
}
// Pose that faces the head (+Z towards it) with no roll against world up. Near straight up/down the
// horizon is undefined, so the window's previous roll takes over smoothly.
inline XrQuaternionf facing(XrVector3f position,XrVector3f head,XrQuaternionf previous) {
    const auto n=normalize(sub(head,position));
    if (length(n)<0.5f) return previous;
    const auto horizon=cross({0,1,0},n);
    const float h=length(horizon);
    auto old=rotate(previous,{1,0,0});
    old=normalize(sub(old,scale(n,dot(old,n))));
    if (length(old)<0.5f) old=horizon;
    const float blend=std::clamp(h/0.3f,0.0f,1.0f);
    auto right=h>1e-6f ? normalize(add(scale(horizon,blend/h),scale(old,1-blend))) : old;
    if (length(right)<0.5f) right=old;
    return fromBasis(right,cross(n,right),n);
}
}

enum class UiTarget { None, Window, Bar, Corner, Tools };
// Something the laser can press or drag: it captures the controller from the game.
inline bool isHandle(UiTarget t) { return t==UiTarget::Bar || t==UiTarget::Corner || t==UiTarget::Tools; }

// Window-space layout (X right, Y up, origin at the window centre, window plane Z=0). Sizes scale with the
// viewing distance so the bar and handle keep roughly the same apparent size however far away the window is.
// On a curved screen the same numbers are arc length along the surface and height, see onScreen().
struct WindowLayout {
    float width=0,height=0;
    float barW=0,barH=0,barY=0;                    // bar centre (0,barY)
    float handle=0,handleX=0,handleY=0;            // square resize handle at the bottom-right, centre (handleX,handleY)
    float barPad=0,handlePad=0;                    // extra reach for grabbing
    float barReveal=0,handleReveal=0;              // extra reach for fading the controls in
    bool hasTools=false;                           // the round tools button left of the bar
    float tools=0,toolsX=0,toolsY=0,toolsPad=0,toolsReveal=0;
};
inline WindowLayout windowLayout(float width,float aspect,float distance,bool withTools=false) {
    WindowLayout l;
    l.width=width; l.height=width*aspect;
    l.barH=std::min(std::clamp(0.045f*distance,0.04f,0.25f),0.07f*width);
    l.barW=8*l.barH;                                // the atlas bar is 8:1
    l.barY=-(l.height/2+l.barH);                    // half a bar of gap below the window
    l.handle=1.1f*l.barH;
    l.handleX=l.width/2+0.58f*l.handle; l.handleY=-(l.height/2+0.58f*l.handle);
    l.barPad=0.5f*l.barH; l.handlePad=0.4f*l.handle;
    l.barReveal=0.8f*l.barH; l.handleReveal=0.8f*l.handle;
    if (withTools) {
        l.hasTools=true; l.tools=1.1f*l.barH;
        l.toolsX=-(l.barW/2+0.35f*l.barH+0.55f*l.tools); l.toolsY=l.barY;
        l.toolsPad=0.25f*l.tools; l.toolsReveal=0.8f*l.tools;
    }
    return l;
}
// What a laser hitting window-space (x,y) is on. `reveal` is true near the bar or the handle, which is the only
// time the controls fade in: pointing at the game itself, or anywhere else, keeps them hidden.
inline UiTarget classify(const WindowLayout& l,float x,float y,bool& reveal) {
    auto inside=[&](float cx,float cy,float hw,float hh,float pad) { return std::fabs(x-cx)<=hw+pad && std::fabs(y-cy)<=hh+pad; };
    reveal=inside(0,l.barY,l.barW/2,l.barH/2,l.barReveal) || inside(l.handleX,l.handleY,l.handle/2,l.handle/2,l.handleReveal) ||
        (l.hasTools && inside(l.toolsX,l.toolsY,l.tools/2,l.tools/2,l.toolsReveal));
    if (inside(l.handleX,l.handleY,l.handle/2,l.handle/2,l.handlePad)) return UiTarget::Corner;
    if (l.hasTools && inside(l.toolsX,l.toolsY,l.tools/2,l.tools/2,l.toolsPad)) return UiTarget::Tools;
    if (inside(0,l.barY,l.barW/2,l.barH/2,l.barPad)) return UiTarget::Bar;
    return inside(0,0,l.width/2,l.height/2,0) ? UiTarget::Window : UiTarget::None;
}

struct PlaneHit {
    bool ok=false;
    float t=0,x=0,y=0;          // ray distance, then plane-local coordinates
    XrVector3f world{};
};
// Ray from an aim pose (forward is -Z) against the front side of a plane pose.
inline PlaneHit hitPlane(const XrPosef& aim,const XrPosef& plane) {
    PlaneHit h;
    const auto dir=pose::rotate(aim.orientation,{0,0,-1});
    const auto o=pose::unrotate(plane.orientation,pose::sub(aim.position,plane.position));
    const auto d=pose::unrotate(plane.orientation,dir);
    if (o.z<=0.01f || d.z>=-1e-4f) return h;
    h.t=-o.z/d.z;
    if (h.t>200) return h;
    h.x=o.x+d.x*h.t; h.y=o.y+d.y*h.t; h.world=pose::add(aim.position,pose::scale(dir,h.t));
    h.ok=true; return h;
}

// A curved screen is a section of a vertical cylinder, concave towards the viewer and tangent to the window plane
// at the window's centre, so its axis runs through window-space (0,*,radius). Positions on it are "unrolled": x is
// the arc length from the centre line, y the height, exactly the flat window's coordinates, which is what lets the
// bar, handle and resizing work unchanged. radius<=0 means flat.
constexpr float MaxArc=3.0f;    // radians: a curved screen never wraps further round the viewer than this
inline float effectiveRadius(float radius,float width) { return radius>0 ? std::max(radius,width/MaxArc) : 0.0f; }
// Radius that keeps a screen `distance` away roughly concentric with the viewer; curvature 1 wraps right round them,
// smaller values are gentler. 0 is flat.
inline float curveRadius(float distance,float curvature) {
    return curvature>0.01f ? std::max(0.5f,distance)/std::min(curvature,1.0f) : 0.0f;
}
// Ray against the screen, flat or curved. Coordinates in the result are unrolled (see above).
inline PlaneHit hitScreen(const XrPosef& aim,const XrPosef& plane,float radius) {
    if (radius<=0) return hitPlane(aim,plane);
    PlaneHit h;
    const auto dir=pose::rotate(aim.orientation,{0,0,-1});
    const auto o=pose::unrotate(plane.orientation,pose::sub(aim.position,plane.position));
    const auto d=pose::unrotate(plane.orientation,dir);
    // Horizontal section: circle of the given radius around the axis. The laser must start inside it (in front of the
    // screen) and hits the wall where it leaves.
    const float px=o.x,pz=o.z-radius;
    const float a=d.x*d.x+d.z*d.z;
    const float c=px*px+pz*pz-radius*radius;
    if (a<1e-8f || c>=0) return h;
    const float b=2*(px*d.x+pz*d.z);
    h.t=(-b+std::sqrt(b*b-4*a*c))/(2*a);
    if (h.t<=0 || h.t>200) return h;
    const float x=o.x+d.x*h.t,z=o.z+d.z*h.t;
    h.x=radius*std::atan2(x,radius-z); h.y=o.y+d.y*h.t; h.world=pose::add(aim.position,pose::scale(dir,h.t));
    h.ok=true; return h;
}
// The pose of the screen's surface at unrolled (u,v): where to put something that should sit on it, facing the
// viewer. Flat: in the plane. Curved: on the cylinder, turned to face its axis.
inline XrPosef onScreen(const XrPosef& window,float radius,float u,float v) {
    if (radius<=0) return {window.orientation,pose::add(window.position,pose::rotate(window.orientation,{u,v,0}))};
    const float phi=u/radius;
    const XrQuaternionf turn{0,-std::sin(phi/2),0,std::cos(phi/2)};
    const XrVector3f offset{radius*std::sin(phi),v,radius*(1-std::cos(phi))};
    return {pose::multiply(window.orientation,turn),pose::add(window.position,pose::rotate(window.orientation,offset))};
}
// Where a curved screen's cylinder axis is: radius in front of the window, towards the viewer.
inline XrVector3f screenAxis(const XrPosef& window,float radius) {
    return pose::add(window.position,pose::rotate(window.orientation,{0,0,radius}));
}

// SteamVR-style window manipulation. Point a controller at the spot just under the window and a bar and a
// resize handle fade in; point anywhere else and they fade out. Trigger or grip on the bar grabs the window:
// it stays attached to your laser, the thumbstick pushes/pulls it along the laser, and it keeps facing you.
// Trigger or grip on the handle resizes the window about its centre, so it grows out to both sides.
// Pure maths, so it is unit-tested without a headset.
class WindowControl {
public:
    enum class Mode { Idle, Move, Resize };
    struct Settings { float pushPullRate=1.1f; bool autoHide=true; float minWidth=0.3f, maxWidth=20.0f; bool tools=false; };
    struct Hand {
        bool valid=false;
        XrPosef aim{{0,0,0,1},{0,0,0}};
        bool click=false;       // trigger or grip pressed
        float stickY=0;         // thumbstick forward(+)/back(-)
    };
    struct Output {
        bool released=false;            // a drag ended this frame: persist the placement
        bool capture[2]{};              // this controller is busy with the UI; hide it from the game
        bool pointer[2]{};              // draw this hand's laser and cursor
        bool pressed[2]{};              // this hand is dragging
        UiTarget hover[2]{};
        UiTarget held=UiTarget::None;
        XrVector3f rayStart[2]{},rayEnd[2]{};
        float alpha=0;                  // bar/handle visibility, 0..1
        WindowLayout layout;
        bool toolsClicked=false;        // the tools button was pressed this frame
        float radius=0;                 // curved screen radius in effect (0 = flat); the layout is arc length on it
        XrQuaternionf cursorPlane[2]{{0,0,0,1},{0,0,0,1}};   // surface orientation under each cursor, for curved screens
    };
    // Controls fade in quickly, and out soon after the laser leaves them (the hold stops edge flicker).
    static constexpr float MinHandDistance=0.3f, MaxHandDistance=60, MinHeadDistance=0.5f, RevealHold=0.25f, FadeIn=10, FadeOut=8;

    WindowControl() = default;
    explicit WindowControl(Settings s) : s_(s) {}
    void configure(const Settings& s) { s_=s; }
    // Curved screen radius (0 = flat); see curveRadius(). Held fixed while a resize drag is under way.
    void setRadius(float radius) { if (mode_!=Mode::Resize) radius_=std::max(0.0f,radius); }
    float radius() const { return radius_; }
    Mode mode() const { return mode_; }
    // Abort any drag. Buttons held right now must be released before they can start a new one.
    void reset() { mode_=Mode::Idle; hand_=-1; prevClick_[0]=prevClick_[1]=true; }

    Output update(const Hand (&hands)[2],const XrPosef& head,XrPosef& window,float& width,float aspect,float dt) {
        dt=std::clamp(dt,0.0f,0.1f);
        Output out;
        const float radius=effectiveRadius(radius_,width);
        const auto layout=windowLayout(width,aspect,std::max(0.5f,pose::length(pose::sub(window.position,head.position))),s_.tools);
        PlaneHit hit[2];
        UiTarget target[2]{};
        bool reveal=false;
        for (int i=0;i<2;++i) {
            if (!hands[i].valid) continue;
            hit[i]=hitScreen(hands[i].aim,window,radius);
            if (!hit[i].ok) continue;
            bool r=false; target[i]=classify(layout,hit[i].x,hit[i].y,r); reveal|=r;
        }
        if (mode_!=Mode::Idle) {
            const Hand& h=hands[hand_];
            if (!h.valid || !h.click) { mode_=Mode::Idle; hand_=-1; out.released=true; }
            else if (mode_==Mode::Move) move(h,head,window,dt);
            else resize(h,window,width,aspect,radius);
        } else {
            for (int i=0;i<2 && mode_==Mode::Idle;++i) {
                if (!hands[i].valid || !hands[i].click || prevClick_[i] || !isHandle(target[i])) continue;
                if (target[i]==UiTarget::Tools) { out.toolsClicked=true; continue; }   // a press, not a drag
                hand_=i;
                if (target[i]==UiTarget::Bar) beginMove(hands[i],window,hit[i].t);
                else beginResize(window,width,aspect,hit[i]);
            }
        }
        const bool engaged=mode_!=Mode::Idle;
        linger_=(reveal || engaged) ? 0 : linger_+dt;
        const bool show=!s_.autoHide || engaged || linger_<RevealHold;
        alpha_=show ? std::min(1.0f,alpha_+FadeIn*dt) : std::max(0.0f,alpha_-FadeOut*dt);
        for (int i=0;i<2;++i) {
            prevClick_[i]=hands[i].click;
            const bool active=engaged && hand_==i;
            out.pressed[i]=active; out.hover[i]=target[i];
            out.capture[i]=active || isHandle(target[i]);
            out.pointer[i]=hands[i].valid && out.capture[i];
            if (!hands[i].valid) continue;
            const auto dir=pose::rotate(hands[i].aim.orientation,{0,0,-1});
            out.rayStart[i]=pose::add(hands[i].aim.position,pose::scale(dir,0.03f));
            out.rayEnd[i]=hit[i].world;
            if (active) out.rayEnd[i]=mode_==Mode::Move ? pose::add(hands[i].aim.position,pose::scale(dir,grabDistance_*scale_)) : lastEnd_;
        }
        if (mode_==Mode::Move) out.held=UiTarget::Bar;
        else if (mode_==Mode::Resize) out.held=UiTarget::Corner;
        out.alpha=alpha_;
        out.radius=effectiveRadius(radius_,width);
        out.layout=windowLayout(width,aspect,std::max(0.5f,pose::length(pose::sub(window.position,head.position))),s_.tools);
        for (int i=0;i<2;++i)
            out.cursorPlane[i]=hit[i].ok && !(mode_!=Mode::Idle && hand_==i) ? onScreen(window,out.radius,hit[i].x,hit[i].y).orientation : window.orientation;
        return out;
    }

private:
    Settings s_;
    Mode mode_=Mode::Idle;
    int hand_=-1;
    bool prevClick_[2]{};
    float alpha_=0,linger_=100,radius_=0;
    // Move: window offset in the hand's frame and how far the thumbstick has pushed it along the laser.
    XrVector3f offset_{};
    float scale_=1,grabDistance_=1;
    // Resize: the window's centre and orientation stay fixed; grab is where on the handle the hand took hold.
    XrVector3f center_{};
    XrQuaternionf orient_{0,0,0,1};
    float grabX_=0,grabY_=0;
    XrVector3f lastEnd_{};

    static float deadzone(float v) {
        const float a=std::fabs(v);
        return a<0.2f ? 0.0f : std::copysign(std::min(1.0f,(a-0.2f)/0.8f),v);
    }
    void beginMove(const Hand& h,const XrPosef& window,float rayDistance) {
        mode_=Mode::Move;
        offset_=pose::unrotate(h.aim.orientation,pose::sub(window.position,h.aim.position));
        scale_=1; grabDistance_=rayDistance;
    }
    void move(const Hand& h,const XrPosef& head,XrPosef& window,float dt) {
        const float stick=deadzone(h.stickY);
        if (stick!=0) {
            // Exponential, so pushing feels the same near and far.
            const float next=scale_*std::exp(s_.pushPullRate*stick*dt);
            const float reach=pose::length(offset_)*next;
            const auto center=pose::add(h.aim.position,pose::rotate(h.aim.orientation,pose::scale(offset_,next)));
            const bool nearer=next<scale_;
            const bool ok=nearer ? (reach>=MinHandDistance && pose::length(pose::sub(center,head.position))>=MinHeadDistance) : reach<=MaxHandDistance;
            if (ok) scale_=next;   // refusing (not clamping) keeps the stick from winding up
        }
        window.position=pose::add(h.aim.position,pose::rotate(h.aim.orientation,pose::scale(offset_,scale_)));
        window.orientation=pose::nlerp(window.orientation,pose::facing(window.position,head.position,window.orientation),1-std::exp(-14*dt));
    }
    void beginResize(const XrPosef& window,float width,float aspect,const PlaneHit& hit) {
        mode_=Mode::Resize;
        center_=window.position; orient_=window.orientation;
        // Offset from the window's bottom-right corner to where the hand took hold of the handle.
        grabX_=hit.x-width/2; grabY_=hit.y+width*aspect/2;
        lastEnd_=hit.world;
    }
    void resize(const Hand& h,XrPosef& window,float& width,float aspect,float radius) {
        const auto hit=hitScreen(h.aim,{orient_,center_},radius);
        if (!hit.ok) return;    // laser left the window plane: hold the current size
        // The dragged corner sits at (width/2, -height/2) from the centre; project it onto that diagonal so the
        // aspect ratio holds. The centre never moves, so the window grows out to both sides.
        const float x=hit.x-grabX_, y=hit.y-grabY_;
        width=std::clamp(2*(x-aspect*y)/(1+aspect*aspect),s_.minWidth,s_.maxWidth);
        window.position=center_; window.orientation=orient_;
        lastEnd_=hit.world;
    }
};

inline ui::Visual visualFor(const WindowControl::Output& o,bool toolsOpen=false) {
    ui::Visual v;
    v.alpha=std::clamp(static_cast<int>(std::lround(o.alpha*ui::FadeSteps)),0,ui::FadeSteps);
    auto level=[&](UiTarget t) {
        if (o.held==t) return 2;
        for (int i=0;i<2;++i) if (o.hover[i]==t && o.pointer[i]) return 1;
        return 0;
    };
    v.bar=level(UiTarget::Bar); v.corner=level(UiTarget::Corner);
    v.tools=toolsOpen ? 2 : level(UiTarget::Tools);    // the tools button stays lit while its panel is open
    v.held[0]=o.pressed[0]; v.held[1]=o.pressed[1];
    return v;
}

// One textured rectangle for the compositor. sheet picks the texture: 0 is the shared atlas (bar, handle, lasers,
// cursors), 1 is the game menu panel.
struct UiQuad { XrPosef pose; XrExtent2Df size; ui::Rect rect; int sheet=0; };

// A hand's laser beam and cursor dot. The beam is a thin quad that turns about its own axis to face the viewer;
// the dot lies in a plane with the given orientation, just in front of it.
inline void appendPointerQuads(std::vector<UiQuad>& q,int hand,XrVector3f start,XrVector3f end,XrQuaternionf plane,const XrPosef& head) {
    const auto span=pose::sub(end,start);
    const float length=pose::length(span);
    if (length>0.05f) {
        const auto x=pose::scale(span,1/length), mid=pose::add(start,pose::scale(span,0.5f));
        auto toHead=pose::sub(head.position,mid);
        toHead=pose::sub(toHead,pose::scale(x,pose::dot(toHead,x)));
        auto z=pose::normalize(toHead);
        if (pose::length(z)<0.5f) z=pose::normalize(pose::cross(x,std::fabs(x.y)<0.9f ? XrVector3f{0,1,0} : XrVector3f{1,0,0}));
        q.push_back({{pose::fromBasis(x,pose::cross(z,x),z),mid},{length,0.006f+0.0015f*length},ui::LaserRect[hand]});
    }
    const auto normal=pose::rotate(plane,{0,0,1});
    const float size=std::clamp(0.018f*pose::length(pose::sub(end,head.position)),0.03f,0.4f);
    q.push_back({{plane,pose::add(end,pose::scale(normal,0.004f))},{size,size},ui::DotRect[hand]});
}

// World-locked quads for the bar, handle, lasers and cursors, in draw order (later is on top).
inline std::vector<UiQuad> buildUiQuads(const WindowControl::Output& o,const XrPosef& window,const XrPosef& head) {
    std::vector<UiQuad> q;
    const auto& l=o.layout;
    // On a curved screen the controls sit on the surface and turn to face its axis, like the picture does.
    if (visualFor(o).alpha>0) {
        q.push_back({onScreen(window,o.radius,0,l.barY),{l.barW,l.barH},ui::BarRect});
        q.push_back({onScreen(window,o.radius,l.handleX,l.handleY),{l.handle,l.handle},ui::CornerRect});
        if (l.hasTools) q.push_back({onScreen(window,o.radius,l.toolsX,l.toolsY),{l.tools,l.tools},ui::ToolsRect});
    }
    for (int i=0;i<2;++i) if (o.pointer[i])
        appendPointerQuads(q,i,o.rayStart[i],o.rayEnd[i],o.radius>0 ? o.cursorPlane[i] : window.orientation,head);
    return q;
}
