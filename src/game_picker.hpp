#pragma once
#include "games.hpp"
#include "window_control.hpp"

// The in-headset game menu: a panel with one tile per game, drawn in front of you. Point a laser at a tile and
// pull the trigger (or squeeze the grip) to launch that game. With one or two games the tiles are large; with more
// they form a grid of six per page with Previous and Next buttons. Pure maths, so it is unit-tested without a headset.
namespace picker {
constexpr float PanelW=3.6f, PanelH=1.8f, Distance=2.6f, Drop=0.1f;     // metres; the panel sits a little below eye level
constexpr uint32_t TexW=2048, TexH=1024;
constexpr float PxPerMeter=TexW/PanelW;                                  // 568.9, the same both ways
constexpr float TileW=1.5f, TileH=1.0f, TileGap=0.2f;                    // large tiles: one or two games
constexpr float GridW=1.05f, GridH=0.55f, GridGap=0.1f, GridRowY=0.325f; // grid tiles: three columns, two rows
constexpr size_t Columns=3, PerPage=6;
struct Box { float cx,cy,hw,hh; };                                       // panel space: X right, Y up, origin at the centre
inline bool grid(size_t total) { return total>2; }
inline size_t pageCount(size_t total) { return std::max<size_t>(1,(total+PerPage-1)/PerPage); }
// Where entry `index` of `total` sits while its page is showing. Rows are centred, so a short last row is not lopsided.
inline Box tile(size_t index,size_t total) {
    if (!grid(total)) {
        const float width=static_cast<float>(total)*TileW+static_cast<float>(total-1)*TileGap;
        return {-width/2+TileW/2+static_cast<float>(index)*(TileW+TileGap),0.0f,TileW/2,TileH/2};
    }
    const size_t page=index/PerPage,slot=index%PerPage,shown=std::min(PerPage,total-page*PerPage);
    const size_t row=slot/Columns,col=slot%Columns,inRow=std::min(Columns,shown-row*Columns),rows=(shown+Columns-1)/Columns;
    const float width=static_cast<float>(inRow)*GridW+static_cast<float>(inRow-1)*GridGap;
    return {-width/2+GridW/2+static_cast<float>(col)*(GridW+GridGap),rows==1 ? 0.0f : (row==0 ? GridRowY : -GridRowY),GridW/2,GridH/2};
}
inline Box backButton() { return {0.0f,-0.72f,0.75f,0.13f}; }
inline Box prevButton() { return {-1.45f,-0.74f,0.3f,0.12f}; }          // clear of the bottom row of tiles (their edge is at -0.6)
inline Box nextButton() { return {1.45f,-0.74f,0.3f,0.12f}; }
inline float px(float x) { return (x+PanelW/2)*PxPerMeter; }             // panel metres to texture pixels
inline float py(float y) { return (PanelH/2-y)*PxPerMeter; }
inline bool inside(const Box& b,float x,float y,float pad=0) { return std::fabs(x-b.cx)<=b.hw+pad && std::fabs(y-b.cy)<=b.hh+pad; }
}

class GamePicker {
public:
    using Hand=WindowControl::Hand;
    enum class Mode { Choose, Message, Status };   // Message: status card with a way back; Status: status card only
    struct Output {
        int hover=-1;                         // enabled tile under a laser (entry number)
        bool backHover=false,prevHover=false,nextHover=false;
        int chosen=-1;                        // a click landed on this tile this frame
        bool back=false;                      // a click landed on the back button this frame
        bool paged=false;                     // a click on Previous or Next changed the page this frame
        bool pointer[2]{},pressed[2]{};       // draw this hand's laser and cursor / it is clicking
        XrVector3f rayStart[2]{},rayEnd[2]{};
    };
    bool placed() const { return placed_; }
    const XrPosef& pose() const { return pose_; }
    // In front of the head at its current heading, so the menu is wherever you are looking when it appears.
    void place(const XrPosef& head) {
        const auto yaw=pose::yawRotation(head.orientation);
        pose_={yaw,pose::add(head.position,pose::rotate(yaw,{0,-picker::Drop,-picker::Distance}))};
        placed_=true;
    }
    void invalidate() { placed_=false; }
    // Buttons held right now must be released before they can click.
    void reset() { prevClick_[0]=prevClick_[1]=true; }
    size_t page() const { return page_; }
    void firstPage() { page_=0; }

    // `enabled` has one flag per entry: false for a game that cannot be started (not installed).
    Output update(const Hand (&hands)[2],Mode mode,const std::vector<char>& enabled) {
        Output out;
        const size_t total=enabled.size(),pages=picker::pageCount(total);
        if (page_>=pages) page_=pages-1;
        const bool grid=picker::grid(total);
        const size_t first=grid ? page_*picker::PerPage : 0,last=grid ? std::min(total,first+picker::PerPage) : total;
        const bool paging=mode==Mode::Choose && pages>1;
        for (int i=0;i<2;++i) {
            const bool click=hands[i].valid && hands[i].click;
            if (hands[i].valid && placed_) {
                const auto hit=hitPlane(hands[i].aim,pose_);
                if (hit.ok && std::fabs(hit.x)<=picker::PanelW/2+0.1f && std::fabs(hit.y)<=picker::PanelH/2+0.1f) {
                    int hover=-1; bool back=false,prev=false,next=false;
                    if (mode==Mode::Choose) {
                        for (size_t g=first;g<last;++g) if (enabled[g] && picker::inside(picker::tile(g,total),hit.x,hit.y,0.03f)) hover=static_cast<int>(g);
                        if (paging) { prev=page_>0 && picker::inside(picker::prevButton(),hit.x,hit.y,0.03f); next=page_+1<pages && picker::inside(picker::nextButton(),hit.x,hit.y,0.03f); }
                    } else if (mode==Mode::Message) back=picker::inside(picker::backButton(),hit.x,hit.y,0.03f);
                    const auto dir=pose::rotate(hands[i].aim.orientation,{0,0,-1});
                    out.pointer[i]=true; out.pressed[i]=click && (hover>=0 || back || prev || next);
                    out.rayStart[i]=pose::add(hands[i].aim.position,pose::scale(dir,0.03f)); out.rayEnd[i]=hit.world;
                    if (hover>=0 && out.hover<0) out.hover=hover;
                    out.backHover|=back; out.prevHover|=prev; out.nextHover|=next;
                    if (click && !prevClick_[i]) {
                        if (hover>=0 && out.chosen<0) out.chosen=hover;
                        if (back) out.back=true;
                        if (prev && !out.paged) { --page_; out.paged=true; }
                        else if (next && !out.paged) { ++page_; out.paged=true; }
                    }
                }
            }
            prevClick_[i]=click;
        }
        return out;
    }
private:
    XrPosef pose_{{0,0,0,1},{0,0,-picker::Distance}};
    bool placed_=false;
    bool prevClick_[2]{};
    size_t page_=0;
};

// The panel (its own texture) with the laser and cursor on top (from the shared atlas).
inline std::vector<UiQuad> buildPickerQuads(const GamePicker& picker,const GamePicker::Output& o,const XrPosef& head) {
    std::vector<UiQuad> q;
    q.push_back({picker.pose(),{picker::PanelW,picker::PanelH},{0,0,picker::TexW,picker::TexH},1});
    for (int i=0;i<2;++i) if (o.pointer[i]) appendPointerQuads(q,i,o.rayStart[i],o.rayEnd[i],picker.pose().orientation,head);
    return q;
}
