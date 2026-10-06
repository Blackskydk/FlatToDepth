#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// CPU-rasterised texture atlas for the window UI: grab bar, resize handle, cursor dots and laser beams.
// Everything is drawn from signed-distance shapes, so there are no image assets to ship.
// Pixels are straight (unpremultiplied) alpha in sRGB; transparent pixels keep their shape colour so
// bilinear filtering never pulls dark fringes into the edges.
namespace ui {
constexpr uint32_t AtlasW=1024, AtlasH=256;
struct Rect { uint32_t x,y,w,h; };
constexpr Rect BarRect{0,0,1024,128};                 // 8:1, matches the bar quad
constexpr Rect CornerRect{0,128,128,128};                                // resize handle, bottom-right
constexpr Rect ToolsRect{128,128,128,128};                               // round tools button, left of the bar
constexpr Rect DotRect[2]{{256,128,64,64},{320,128,64,64}};        // per hand
constexpr Rect LaserRect[2]{{384,128,128,32},{512,128,128,32}};    // per hand
constexpr int FadeSteps=16;

struct Visual {
    int alpha=0;            // bar and handle, 0..FadeSteps
    int bar=0;              // 0 idle, 1 hover, 2 held
    int corner=0;           // resize handle, same levels
    bool held[2]{};         // hand is dragging: tints its laser and dot
    int tools=0;            // tools button: 0 idle, 1 hover, 2 its panel is open
    bool operator==(const Visual&) const=default;
};

namespace detail {
struct Px { float r,g,b,a; };
struct Col { float r,g,b,a; };
inline void over(Px& d,const Col& c,float coverage) {
    const float a=c.a*coverage;
    if (a<=0) return;
    const float oa=a+d.a*(1-a);
    d.r=(c.r*a+d.r*d.a*(1-a))/oa; d.g=(c.g*a+d.g*d.a*(1-a))/oa; d.b=(c.b*a+d.b*d.a*(1-a))/oa; d.a=oa;
}
inline float cover(float distance) { return std::clamp(0.5f-distance,0.0f,1.0f); }
inline float smooth(float a,float b,float x) { const float t=std::clamp((x-a)/(b-a),0.0f,1.0f); return t*t*(3-2*t); }
inline float roundRect(float x,float y,float cx,float cy,float hw,float hh,float r) {
    const float qx=std::fabs(x-cx)-(hw-r), qy=std::fabs(y-cy)-(hh-r);
    return std::hypot(std::max(qx,0.0f),std::max(qy,0.0f))+std::min(std::max(qx,qy),0.0f)-r;
}
inline float segment(float x,float y,float ax,float ay,float bx,float by) {
    const float pax=x-ax,pay=y-ay,bax=bx-ax,bay=by-ay;
    const float h=std::clamp((pax*bax+pay*bay)/(bax*bax+bay*bay),0.0f,1.0f);
    return std::hypot(pax-bax*h,pay-bay*h);
}
inline Px unpack(uint32_t d,bool bgra) {
    const uint32_t r=bgra ? (d>>16)&255 : d&255, b=bgra ? d&255 : (d>>16)&255;
    return {static_cast<float>(r),static_cast<float>((d>>8)&255),static_cast<float>(b),((d>>24)&255)/255.0f};
}
inline uint32_t pack(const Px& p,bool bgra,float alphaScale) {
    auto byte=[](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v,0.0f,255.0f))); };
    const uint32_t a=static_cast<uint32_t>(std::lround(std::clamp(p.a*alphaScale,0.0f,1.0f)*255));
    return bgra ? (byte(p.b)|byte(p.g)<<8|byte(p.r)<<16|a<<24) : (byte(p.r)|byte(p.g)<<8|byte(p.b)<<16|a<<24);
}
template<class Shade>
void fill(std::vector<uint32_t>& px,const Rect& r,bool bgra,float alphaScale,Px base,Shade shade) {
    for (uint32_t y=0;y<r.h;++y) for (uint32_t x=0;x<r.w;++x) {
        Px p=base; shade(p,x+0.5f,y+0.5f);
        px[static_cast<size_t>(r.y+y)*AtlasW+r.x+x]=pack(p,bgra,alphaScale);
    }
}
constexpr Col BarFill[3]{{26,28,36,0.80f},{42,48,66,0.94f},{46,128,255,0.96f}};
constexpr Col BarEdge[3]{{255,255,255,0.32f},{205,225,255,0.90f},{200,225,255,1.0f}};
inline void bar(Px& p,float x,float y,int state) {
    const float d=roundRect(x,y,512,64,506,58,58);
    over(p,BarFill[state],cover(d));
    over(p,BarEdge[state],cover(std::fabs(d+2.0f)-2.0f));
    // Grab marks: a centre line and a dot either side.
    const float mark=state==0 ? 0.55f : 0.95f;
    over(p,{255,255,255,mark},cover(roundRect(x,y,512,64,110,6,6)));
    for (const float dx:{-170.0f,170.0f}) over(p,{255,255,255,mark*0.8f},cover(std::hypot(x-512-dx,y-64)-7));
}
constexpr Col CornerBack[3]{{26,28,36,0.55f},{42,48,66,0.85f},{46,128,255,0.90f}};
constexpr Col CornerArm[3]{{255,255,255,0.85f},{150,205,255,1.0f},{255,255,255,1.0f}};
inline void corner(Px& p,float x,float y,int state) {
    over(p,CornerBack[state],cover(roundRect(x,y,64,64,54,54,20)));
    // Bracket whose elbow points away from the window corner.
    const float d=std::min(segment(x,y,104,28,104,104),segment(x,y,28,104,104,104))-7;
    over(p,CornerArm[state],cover(d));
}
// Round button with three sliders, the usual "settings" mark.
inline void toolsIcon(Px& p,float x,float y,int state) {
    const float d=roundRect(x,y,64,64,58,58,58);
    over(p,BarFill[state],cover(d));
    over(p,BarEdge[state],cover(std::fabs(d+2.0f)-2.0f));
    const float ink=state==0 ? 0.8f : 1.0f;
    const float lineY[3]{40,64,88},knobX[3]{80,46,70};
    for (int i=0;i<3;++i) {
        over(p,{255,255,255,ink},cover(roundRect(x,y,64,lineY[i],32,3.5f,3.5f)));
        const float r=std::hypot(x-knobX[i],y-lineY[i]);
        over(p,BarFill[state==2 ? 2 : 0],cover(r-12.5f));          // a ring that cuts the line
        over(p,{255,255,255,ink},cover(r-8.5f));
    }
}
inline void dot(Px& p,float x,float y,bool held) {
    const float r=std::hypot(x-32,y-32);
    over(p,{20,22,28,0.75f},cover(r-13));
    over(p,held ? Col{64,150,255,1.0f} : Col{255,255,255,0.98f},cover(r-10));
    over(p,{255,255,255,held ? 0.9f : 0.7f},cover(std::fabs(r-25)-2));
}
inline void laser(Px& p,float x,float y,bool held) {
    const float u=x/128, v=std::fabs(y-16)/16;
    const float across=1-smooth(0.2f,1.0f,v);                      // soft edges
    const float along=smooth(0.0f,0.35f,u)*(1-smooth(0.92f,1.0f,u)); // fades in at the controller, out at the cursor
    over(p,held ? Col{110,175,255,0.9f} : Col{205,228,255,0.85f},across*along);
}
}

// bgra=true when the runtime only offers B8G8R8A8 swapchains.
inline void render(std::vector<uint32_t>& px,const Visual& v,bool bgra) {
    using namespace detail;
    px.assign(static_cast<size_t>(AtlasW)*AtlasH,0);
    const float fade=static_cast<float>(std::clamp(v.alpha,0,FadeSteps))/FadeSteps;
    fill(px,BarRect,bgra,fade,{26,28,36,0},[&](Px& p,float x,float y) { bar(p,x,y,v.bar); });
    fill(px,CornerRect,bgra,fade,{26,28,36,0},[&](Px& p,float x,float y) { corner(p,x,y,v.corner); });
    fill(px,ToolsRect,bgra,fade,{26,28,36,0},[&](Px& p,float x,float y) { toolsIcon(p,x,y,v.tools); });
    for (int h=0;h<2;++h) {
        fill(px,DotRect[h],bgra,1,{255,255,255,0},[&](Px& p,float x,float y) { dot(p,x,y,v.held[h]); });
        fill(px,LaserRect[h],bgra,1,v.held[h] ? Px{110,175,255,0} : Px{205,228,255,0},
            [&](Px& p,float x,float y) { laser(p,x,y,v.held[h]); });
    }
}
}
