#include "common.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include "bmp_dump.hpp"
#include "picker_art.hpp"

static int failures=0;
#define CHECK(cond,msg) do { if (!(cond)) { ++failures; std::cerr<<"FAIL line "<<__LINE__<<": "<<(msg)<<'\n'; } } while (0)
static bool nearly(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }
static bool nearly(XrVector3f a,XrVector3f b,float eps=1e-3f) { return pose::length(pose::sub(a,b))<=eps; }

using Mode=GamePicker::Mode;
static XrPosef aim(XrVector3f from,XrVector3f to) {
    // facing() points +Z at a position, so aim it away from the target: -Z then looks at the target.
    return {pose::facing(from,pose::sub(from,pose::sub(to,from)),{0,0,0,1}),from};
}

// A menu with `n` entries, all startable, and a head at the origin.
struct Menu {
    GamePicker picker;
    XrPosef head{{0,0,0,1},{0,0,0}};
    std::vector<char> enabled;
    GamePicker::Hand hands[2];
    GamePicker::Output last;
    explicit Menu(size_t n=2) : enabled(n,1) { picker.place(head); picker.reset(); }
    XrVector3f at(float x,float y) const { return pose::add(picker.pose().position,pose::rotate(picker.pose().orientation,{x,y,0})); }
    void point(int h,float x,float y) { hands[h].valid=true; hands[h].aim=aim({h==0 ? -0.3f : 0.3f,-0.4f,0},at(x,y)); }
    void pointAt(int h,const picker::Box& b) { point(h,b.cx,b.cy); }
    void step(Mode mode=Mode::Choose,int frames=1) { for (int i=0;i<frames;++i) last=picker.update(hands,mode,enabled); }
    // Hover, release, press: how every click starts. Returns the frame of the press.
    GamePicker::Output click(int h,const picker::Box& b,Mode mode=Mode::Choose) {
        hands[h].click=false; pointAt(h,b); step(mode,2);
        hands[h].click=true; step(mode); const auto pressed=last;
        hands[h].click=false; step(mode); return pressed;
    }
};
static PickerVisual makeVisual(size_t n) {
    PickerVisual v;
    for (size_t i=0;i<n;++i) v.entries.push_back({L"Game number "+std::to_wstring(i+1),L"Subtitle "+std::to_wstring(i+1),true});
    v.hint=L"Point at a game and pull the trigger";
    return v;
}

static uint32_t at(const std::vector<uint32_t>& px,int x,int y) { return px[static_cast<size_t>(y)*picker::TexW+x]; }
static int alphaOf(uint32_t p) { return (p>>24)&255; }
static int redOf(uint32_t p) { return p&255; }
static int blueOf(uint32_t p) { return (p>>16)&255; }
// Pixels in a rectangle that are bright and opaque: white text on a coloured tile.
static int brightIn(const std::vector<uint32_t>& px,int x0,int y0,int x1,int y1) {
    int n=0;
    for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) { const auto p=at(px,x,y); n+=redOf(p)>200 && blueOf(p)>200 && alphaOf(p)>200; }
    return n;
}

static void dump(const char* path) {
    std::vector<uint32_t> sheet,one;
    std::vector<PickerVisual> states;
    auto two=makeVisual(2); two.entries[0]={L"Ori and the Blind Forest",L"Definitive Edition",true}; two.entries[1]={L"Ori and the Will of the Wisps",L"",true};
    two.hint=L"Point at a game and pull the trigger or squeeze the grip";
    states.push_back(two);
    auto grid=makeVisual(8); const wchar_t* names[]{L"Ori and the Blind Forest",L"Ori and the Will of the Wisps",L"Some Longer Game Title Here",L"Hollow Knight",L"Hades",L"Cuphead",L"Celeste",L"Dead Cells"};
    for (size_t i=0;i<8;++i) { grid.entries[i].title=names[i]; grid.entries[i].caption=i==0 ? L"Definitive Edition" : L""; }
    grid.hover=1; states.push_back(grid);
    grid.page=1; grid.hover=-1; grid.nextHover=false; grid.prevHover=true; states.push_back(grid);
    auto some=makeVisual(3); some.entries[2].installed=false; states.push_back(some);
    PickerVisual msg=two; msg.mode=Mode::Message; msg.message=L"Starting Ori and the Will of the Wisps..."; msg.hint=L"This can take a moment."; msg.backHover=true; states.push_back(msg);
    for (const auto& v : states) { picker::render(one,v,false); sheet.insert(sheet.end(),one.begin(),one.end()); }
    writeBmp(path,sheet,picker::TexW,picker::TexH*static_cast<uint32_t>(states.size()));
    std::cout<<"Wrote "<<path<<'\n';
}

int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--dump-menu") { dump(argv[2]); return 0; }

    // --- Layout ----------------------------------------------------------------------------------------------------
    {
        CHECK(!picker::grid(0) && !picker::grid(1) && !picker::grid(2) && picker::grid(3) && picker::grid(40),"one or two games get large tiles, more get a grid");
        CHECK(picker::pageCount(0)==1 && picker::pageCount(6)==1 && picker::pageCount(7)==2 && picker::pageCount(12)==2 && picker::pageCount(13)==3,"six games to a page");
        const auto t0=picker::tile(0,2),t1=picker::tile(1,2);
        CHECK(nearly(t0.cx,-t1.cx,1e-5f) && t0.cx<0 && t0.hw*2<=picker::PanelW/2,"two tiles, side by side, centred, inside the panel");
        CHECK(nearly(picker::tile(0,1).cx,0) && nearly(picker::tile(0,1).cy,0),"a single game is centred");
        auto inPanel=[](const picker::Box& b) { return b.cx-b.hw>=-picker::PanelW/2 && b.cx+b.hw<=picker::PanelW/2 && b.cy-b.hh>=-picker::PanelH/2 && b.cy+b.hh<=picker::PanelH/2; };
        auto overlap=[](const picker::Box& a,const picker::Box& b) { return std::fabs(a.cx-b.cx)<a.hw+b.hw && std::fabs(a.cy-b.cy)<a.hh+b.hh; };
        for (size_t total : {3u,4u,5u,6u,7u,8u,12u,13u,17u}) {
            for (size_t page=0;page<picker::pageCount(total);++page) {
                std::vector<picker::Box> shown;
                for (size_t i=page*picker::PerPage;i<std::min(total,(page+1)*picker::PerPage);++i) shown.push_back(picker::tile(i,total));
                bool ok=true;
                for (size_t a=0;a<shown.size();++a) { ok&=inPanel(shown[a]); for (size_t b=a+1;b<shown.size();++b) ok&=!overlap(shown[a],shown[b]); }
                CHECK(ok,"tiles on a page stay inside the panel and never overlap");
                for (const auto& b : shown) CHECK(!overlap(b,picker::prevButton()) && !overlap(b,picker::nextButton()) && b.cy-b.hh>-0.62f && b.cy+b.hh<0.62f,"tiles keep clear of the page buttons and the title");
            }
        }
        CHECK(!overlap(picker::prevButton(),picker::nextButton()) && inPanel(picker::prevButton()) && inPanel(picker::nextButton()),"page buttons are on the panel and apart");
        // Rows are centred: a page of four is three and one; a page of two (the last of eight) is a centred pair.
        CHECK(nearly(picker::tile(3,4).cx,0) && picker::tile(3,4).cy<picker::tile(0,4).cy,"a short last row is centred, below the first");
        CHECK(nearly(picker::tile(6,8).cx,-picker::tile(7,8).cx,1e-5f) && nearly(picker::tile(6,8).cy,0),"the last page of eight is a centred pair on one row");
        CHECK(nearly(picker::tile(0,8).cy,picker::tile(2,8).cy) && picker::tile(2,8).cx>picker::tile(1,8).cx && picker::tile(1,8).cx>picker::tile(0,8).cx,"a full page reads left to right along each row");
        CHECK(nearly(picker::tile(0,13).cx,picker::tile(6,13).cx) && nearly(picker::tile(0,13).cy,picker::tile(6,13).cy),"every page starts in the same place");
        CHECK(nearly(picker::tile(12,13).cx,0) && nearly(picker::tile(12,13).cy,0),"a last page of one is centred");
    }
    // The shipped placement is unchanged for the games that were there from the start.
    {
        const auto t0=picker::tile(0,2),t1=picker::tile(1,2);
        CHECK(nearly(t0.cx,-0.85f,1e-4f) && nearly(t1.cx,0.85f,1e-4f) && nearly(t0.hw,0.75f) && nearly(t0.hh,0.5f),"two games look exactly as before");
    }

    // --- Placement -------------------------------------------------------------------------------------------------
    {
        GamePicker p;
        CHECK(!p.placed(),"not placed until asked");
        const XrPosef head{{0,std::sin(0.7853982f),0,std::cos(0.7853982f)},{1,1.6f,2}};   // turned 90 degrees to the left
        p.place(head);
        CHECK(p.placed() && nearly(p.pose().position,{1-picker::Distance,1.6f-picker::Drop,2},1e-3f),"menu appears in front of the head, below eye level");
        CHECK(nearly(pose::rotate(p.pose().orientation,{0,0,1}),{1,0,0},1e-3f),"and faces back at you");
        p.invalidate(); CHECK(!p.placed(),"invalidate asks for re-placement");
    }
    // --- Hover and click -------------------------------------------------------------------------------------------
    {
        Menu m; const auto t0=picker::tile(0,2),t1=picker::tile(1,2);
        m.point(1,t0.cx,0); m.step();
        CHECK(m.last.hover==0 && m.last.pointer[1] && !m.last.pointer[0],"laser on the first tile hovers it and draws its beam");
        CHECK(nearly(m.last.rayEnd[1],m.at(t0.cx,0),1e-3f),"the beam ends where the laser meets the panel");
        m.point(1,t1.cx,0.2f); m.step();
        CHECK(m.last.hover==1,"second tile");
        m.point(1,0,0); m.step();
        CHECK(m.last.hover==-1 && m.last.pointer[1],"the gap between tiles hovers nothing but the laser still shows");
        m.point(1,6,0); m.step();
        CHECK(!m.last.pointer[1],"no beam when the laser misses the panel");
        // Click: an edge on a tile chooses it, once.
        m.point(1,t1.cx,0); m.step(Mode::Choose,2);
        m.hands[1].click=true; m.step();
        CHECK(m.last.chosen==1 && m.last.pressed[1],"pulling the trigger on a tile chooses it");
        m.step(); CHECK(m.last.chosen==-1,"holding the trigger does not choose again");
        m.hands[1].click=false; m.step();
        // A held trigger swept onto a tile does not choose; it needs a fresh press.
        m.hands[1].click=true; m.point(1,0,0); m.step(Mode::Choose,3);
        m.point(1,t0.cx,0); m.step(Mode::Choose,3);
        CHECK(m.last.chosen==-1,"sweeping onto a tile with a held trigger does not choose it");
        m.hands[1].click=false; m.step(); m.hands[1].click=true; m.step();
        CHECK(m.last.chosen==0,"a fresh press does");
        // A tile that is not installed cannot be chosen, and does not highlight.
        Menu d; d.enabled[1]=0; d.point(0,t1.cx,0); d.step(Mode::Choose,2); d.hands[0].click=true; d.step();
        CHECK(d.last.hover==-1 && d.last.chosen==-1 && !d.last.pressed[0],"a game that is not installed cannot be chosen");
        // Grabbing with either hand works; an untracked hand is ignored.
        Menu e; e.point(0,t0.cx,0); e.step(Mode::Choose,2); e.hands[0].click=true; e.step();
        CHECK(e.last.chosen==0,"the left hand can choose too");
        Menu f; f.hands[1].click=true; f.hands[1].valid=false; f.step(Mode::Choose,3);
        CHECK(f.last.chosen==-1 && !f.last.pointer[1],"an untracked hand does nothing");
        // reset() (focus loss) needs a release first.
        Menu r; r.point(1,t0.cx,0); r.hands[1].click=true; r.step(Mode::Choose,2); r.picker.reset(); r.step();
        CHECK(r.last.chosen==-1,"reset ignores the button that is already down");
        // No games at all: nothing to hover or choose, and no page buttons.
        Menu none(0); none.point(1,0,0); none.hands[1].click=false; none.step(Mode::Choose,2); none.hands[1].click=true; none.step();
        CHECK(none.last.hover==-1 && none.last.chosen==-1 && !none.last.paged && none.last.pointer[1],"an empty menu is inert");
    }
    // --- Pages -----------------------------------------------------------------------------------------------------
    {
        Menu m(8);
        CHECK(m.picker.page()==0,"starts on the first page");
        // Entries on the first page can be hovered and chosen by their number; entries on other pages cannot.
        m.pointAt(1,picker::tile(4,8)); m.step();
        CHECK(m.last.hover==4,"the fifth tile is the fifth game");
        m.pointAt(1,picker::tile(6,8)); m.step();
        CHECK(m.last.hover==-1,"the seventh game is on the next page, not under the first page's empty middle");
        const auto choose=m.click(1,picker::tile(5,8));
        CHECK(choose.chosen==5 && !choose.paged,"choosing on the first page");
        // Previous is unavailable on the first page; Next works.
        m.hands[1].click=false; m.pointAt(1,picker::prevButton()); m.step(Mode::Choose,2);
        CHECK(!m.last.prevHover,"no Previous on the first page");
        auto first=m.click(1,picker::prevButton()); CHECK(!first.paged && m.picker.page()==0,"clicking where Previous would be does nothing");
        m.pointAt(1,picker::nextButton()); m.step(Mode::Choose,2);
        CHECK(m.last.nextHover && m.last.pointer[1] && !m.last.prevHover,"Next hovers on the first page");
        auto next=m.click(1,picker::nextButton());
        CHECK(next.paged && next.pressed[1] && m.picker.page()==1 && next.chosen==-1,"Next turns the page");
        // The second page holds games 6 and 7, centred; they are chosen by their own numbers.
        m.pointAt(1,picker::tile(0,8)); m.step(Mode::Choose,2);
        CHECK(m.last.hover==-1,"the first page's tiles are gone");
        const auto second=m.click(1,picker::tile(7,8));
        CHECK(second.chosen==7 && m.picker.page()==1,"the last game is chosen from the last page");
        m.hands[1].click=false; m.pointAt(1,picker::nextButton()); m.step(Mode::Choose,2);
        CHECK(!m.last.nextHover,"no Next on the last page");
        CHECK(!m.click(1,picker::nextButton()).paged && m.picker.page()==1,"clicking where Next would be does nothing");
        m.pointAt(1,picker::prevButton()); m.step(Mode::Choose,2);
        CHECK(m.last.prevHover,"Previous hovers on the last page");
        CHECK(m.click(1,picker::prevButton()).paged && m.picker.page()==0,"Previous turns back");
        // A held trigger swept onto Next does not turn the page; a fresh press does.
        m.point(1,0,0.9f); m.hands[1].click=true; m.step(Mode::Choose,3);
        m.pointAt(1,picker::nextButton()); m.step(Mode::Choose,3);
        CHECK(m.picker.page()==0 && !m.last.paged,"a held trigger swept onto Next does not page");
        m.hands[1].click=false; m.step(); m.hands[1].click=true; m.step();
        CHECK(m.picker.page()==1 && m.last.paged,"a fresh press does");
        m.hands[1].click=false; m.step();
        // Only one page turn per frame, even with both hands on the buttons.
        Menu both(13); both.pointAt(0,picker::nextButton()); both.pointAt(1,picker::nextButton()); both.step(Mode::Choose,2);
        both.hands[0].click=true; both.hands[1].click=true; both.step();
        CHECK(both.picker.page()==1,"two hands pressing Next in one frame turn one page");
        // The page buttons exist only in the choosing view.
        Menu card(8); card.pointAt(1,picker::nextButton()); card.step(Mode::Message,2); card.hands[1].click=true; card.step(Mode::Message);
        CHECK(card.picker.page()==0 && !card.last.nextHover && !card.last.paged,"no page buttons on the status cards");
        // Disabled games in a grid cannot be chosen, and fewer games bring the page back into range.
        Menu dis(8); dis.enabled[2]=0; dis.pointAt(1,picker::tile(2,8)); dis.step(Mode::Choose,2); dis.hands[1].click=true; dis.step();
        CHECK(dis.last.hover==-1 && dis.last.chosen==-1,"a greyed-out tile in the grid cannot be chosen");
        Menu shrink(8); shrink.click(1,picker::nextButton()); CHECK(shrink.picker.page()==1,"on page two");
        shrink.enabled.resize(3); shrink.step(); CHECK(shrink.picker.page()==0,"when the list gets shorter the page comes back into range");
        shrink.enabled.resize(8,1); shrink.picker.firstPage(); CHECK(shrink.picker.page()==0,"firstPage goes home");
    }
    // --- The status cards ------------------------------------------------------------------------------------------
    {
        Menu m; const auto back=picker::backButton();
        m.point(1,back.cx,back.cy); m.step(Mode::Message,2);
        CHECK(m.last.backHover && m.last.hover==-1,"the back button hovers in the starting card");
        m.hands[1].click=true; m.step(Mode::Message);
        CHECK(m.last.back && m.last.chosen==-1,"clicking it goes back");
        Menu s; s.point(1,back.cx,back.cy); s.step(Mode::Status,2); s.hands[1].click=true; s.step(Mode::Status);
        CHECK(!s.last.backHover && !s.last.back,"the waiting card has no button");
        Menu t; t.point(1,picker::tile(0,2).cx,0); t.step(Mode::Message,2); t.hands[1].click=true; t.step(Mode::Message);
        CHECK(t.last.chosen==-1 && t.last.hover==-1,"tiles are not clickable while a game is starting");
    }
    // --- What the compositor is given ------------------------------------------------------------------------------
    {
        Menu m; m.point(1,picker::tile(0,2).cx,0); m.step();
        const auto quads=buildPickerQuads(m.picker,m.last,m.head);
        CHECK(quads.size()==3,"panel + beam + cursor");
        CHECK(quads[0].sheet==1 && quads[0].rect.w==picker::TexW && quads[0].rect.h==picker::TexH && nearly(quads[0].size.width,picker::PanelW) && nearly(quads[0].size.height,picker::PanelH),"the panel uses its own full texture");
        CHECK(nearly(quads[0].pose.position,m.picker.pose().position,1e-5f),"panel is where it was placed");
        CHECK(quads[1].sheet==0 && quads[2].sheet==0,"beam and cursor come from the shared atlas");
        CHECK(nearly(quads[2].pose.position,pose::add(m.last.rayEnd[1],pose::scale(pose::rotate(m.picker.pose().orientation,{0,0,1}),0.004f)),1e-5f),"cursor sits on the panel");
        Menu none; none.step();
        CHECK(buildPickerQuads(none.picker,none.last,none.head).size()==1,"no lasers: just the panel");
    }

    // --- Art -------------------------------------------------------------------------------------------------------
    {
        std::vector<uint32_t> px;
        PickerVisual v=makeVisual(2);
        picker::render(px,v,false);
        CHECK(px.size()==static_cast<size_t>(picker::TexW)*picker::TexH,"texture size");
        CHECK(alphaOf(at(px,1024,512))>200 && alphaOf(at(px,1,1))==0,"opaque panel with transparent rounded corners");
        const auto b0=picker::tile(0,2);
        const int x0=static_cast<int>(picker::px(b0.cx-b0.hw)), y0=static_cast<int>(picker::py(b0.cy+b0.hh)), x1=static_cast<int>(picker::px(b0.cx+b0.hw));
        // The game's title is drawn in white on its tile.
        CHECK(brightIn(px,x0+40,y0+110,x1-40,y0+340)>300,"the tile shows the game's title");
        // Hover brightens the tile; not installed greys it out.
        const auto fillAt=[&](const std::vector<uint32_t>& pixels) { return at(pixels,x0+30,(y0+static_cast<int>(picker::py(b0.cy-b0.hh)))/2); };
        std::vector<uint32_t> hovered,missing; PickerVisual h=v; h.hover=0; picker::render(hovered,h,false);
        PickerVisual m=v; m.entries[0].installed=false; picker::render(missing,m,false);
        CHECK(redOf(fillAt(hovered))>redOf(fillAt(px)),"a hovered tile is brighter");
        CHECK(blueOf(fillAt(missing))<blueOf(fillAt(px)),"a tile for a game that is not installed is greyed out");
        // Message card: the back button is only in the starting card.
        PickerVisual card=v; card.mode=Mode::Message; card.message=L"Starting..."; std::vector<uint32_t> msg,status;
        picker::render(msg,card,false); card.mode=Mode::Status; picker::render(status,card,false);
        const auto back=picker::backButton(); const int bx=static_cast<int>(picker::px(back.cx)), by=static_cast<int>(picker::py(back.cy));
        CHECK(redOf(at(msg,bx-300,by))>redOf(at(status,bx-300,by)),"the starting card draws the back button");
        int differing=0; for (int y=by-30;y<by+30;++y) for (int x=bx-300;x<bx+300;++x) differing+=at(msg,x,y)!=at(status,x,y);
        CHECK(differing>2000,"the waiting card does not");
        // BGRA swaps red and blue only. The first tile is blue (R != B), so it shows whether they were swapped.
        std::vector<uint32_t> bgra; picker::render(bgra,v,true);
        const auto rgbaPixel=fillAt(px), bgraPixel=fillAt(bgra);
        CHECK(alphaOf(rgbaPixel)==alphaOf(bgraPixel) && redOf(rgbaPixel)==blueOf(bgraPixel) && blueOf(rgbaPixel)==redOf(bgraPixel) && redOf(rgbaPixel)!=blueOf(rgbaPixel),"BGRA output swaps red and blue");
        CHECK(PickerVisual{}==PickerVisual{} && !(v==h),"visual states compare");
        PickerVisual other=v; other.entries[1].title=L"Another"; CHECK(!(v==other),"a changed title is a changed look");
        PickerVisual paged=v; paged.page=1; CHECK(!(v==paged),"a changed page is a changed look");
    }
    // The grid: titles on every tile of the page, the page's own tiles only, and the page controls.
    {
        PickerVisual g=makeVisual(8);
        std::vector<uint32_t> p0,p1,hovered,missing;
        picker::render(p0,g,false);
        auto tileBox=[&](size_t i,int& x0,int& y0,int& x1,int& y1) {
            const auto b=picker::tile(i,8);
            x0=static_cast<int>(picker::px(b.cx-b.hw)); x1=static_cast<int>(picker::px(b.cx+b.hw)); y0=static_cast<int>(picker::py(b.cy+b.hh)); y1=static_cast<int>(picker::py(b.cy-b.hh));
        };
        for (size_t i=0;i<6;++i) { int x0,y0,x1,y1; tileBox(i,x0,y0,x1,y1); CHECK(brightIn(p0,x0+20,y0+24,x1-20,y0+200)>150,"every tile on the first page shows its title"); }
        // Tiles 6 and 7 are on page two, so page one does not draw them (their spot is the empty panel).
        { int x0,y0,x1,y1; tileBox(6,x0,y0,x1,y1); CHECK(alphaOf(at(p0,(x0+x1)/2,(y0+y1)/2))>0 && brightIn(p0,x0+20,y0+24,x1-20,y0+200)<20,"the second page's tiles are not drawn on the first"); }
        g.page=1; picker::render(p1,g,false);
        { int x0,y0,x1,y1; tileBox(6,x0,y0,x1,y1); CHECK(brightIn(p1,x0+20,y0+24,x1-20,y0+200)>150,"the second page draws its own tiles, where tile 6 sits"); }
        { int x0,y0,x1,y1; tileBox(0,x0,y0,x1,y1); CHECK(brightIn(p1,x0+20,y0+24,x1-20,y0+200)<20,"and not the first page's"); }
        // Page label: only when there is more than one page.
        const int lx0=picker::TexW-90-480,ly0=44;
        int labelInk=0; for (int y=ly0;y<ly0+110;++y) for (int x=lx0;x<lx0+480;++x) labelInk+=alphaOf(at(p0,x,y))>0 && (redOf(at(p0,x,y))>150);
        PickerVisual single=makeVisual(4); std::vector<uint32_t> sp; picker::render(sp,single,false);
        int singleInk=0; for (int y=ly0;y<ly0+110;++y) for (int x=lx0;x<lx0+480;++x) singleInk+=alphaOf(at(sp,x,y))>0 && (redOf(at(sp,x,y))>150);
        CHECK(labelInk>200 && singleInk<20,"the page label appears only with more than one page");
        // Page buttons: Next is live on the first page and Previous is dimmed; the other way round on the last.
        auto liveness=[&](const std::vector<uint32_t>& img,const picker::Box& b) {
            int ink=0; const int x0=static_cast<int>(picker::px(b.cx-b.hw)),x1=static_cast<int>(picker::px(b.cx+b.hw)),y0=static_cast<int>(picker::py(b.cy+b.hh)),y1=static_cast<int>(picker::py(b.cy-b.hh));
            for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) ink+=redOf(at(img,x,y))>200 && blueOf(at(img,x,y))>200;
            return ink;
        };
        CHECK(liveness(p0,picker::nextButton())>150 && liveness(p0,picker::prevButton())<40,"first page: Next is live, Previous is dimmed");
        CHECK(liveness(p1,picker::prevButton())>150 && liveness(p1,picker::nextButton())<40,"last page: Previous is live, Next is dimmed");
        CHECK(liveness(sp,picker::nextButton())<40 && liveness(sp,picker::prevButton())<40,"a single page has no page buttons");
        // Hover brightens a grid tile; not installed greys it out.
        PickerVisual hov=g; hov.page=0; hov.hover=1; picker::render(hovered,hov,false);
        PickerVisual mis=g; mis.page=0; mis.entries[1].installed=false; picker::render(missing,mis,false);
        { int x0,y0,x1,y1; tileBox(1,x0,y0,x1,y1); const int fx=x0+12,fy=(y0+y1)/2;
          CHECK(redOf(at(hovered,fx,fy))>redOf(at(p0,fx,fy)),"a hovered grid tile is brighter");
          CHECK(blueOf(at(missing,fx,fy))<blueOf(at(p0,fx,fy)),"a grid tile for a game that is not installed is greyed out");
          PickerVisual firstPage=g; firstPage.page=0; std::vector<uint32_t> bg; picker::render(bg,firstPage,true);
          CHECK(redOf(at(p0,fx,fy))==blueOf(at(bg,fx,fy)) && blueOf(at(p0,fx,fy))==redOf(at(bg,fx,fy)),"BGRA swaps red and blue in the grid too"); }
        PickerVisual ph=g; ph.prevHover=true; ph.page=1; std::vector<uint32_t> prevHot; picker::render(prevHot,ph,false); PickerVisual pn=g; pn.page=1; std::vector<uint32_t> prevCold; picker::render(prevCold,pn,false);
        { const auto b=picker::prevButton(); const int fx=static_cast<int>(picker::px(b.cx-b.hw))+14,fy=static_cast<int>(picker::py(b.cy));
          CHECK(prevHot[static_cast<size_t>(fy)*picker::TexW+fx]!=prevCold[static_cast<size_t>(fy)*picker::TexW+fx],"a hovered page button changes colour"); }
        // A page index beyond the list does not break the picture.
        PickerVisual beyond=g; beyond.page=99; std::vector<uint32_t> bp; picker::render(bp,beyond,false);
        CHECK(bp==p1,"a page past the end shows the last page");
        PickerVisual empty; std::vector<uint32_t> ep; picker::render(ep,empty,false);
        CHECK(ep.size()==static_cast<size_t>(picker::TexW)*picker::TexH && alphaOf(at(ep,1024,512))>200,"an empty list still draws a panel");
    }
    // Text drawing is safe at the edges and with nothing to draw.
    {
        std::vector<uint32_t> px(64*64,0);
        ui::drawText(px,64,false,0,0,64,64,L"",20,400,{255,255,255,1},DT_CENTER);
        CHECK(std::all_of(px.begin(),px.end(),[](uint32_t p) { return p==0; }),"empty text draws nothing");
        ui::drawText(px,64,false,40,40,64,64,L"Edge",30,600,{255,255,255,1},DT_LEFT|DT_SINGLELINE);
        ui::drawText(px,64,false,-20,-20,64,64,L"Edge",30,600,{255,255,255,1},DT_LEFT|DT_SINGLELINE);
        CHECK(std::any_of(px.begin(),px.end(),[](uint32_t p) { return (p>>24)!=0; }),"text partly off the buffer is clipped, not dropped");
    }

    if (failures) { std::cerr<<failures<<" game-menu check(s) failed\n"; return 1; }
    std::cout<<"PASS game menu: layout and pages, placement, hover, click edge, disabled tiles, status cards, compositor quads, artwork\n";
    return 0;
}
