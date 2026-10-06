#include "common.hpp"
#include "bmp_dump.hpp"
#include "config.hpp"
#include "keys.hpp"
#include "screen_fx.hpp"
#include "screen_layer.hpp"
#include "tools_art.hpp"
#include <cstdio>

static int failures=0;
#define CHECK(cond,msg) do { if (!(cond)) { ++failures; std::cerr<<"FAIL line "<<__LINE__<<": "<<(msg)<<'\n'; } } while (0)
static bool nearly(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }
static bool nearly(XrVector3f a,XrVector3f b,float eps=1e-3f) { return pose::length(pose::sub(a,b))<=eps; }

using Hand=WindowControl::Hand;
using Clock=KeyPresser::Clock;
static std::pair<WORD,bool> keyEvent(WORD vk,bool down) { return {vk,down}; }

// A seated user with the panel opened in front of them; hands lower and to the sides, as in the window tests.
struct PanelRig {
    ToolsPanel panel;
    XrPosef head{{0,0,0,1},{0,0,0}};
    Hand hands[2];
    ToolsPanel::Output last;
    XrVector3f handOrigin[2]{{-0.3f,-0.4f,0},{0.3f,-0.4f,0}};
    size_t items;
    explicit PanelRig(size_t count=11) : items(count) { panel.show(head); }
    static XrPosef aim(XrVector3f from,XrVector3f to) { return {pose::facing(from,pose::sub(from,pose::sub(to,from)),{0,0,0,1}),from}; }
    // A point on the panel at texture pixel (px,py).
    XrVector3f at(float px,float py) const {
        const float x=(px-tools::TexW/2.0f)/tools::PxPerMeter,y=(tools::TexH/2.0f-py)/tools::PxPerMeter;
        return pose::add(panel.pose().position,pose::rotate(panel.pose().orientation,{x,y,0}));
    }
    XrVector3f cellCentre(size_t i) const { const auto r=tools::cell(i); return at(r.x+r.w/2,r.y+r.h/2); }
    void point(int h,XrVector3f target) { hands[h].valid=true; hands[h].aim=aim(handOrigin[h],target); }
    void step(int frames=1) { for (int i=0;i<frames;++i) last=panel.update(hands,items); }
};

int main(int argc,char** argv) {
    const bool dumping=argc==4 && std::string(argv[1])=="--dump";   // otherwise the first argument is the source folder
    const std::filesystem::path root=argc>=2 && !dumping ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
    require(loadGames(root),"the shipped games.catalog.ini must load");
    if (dumping) {
        // Renders the tools panel and the glow so they can be looked at: --dump tools.bmp glow.bmp
        ToolsVisual v; v.game=1; v.hover=2; v.swapEyes=true; v.curve=2; v.glow=true; v.floatWindow=1; v.status=L"Convergence (F1) sent to the game.";
        std::vector<uint32_t> px; tools::render(px,v,false); writeBmp(argv[2],px,tools::TexW,tools::TexH);
        fx::GlowState g; std::vector<uint8_t> cells(16*9*4); for (int y=0;y<9;++y) for (int x=0;x<16;++x) {
            uint8_t* c=&cells[(static_cast<size_t>(y)*16+x)*4]; c[0]=static_cast<uint8_t>(x*16); c[1]=static_cast<uint8_t>(60+y*10); c[2]=static_cast<uint8_t>(255-x*16); c[3]=255; }
        g.update(cells.data(),16*4,16,9,false,1);
        std::vector<uint32_t> glow; fx::renderGlow(glow,g,0.8f,16.0f/9,false);
        for (auto& p:glow) { const uint32_t a=p>>24; if (a==0) { p=0xff000000; continue; }   // show over black, like the room
            const uint32_t r=((p&255)*a)/255,gr=(((p>>8)&255)*a)/255,b=(((p>>16)&255)*a)/255; p=0xff000000|(b<<16)|(gr<<8)|r; }
        writeBmp(argv[3],glow,fx::GlowTex,fx::GlowTex);
        std::cout<<"Wrote "<<argv[2]<<" and "<<argv[3]<<'\n'; return 0;
    }

    // --- The games' shortcut tables and the panel's button list ---------------------------------------------------
    for (size_t g=0;g<Games.size();++g) {
        const auto& game=Games[g];
        CHECK(game.keys.size()<=MaxGameKeys,"no game lists more than six shortcuts");
        for (size_t k=0;k<game.keys.size();++k) {
            CHECK(!game.keys[k].label.empty() && KeyPresser::allowed(game.keys[k].vk),"every shortcut has a label and is a function key");
            for (size_t j=k+1;j<game.keys.size();++j) CHECK(game.keys[k].vk!=game.keys[j].vk,"a game's shortcuts use distinct keys");
        }
        const auto list=tools::items(g);
        CHECK(list.size()==game.keys.size()+6 && list.size()<=tools::MaxItems,"the panel lists the shortcuts and six settings, and they fit the grid");
        for (size_t k=0;k<game.keys.size();++k) CHECK(list[k].kind==tools::Kind::Key && list[k].key==k,"shortcuts come first, in order");
        int seen[7]{}; for (const auto& i:list) ++seen[static_cast<int>(i.kind)];
        CHECK(seen[static_cast<int>(tools::Kind::SwapEyes)]==1 && seen[static_cast<int>(tools::Kind::Curve)]==1 && seen[static_cast<int>(tools::Kind::Glow)]==1 &&
            seen[static_cast<int>(tools::Kind::FloatWindow)]==1 && seen[static_cast<int>(tools::Kind::Rumble)]==1 && seen[static_cast<int>(tools::Kind::Recenter)]==1,"each setting appears once");
    }
    CHECK(Games.size()>=2 && Games[0].id=="blindforest" && Games[1].id=="wotw","the shipped catalog starts with the first two games");
    CHECK(Games[0].keys.size()==5 && Games[0].keys[0].vk==VK_F1 && Games[0].keys[4].vk==VK_F5,"Blind Forest: F1 convergence ... F5 bloom, as documented");
    CHECK(Games[1].keys.size()==6 && Games[1].keys[3].vk==VK_F4 && Games[1].keys[5].vk==VK_F6,"Will of the Wisps: F4 blurriness, F5 vignette, F6 bloom");
    CHECK(tools::items(99).size()==6,"an unknown game still gets the settings");

    // --- Panel geometry --------------------------------------------------------------------------------------------
    {
        const auto close=tools::closeButton();
        auto overlap=[](const tools::PxRect& a,const tools::PxRect& b) { return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; };
        for (size_t i=0;i<tools::MaxItems;++i) {
            const auto a=tools::cell(i);
            CHECK(a.x>=0 && a.y>=0 && a.x+a.w<=tools::TexW && a.y+a.h<=tools::TexH,"every button lies inside the texture");
            CHECK(!overlap(a,close),"no button overlaps the close button");
            for (size_t j=i+1;j<tools::MaxItems;++j) CHECK(!overlap(a,tools::cell(j)),"buttons do not overlap");
        }
        CHECK(close.x+close.w<=tools::TexW && close.y>=0,"the close button is inside the texture");
        CHECK(nearly(tools::pxX(0),tools::TexW/2.0f) && nearly(tools::pxY(0),tools::TexH/2.0f) && nearly(tools::pxX(tools::PanelW/2),static_cast<float>(tools::TexW)),"pixel mapping is centred");
        CHECK(nearly(tools::PxPerMeter*tools::PanelH,static_cast<float>(tools::TexH),0.01f),"pixels are square on the panel");
        CHECK(tools::cell(1).x>tools::cell(0).x && tools::cell(4).y>tools::cell(0).y && nearly(tools::cell(4).x,tools::cell(0).x),"four columns, three rows");
    }

    // --- Panel behaviour -------------------------------------------------------------------------------------------
    {
        ToolsPanel idle; Hand hands[2]; hands[0].valid=true;
        const auto out=idle.update(hands,11);
        CHECK(!idle.open() && !out.pointer[0] && out.hover<0 && out.clicked<0,"a closed panel does nothing");
        // Placement: in front of where the head faces, a little below eye level, facing back at it.
        XrPosef turned{pose::yawRotation({0,std::sin(0.5f),0,std::cos(0.5f)}),{1,0.1f,2}};
        ToolsPanel p; p.show(turned);
        const auto toPanel=pose::sub(p.pose().position,turned.position);
        CHECK(nearly(pose::length(toPanel),std::sqrt(tools::Distance*tools::Distance+tools::Drop*tools::Drop),1e-4f) && nearly(toPanel.y,-tools::Drop),"the panel opens in front of you, a little low");
        CHECK(pose::dot(pose::normalize(XrVector3f{toPanel.x,0,toPanel.z}),pose::rotate(turned.orientation,{0,0,-1}))>0.9999f,"in the direction you are facing");
        CHECK(pose::dot(pose::rotate(p.pose().orientation,{0,0,1}),pose::normalize(XrVector3f{-toPanel.x,0,-toPanel.z}))>0.9999f,"and facing you");
    }
    {
        PanelRig r;
        r.point(1,r.cellCentre(2)); r.step();
        CHECK(r.last.hover==2 && r.last.pointer[1] && !r.last.pointer[0] && r.last.entered[1] && !r.last.pressed[1],"pointing at a button hovers it and draws that hand's laser");
        r.step(); CHECK(!r.last.entered[1] && r.last.hover==2,"entering is reported once");
        r.point(1,r.cellCentre(5)); r.step(); CHECK(r.last.hover==5 && r.last.entered[1],"moving to another button enters it");
        // A fresh click lands once and reports which hand.
        r.hands[1].click=true; r.step();
        CHECK(r.last.clicked==5 && r.last.clickHand==1 && r.last.pressed[1] && !r.last.close,"a fresh click on a button is reported");
        r.step(3); CHECK(r.last.clicked<0 && r.last.pressed[1],"holding does not repeat the click");
        r.hands[1].click=false; r.step(); CHECK(!r.last.pressed[1],"release");
        // Sweeping onto a button with the trigger already down is not a click.
        r.point(1,r.at(tools::TexW/2.0f,tools::TexH-8.0f)); r.hands[1].click=true; r.step(3);
        r.point(1,r.cellCentre(0)); r.step();      // the first frame on the button is when a click would fire
        CHECK(r.last.clicked<0 && r.last.hover==0,"a held trigger swept onto a button does not click it");
        r.step(3); CHECK(r.last.clicked<0,"and keeps not clicking while it stays down");
        r.hands[1].click=false; r.step();
        // The other hand works independently.
        r.point(0,r.cellCentre(7)); r.hands[0].click=true; r.step();
        CHECK(r.last.clicked==7 && r.last.clickHand==0 && r.last.hover==7 && r.last.pointer[0] && r.last.pointer[1],"the second hand clicks on its own");
        r.hands[0].click=false; r.hands[0].valid=false; r.step();
        // Close button.
        const auto c=tools::closeButton();
        r.point(1,r.at(c.x+c.w/2,c.y+c.h/2)); r.step();
        CHECK(r.last.closeHover && r.last.hover<0 && r.last.entered[1],"the close button hovers");
        r.hands[1].click=true; r.step();
        CHECK(r.last.close && r.last.clicked<0,"clicking the close button reports close");
        r.hands[1].click=false; r.step();
        // Blank panel: the laser shows, nothing is hovered or clicked, but the hand is still on the panel.
        r.point(1,r.at(tools::TexW/2.0f,tools::TexH-10.0f)); r.hands[1].click=true; r.step();
        r.hands[1].click=false; r.step(); r.hands[1].click=true; r.step();
        CHECK(r.last.pointer[1] && r.last.hover<0 && r.last.clicked<0 && !r.last.close && !r.last.pressed[1],"empty panel space is inert but still captures the laser");
        r.hands[1].click=false; r.step();
        // Off the panel altogether.
        r.point(1,{3,2,-1}); r.step();
        CHECK(!r.last.pointer[1] && r.last.hover<0,"aiming away from the panel leaves the game its controller");
        r.hands[1].valid=false; r.step(); CHECK(!r.last.pointer[1],"a lost controller does nothing");
        // Only listed buttons can be pressed.
        PanelRig few(5); few.point(1,few.cellCentre(8)); few.step();
        CHECK(few.last.hover<0 && few.last.pointer[1],"a button the game does not have cannot be pressed");
        // Hiding and showing again must not turn a held trigger into a click: press while it is closed, open it with the
        // trigger still down, and nothing may happen until it is released and pressed afresh.
        PanelRig held; held.point(1,held.cellCentre(1)); held.hands[1].click=false; held.step(2);
        held.panel.hide(); held.hands[1].click=true; held.step(); CHECK(!held.panel.open() && !held.last.pointer[1],"hidden panels do nothing");
        held.panel.show(held.head); held.step();     // the first frame is the one that matters
        CHECK(held.last.clicked<0 && held.last.hover==1,"opening with the trigger already down does not click");
        held.step(2); CHECK(held.last.clicked<0,"nor does it click later while the trigger stays down");
        held.hands[1].click=false; held.step(); held.hands[1].click=true; held.step();
        CHECK(held.last.clicked==1,"and a fresh press after that does");
        // Panel quads: the panel on its own sheet, then a beam and a cursor for the pointing hand.
        PanelRig q; q.point(0,q.cellCentre(3)); q.step();
        const auto quads=buildToolsQuads(q.panel,q.last,q.head);
        CHECK(quads.size()==3 && quads[0].sheet==2 && quads[0].rect.w==tools::TexW && nearly(quads[0].size.width/quads[0].size.height,static_cast<float>(tools::TexW)/tools::TexH,1e-4f),"panel quad on sheet 2");
        CHECK(nearly(quads[2].pose.position.z,quads[0].pose.position.z,0.01f) && quads[1].sheet==0 && quads[2].sheet==0,"beam and cursor come from the shared atlas");
        CHECK(buildToolsQuads(q.panel,ToolsPanel::Output{},q.head).size()==1,"no laser, no pointer quads");
    }

    // --- Key presser -----------------------------------------------------------------------------------------------
    {
        std::vector<std::pair<WORD,bool>> sent;
        const auto t0=Clock::now();
        {
            KeyPresser keys([&](WORD vk,bool down) { sent.push_back({vk,down}); });
            CHECK(keys.press(VK_F1,false,t0)==KeyPresser::Result::NotInFront && sent.empty() && !keys.holding(),"nothing is typed when the game does not have the keyboard");
            CHECK(keys.press('A',true,t0)==KeyPresser::Result::Refused && keys.press(VK_RETURN,true,t0)==KeyPresser::Result::Refused &&
                keys.press(VK_F12+1,true,t0)==KeyPresser::Result::Refused && keys.press(VK_F1-1,true,t0)==KeyPresser::Result::Refused && sent.empty(),"only function keys are ever pressed");
            CHECK(keys.press(VK_F3,true,t0)==KeyPresser::Result::Sent && sent.size()==1 && sent[0]==keyEvent(VK_F3,true) && keys.holding(),"a press goes down at once");
            CHECK(keys.press(VK_F4,true,t0+std::chrono::milliseconds(10))==KeyPresser::Result::Busy && sent.size()==1,"one key at a time");
            keys.tick(t0+std::chrono::milliseconds(KeyPresser::HoldMs-5)); CHECK(sent.size()==1 && keys.holding(),"held long enough for the fix to notice it");
            keys.tick(t0+std::chrono::milliseconds(KeyPresser::HoldMs+1));
            CHECK(sent.size()==2 && sent[1]==keyEvent(VK_F3,false) && !keys.holding(),"then released");
            keys.tick(t0+std::chrono::seconds(5)); CHECK(sent.size()==2,"no second release");
            CHECK(keys.press(VK_F4,true,t0+std::chrono::seconds(6))==KeyPresser::Result::Sent && sent.size()==3,"free again afterwards");
        }
        CHECK(sent.size()==4 && sent[3]==keyEvent(VK_F4,false),"a key still down is let go when the presser goes away");
        CHECK(!foregroundIs(L"flattodepth-no-such-game.exe"),"a game that does not exist never has the keyboard");
    }

    // --- Floating window -------------------------------------------------------------------------------------------
    {
        CHECK(fx::floatShift(2560,0)==0 && fx::floatShift(2560,-1)==0 && fx::floatShift(4,0.02f)==0,"no shift when off");
        CHECK(fx::floatShift(2560,0.016f)==41 && fx::floatShift(2560,0.05f)==128 && fx::floatShift(2560,1)==320,"the shift is a fraction of the picture, never more than an eighth");
        const UINT V=2560; const float W=6.0f; const UINT shift=41;
        const auto l=fx::eyeSlice(0,V,shift,W),r=fx::eyeSlice(1,V,shift,W);
        CHECK(l.x==shift && l.w==V-shift && r.x==0 && r.w==V-shift,"each eye drops its shift columns at one edge: the left eye its left, the right eye its right");
        const float perPixel=W/V;
        CHECK(nearly(l.width,(V-shift)*perPixel,1e-5f) && nearly(r.width,l.width,1e-6f),"both slices are equally wide");
        CHECK(nearly(l.centre-l.width/2,-W/2+shift*perPixel,1e-4f) && nearly(l.centre+l.width/2,W/2,1e-4f),"the left eye's slice ends at the screen's right edge and starts shift pixels in");
        CHECK(nearly(r.centre-r.width/2,-W/2,1e-4f) && nearly(r.centre+r.width/2,W/2-shift*perPixel,1e-4f),"the right eye's slice starts at the screen's left edge and ends shift pixels in");
        CHECK((l.centre-l.width/2)-(r.centre-r.width/2)>0 && (l.centre+l.width/2)-(r.centre+r.width/2)>0,"left edge further right in the left eye, right edge further right in the left eye: crossed, so nearer than the screen");
        const auto a=fx::eyeSlice(0,V,0,W),b=fx::eyeSlice(1,V,0,W);
        CHECK(a.x==0 && a.w==V && nearly(a.centre,0) && nearly(a.width,W) && b.x==0 && b.w==V && nearly(b.centre,0) && nearly(b.width,W),"no shift: both eyes show everything");
    }

    // --- Ambient glow ----------------------------------------------------------------------------------------------
    {
        CHECK(fx::GlowSampler::levelFor(1440)==7 && fx::GlowSampler::levelFor(1080)==6 && fx::GlowSampler::levelFor(720)==6 && fx::GlowSampler::levelFor(100)==3 && fx::GlowSampler::levelFor(8)==0,
            "the sampled level has about a dozen rows");
        for (UINT h:{240u,480u,720u,1080u,1440u,2160u}) { const UINT rows=h>>fx::GlowSampler::levelFor(h); CHECK(rows>=10 && rows<20,"between ten and nineteen rows"); }
        // Reading a grid: RGBA and BGRA, smoothing, change detection.
        std::vector<uint8_t> px(2*2*4);
        auto set=[&](int cell,uint8_t r,uint8_t g,uint8_t b) { px[cell*4]=r; px[cell*4+1]=g; px[cell*4+2]=b; px[cell*4+3]=255; };
        set(0,200,0,0); set(1,0,200,0); set(2,0,0,200); set(3,100,100,100);
        fx::GlowState s; CHECK(!s.valid(),"empty state is not valid");
        CHECK(s.update(px.data(),8,2,2,false,0.5f)>=255.0f && s.valid() && s.w==2 && s.h==2,"the first reading fills the grid whatever the blend");
        CHECK(nearly(s.rgb[0],200) && nearly(s.rgb[1],0) && nearly(s.rgb[4],200) && nearly(s.rgb[6],0) && nearly(s.rgb[8],200) && nearly(s.rgb[11],100),"RGBA channels land in order");
        fx::GlowState bgra; bgra.update(px.data(),8,2,2,true,1);
        CHECK(nearly(bgra.rgb[0],0) && nearly(bgra.rgb[2],200),"BGRA swaps red and blue");
        set(0,0,0,0);
        const float moved=s.update(px.data(),8,2,2,false,0.25f);
        CHECK(nearly(moved,50,1e-3f) && nearly(s.rgb[0],150,1e-3f),"a quarter of the way to the new reading");
        const fx::GlowState before=s; CHECK(nearly(before.distance(s),0) && s.distance(bgra)>1,"distance compares grids");
        const float same=s.update(px.data(),8,2,2,false,0); CHECK(nearly(same,0),"blend 0 changes nothing");
        std::vector<uint8_t> bigger(3*2*4); fx::GlowState other=s; other.update(bigger.data(),12,3,2,false,1);
        CHECK(other.w==3 && s.distance(other)>=255.0f,"a different grid size starts afresh and is not comparable");
        CHECK(s.update(nullptr,0,2,2,false,1)==0,"no data, no change");
        // Bilinear sampling hits cell centres exactly and blends between them.
        float c[3]; s.sample(0.5f,0.5f,c); CHECK(nearly(c[0],s.rgb[0]) && nearly(c[2],s.rgb[2]),"cell centre");
        s.sample(1.0f,0.5f,c); CHECK(nearly(c[0],(s.rgb[0]+s.rgb[3])/2) && nearly(c[1],(s.rgb[1]+s.rgb[4])/2),"halfway between two cells");
        s.sample(-5,-5,c); CHECK(nearly(c[0],s.rgb[0]),"clamped to the grid");

        // Rendering: left half of the picture red, right half blue, a bright cell in the middle of the bottom row.
        fx::GlowState scene; std::vector<uint8_t> cells(8*4*4);
        for (int y=0;y<4;++y) for (int x=0;x<8;++x) { uint8_t* q=&cells[(static_cast<size_t>(y)*8+x)*4]; q[0]=x<4 ? 220 : 10; q[1]=10; q[2]=x<4 ? 10 : 220; q[3]=255; }
        scene.update(cells.data(),32,8,4,false,1);
        const float aspect=16.0f/9,strength=0.6f;
        std::vector<uint32_t> px1; fx::renderGlow(px1,scene,strength,aspect,false);
        auto texel=[&](const std::vector<uint32_t>& v,float qx,float qy) {       // picture-height units, origin at the picture's centre
            const float spanX=aspect+2*fx::GlowMargin,spanY=1+2*fx::GlowMargin;
            const int tx=static_cast<int>((qx/spanX+0.5f)*fx::GlowTex),ty=static_cast<int>((0.5f-qy/spanY)*fx::GlowTex);
            return v[static_cast<size_t>(std::clamp(ty,0,255))*fx::GlowTex+std::clamp(tx,0,255)];
        };
        auto alpha=[](uint32_t p) { return p>>24; };
        CHECK(px1.size()==static_cast<size_t>(fx::GlowTex)*fx::GlowTex,"glow texture size");
        const float edge=aspect/2;
        CHECK(alpha(texel(px1,0,0))>=static_cast<uint32_t>(strength*255-3),"behind the picture it is as strong as the setting");
        const uint32_t leftGlow=texel(px1,-edge-0.1f,0),rightGlow=texel(px1,edge+0.1f,0);
        CHECK((leftGlow&255)>(leftGlow>>16&255)*4 && (rightGlow>>16&255)>(rightGlow&255)*4,"the glow takes the colour of the nearest edge: red on the left, blue on the right");
        CHECK(alpha(texel(px1,edge+0.05f,0))>alpha(texel(px1,edge+0.25f,0)) && alpha(texel(px1,edge+0.25f,0))>alpha(texel(px1,edge+0.45f,0)) && alpha(texel(px1,edge+0.45f,0))>0,"it fades smoothly with distance");
        CHECK(alpha(texel(px1,edge+fx::GlowMargin-0.01f,0))<10 && alpha(px1[0])==0 && alpha(px1.back())==0,"it is gone by the end of its reach, and the corners are clear");
        CHECK(alpha(texel(px1,0,0.5f+0.1f))>0 && alpha(texel(px1,0,0.5f+0.1f))<alpha(texel(px1,0,0)),"glow above and below too");
        std::vector<uint32_t> weak,none; fx::renderGlow(weak,scene,0.3f,aspect,false); fx::renderGlow(none,scene,0,aspect,false);
        CHECK(alpha(texel(weak,-edge-0.1f,0))<alpha(texel(px1,-edge-0.1f,0)) && alpha(texel(none,-edge-0.1f,0))==0,"strength scales it, zero is off");
        std::vector<uint32_t> bgraPx; fx::renderGlow(bgraPx,scene,strength,aspect,true);
        const uint32_t lg=texel(bgraPx,-edge-0.1f,0); CHECK((lg>>16&255)==(leftGlow&255) && (lg&255)==(leftGlow>>16&255),"BGRA path swaps red and blue only");
        std::vector<uint32_t> blank; fx::renderGlow(blank,fx::GlowState{},1,aspect,false);
        CHECK(blank.size()==static_cast<size_t>(fx::GlowTex)*fx::GlowTex && std::all_of(blank.begin(),blank.end(),[](uint32_t p) { return p==0; }),"no reading yet, no glow");
    }

    // --- Layers for the compositor ---------------------------------------------------------------------------------
    {
        ScreenLayer layer; const XrSpace space=reinterpret_cast<XrSpace>(0x1234); const XrSwapchain chain=reinterpret_cast<XrSwapchain>(0x5678);
        const XrPosef window{{0,0,0,1},{0.2f,0.1f,-2.5f}};
        const XrRect2Di rect{{41,0},{2519,1440}};
        fillScreenLayer(layer,space,window,0,false,0.4f,5.0f,2.8f,chain,rect,XR_EYE_VISIBILITY_LEFT,0);
        const auto* flat=reinterpret_cast<const XrCompositionLayerQuad*>(layer.header());
        CHECK(!layer.curved && flat->type==XR_TYPE_COMPOSITION_LAYER_QUAD && flat->space==space && flat->eyeVisibility==XR_EYE_VISIBILITY_LEFT &&
            flat->subImage.swapchain==chain && flat->subImage.imageRect.offset.x==41 && flat->subImage.imageRect.extent.width==2519,"flat: a quad with the eye, swapchain and sub-image");
        CHECK(nearly(flat->pose.position,{0.6f,0.1f,-2.5f}) && nearly(flat->size.width,5.0f) && nearly(flat->size.height,2.8f),"flat: the slice is offset along the plane and sized in metres");
        const float R=2.5f;
        fillScreenLayer(layer,space,window,R,false,0,6.0f,3.375f,chain,rect,XR_EYE_VISIBILITY_BOTH,XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
        const auto* curved=reinterpret_cast<const XrCompositionLayerCylinderKHR*>(layer.header());
        CHECK(layer.curved && curved->type==XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR && curved->radius==R && curved->eyeVisibility==XR_EYE_VISIBILITY_BOTH &&
            curved->layerFlags==XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,"curved: a cylinder layer");
        CHECK(nearly(curved->centralAngle*curved->radius,6.0f,1e-4f) && nearly(curved->aspectRatio,6.0f/3.375f,1e-4f),"curved: arc length and aspect ratio are the slice's");
        CHECK(nearly(curved->pose.position,window.position) && nearly(curved->pose.orientation.w,1),"curved: the pose is the surface point at the window's centre");
        // A slice off to one side sits further round the cylinder.
        fillScreenLayer(layer,space,window,R,false,1.25f,3.0f,3.0f,chain,rect,XR_EYE_VISIBILITY_RIGHT,0);
        const auto side=onScreen(window,R,1.25f,0);
        CHECK(nearly(layer.cylinder.pose.position,side.position) && pose::dot(pose::rotate(layer.cylinder.pose.orientation,{0,0,1}),pose::rotate(side.orientation,{0,0,1}))>0.9999f,"curved: an off-centre slice is placed on the surface and turned to face the axis");
        // The other reading of the specification: the pose is the axis, radius behind the surface.
        fillScreenLayer(layer,space,window,R,true,0,6.0f,3.375f,chain,rect,XR_EYE_VISIBILITY_BOTH,0);
        CHECK(nearly(layer.cylinder.pose.position,screenAxis(window,R)) && nearly(layer.cylinder.pose.orientation.w,1),"curved, pose at the axis: the same screen described from its centre");
        fillScreenLayer(layer,space,window,R,true,1.25f,3.0f,3.0f,chain,rect,XR_EYE_VISIBILITY_BOTH,0);
        CHECK(nearly(layer.cylinder.pose.position,screenAxis(window,R),1e-4f),"the axis is the same point for every slice");
        // Flat again afterwards: the same slot can be reused.
        fillScreenLayer(layer,space,window,0,true,0,6.0f,3.375f,chain,rect,XR_EYE_VISIBILITY_BOTH,0);
        CHECK(!layer.curved && layer.header()->type==XR_TYPE_COMPOSITION_LAYER_QUAD,"radius 0 is a flat quad whatever the pose setting");
    }

    // --- Settings --------------------------------------------------------------------------------------------------
    {
        CHECK(levelOf(CurveLevels,0)==0 && levelOf(CurveLevels,0.6f)==2 && levelOf(CurveLevels,1)==3 && levelOf(CurveLevels,0.14f)==0 && levelOf(CurveLevels,0.16f)==1,"nearest curve step");
        CHECK(levelOf(FloatLevels,0.016f)==2 && levelOf(FloatLevels,0.05f)==3 && levelOf(FloatLevels,0.002f)==0,"nearest floating-window step");
        for (int i=1;i<4;++i) CHECK(CurveLevels[i]>CurveLevels[i-1] && FloatLevels[i]>FloatLevels[i-1] && CurveLevels[i]<=1 && FloatLevels[i]<=0.05f,"the steps rise and stay in range");
        const auto dir=std::filesystem::temp_directory_path()/("flattodepth-tools-test-"+std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(dir);
        auto write=[&](const char* name,const std::string& text) { const auto p=dir/name; std::ofstream(p)<<text; return p; };
        // The settings file the program ships.
        for (const char* file:{"flattodepth.default.ini"}) {
            const auto path=root/file;
            CHECK(std::filesystem::exists(path),std::string("missing ")+file);
            Config c;
            try { c.load(path); } catch (const std::exception& e) { CHECK(false,std::string(file)+" does not load: "+e.what()); continue; }
            CHECK(nearly(c.distance,2.5f) && nearly(c.width,6.0f) && c.swap && nearly(static_cast<float>(c.cropAspect),1.777778f,1e-5f) && c.uiAutoHide,"existing settings read as before");
            CHECK(nearly(c.curve,0) && !c.curveAtAxis && c.glow && nearly(c.glowStrength,0.55f) && nearly(c.floatWindow,0),"new screen settings: flat, glow on, floating window off");
            CHECK(c.rumble && nearly(c.rumbleStrength,1) && !c.rumbleSplit,"rumble on, both controllers follow the stronger motor");
        }
        // A file written before these settings existed still loads, with the defaults.
        {
            Config c; c.load(write("old.ini","[screen]\ndistance_m=2.5\npicture_width_m=6.0\nvertical_offset_m=0.0\nswap_eyes=0\ncrop_aspect=1.777778\n[ui]\nauto_hide=1\npush_pull_rate=1.1\n"));
            CHECK(!c.swap && nearly(c.curve,0) && c.glow && c.rumble && nearly(c.floatWindow,0),"an older settings file keeps working");
        }
        // Explicit values.
        {
            Config c; c.load(write("new.ini","[screen]\nswap_eyes=1\npicture_width_m=4\ncurvature=0.55\ncurve_pose_at_axis=1\nglow=0\nglow_strength=0.3\nfloat_window=0.016\n[haptics]\nenabled=0\nstrength=1.5\nsplit=1\n"));
            CHECK(c.swap && nearly(c.width,4) && nearly(c.curve,0.55f) && c.curveAtAxis && !c.glow && nearly(c.glowStrength,0.3f) && nearly(c.floatWindow,0.016f) && !c.rumble && nearly(c.rumbleStrength,1.5f) && c.rumbleSplit,"every new setting is read");
        }
        // Bad values are refused rather than half-applied.
        for (const char* bad:{"curvature=1.5","curvature=-0.1","curvature=abc","curve_pose_at_axis=2","glow=2","glow_strength=1.5","float_window=0.5","float_window=-1"}) {
            bool threw=false; try { Config c; c.load(write("bad.ini",std::string("[screen]\n")+bad+"\n")); } catch (const std::exception&) { threw=true; }
            CHECK(threw,std::string("refused: ")+bad);
        }
        for (const char* bad:{"enabled=2","strength=3","strength=-1","split=2"}) {
            bool threw=false; try { Config c; c.load(write("bad.ini",std::string("[haptics]\n")+bad+"\n")); } catch (const std::exception&) { threw=true; }
            CHECK(threw,std::string("refused: [haptics] ")+bad);
        }
        // What the headset saves comes back.
        {
            const auto path=write("save.ini","[screen]\nswap_eyes=1\n");
            Config c; c.load(path);
            c.save(L"screen",L"swap_eyes",0.0f); c.save(L"screen",L"curvature",0.55f); c.save(L"screen",L"glow",0.0f); c.save(L"screen",L"float_window",0.016f);
            c.save(L"haptics",L"enabled",0.0f);
            Config d; d.load(path);
            CHECK(!d.swap && nearly(d.curve,0.55f) && !d.glow && nearly(d.floatWindow,0.016f) && !d.rumble,"settings saved from the headset are read back");
            c.save(L"screen",L"swap_eyes",1.0f); Config e; e.load(path); CHECK(e.swap,"swapping the eyes back and forth survives a restart");
        }
        std::error_code ignored; std::filesystem::remove_all(dir,ignored);
    }

    // --- The panel's picture ---------------------------------------------------------------------------------------
    {
        ToolsVisual v; v.game=0;
        auto text=[&](size_t index) { return tools::describe(tools::items(0)[index],v); };
        CHECK(text(0).title==L"Convergence" && text(0).caption==L"F1" && !text(0).on && text(4).title==L"Bloom" && text(4).caption==L"F5","shortcut buttons name the key");
        v.game=1; CHECK(tools::describe(tools::items(1)[3],v).title==L"Blurriness" && tools::describe(tools::items(1)[5],v).caption==L"F6","Will of the Wisps has its own list");
        v.game=0;
        const size_t swap=5,curve=6,glow=7,window=8,rumble=9,recenter=10;
        CHECK(tools::items(0)[swap].kind==tools::Kind::SwapEyes && tools::items(0)[curve].kind==tools::Kind::Curve && tools::items(0)[recenter].kind==tools::Kind::Recenter,"button order");
        CHECK(text(swap).caption==L"Normal" && !text(swap).on,"eyes normal"); v.swapEyes=true; CHECK(text(swap).caption==L"Swapped" && text(swap).on,"eyes swapped");
        v.curve=2; CHECK(text(curve).caption==L"Medium" && text(curve).on,"curve medium"); v.curve=0; CHECK(text(curve).caption==L"Off" && !text(curve).on,"curve off");
        v.curveAvailable=false; v.curve=3; CHECK(text(curve).dim && !text(curve).on && text(curve).caption==L"Not available","no cylinder layers: dimmed");
        v.glow=true; CHECK(text(glow).caption==L"On" && text(glow).on,"glow on"); v.floatWindow=1; CHECK(text(window).caption==L"Low" && text(window).on,"floating window low");
        v.rumble=true; CHECK(text(rumble).on && text(rumble).caption==L"On","rumble on"); v.rumbleAvailable=false; CHECK(text(rumble).dim && text(rumble).caption==L"No haptics","no haptics: dimmed");
        CHECK(tools::levelName(-5)==std::wstring(L"Off") && tools::levelName(9)==std::wstring(L"High"),"level names clamp");
        // Pixels: the panel is opaque, buttons light up with state, the close button is a disc.
        ToolsVisual base; base.game=0; base.curveAvailable=true;
        std::vector<uint32_t> a,b,c;
        tools::render(a,base,false);
        auto px=[&](const std::vector<uint32_t>& img,float x,float y) { return img[static_cast<size_t>(y)*tools::TexW+static_cast<size_t>(x)]; };
        auto red=[](uint32_t p) { return static_cast<int>(p&255); };
        auto green=[](uint32_t p) { return static_cast<int>((p>>8)&255); };
        auto alphaOf=[](uint32_t p) { return static_cast<int>(p>>24); };
        CHECK(alphaOf(px(a,tools::TexW/2.0f,tools::TexH/2.0f))>200 && alphaOf(px(a,1,1))==0,"the panel is a rounded rectangle");
        const auto cell0=tools::cell(0),cell5=tools::cell(5);
        auto fillPoint=[&](const tools::PxRect& r) { return std::make_pair(r.x+10,r.y+r.h/2); };   // inside the button, clear of its centred text
        const auto f0=fillPoint(cell0),f5=fillPoint(cell5),f7=fillPoint(tools::cell(7));
        base.swapEyes=true; tools::render(b,base,false);
        CHECK(green(px(b,f5.first,f5.second))>green(px(a,f5.first,f5.second)),"a switched-on button turns green");
        CHECK(px(b,f0.first,f0.second)==px(a,f0.first,f0.second),"other buttons are untouched");
        base.swapEyes=false; base.hover=0; tools::render(b,base,false);
        CHECK(red(px(b,f0.first,f0.second))>red(px(a,f0.first,f0.second)),"a hovered button is brighter");
        base.pressed=true; tools::render(c,base,false);
        CHECK(green(px(c,f0.first,f0.second))>green(px(b,f0.first,f0.second)),"a pressed button takes the accent colour");
        base.hover=-1; base.pressed=false; base.curveAvailable=false; base.game=1; tools::render(b,base,false);
        CHECK(alphaOf(px(b,f7.first,f7.second))>0 && px(b,f7.first,f7.second)!=px(a,f7.first,f7.second),"the other game's layout differs (its curve button is dimmed where the first game's glow button is not)");
        const auto close=tools::closeButton();
        CHECK(px(a,close.x+close.w/2,close.y+close.h-12)!=px(a,close.x+close.w/2,close.y+close.h+10) && px(a,close.x+2,close.y+2)==px(a,close.x-12,close.y+close.h/2),"the close button is a disc: its corners are the panel");
        ToolsVisual hov=base; hov.game=0; hov.closeHover=true; std::vector<uint32_t> d; tools::render(d,hov,false);
        CHECK(px(d,close.x+close.w/2,close.y+close.h-12)!=px(a,close.x+close.w/2,close.y+close.h-12),"the close button reacts to the laser");
        std::vector<uint32_t> rgba,bgra; ToolsVisual st; st.swapEyes=true; tools::render(rgba,st,false); tools::render(bgra,st,true);
        const size_t at=static_cast<size_t>(f5.second)*tools::TexW+static_cast<size_t>(f5.first);
        CHECK((rgba[at]>>24)==(bgra[at]>>24) && (rgba[at]&255)==((bgra[at]>>16)&255) && ((rgba[at]>>16)&255)==(bgra[at]&255) && (rgba[at]&255)!=((rgba[at]>>16)&255),"BGRA path swaps red and blue only");
        ToolsVisual s1,s2; s2.status=L"x"; CHECK(!(s1==s2) && s1==ToolsVisual{},"visual states compare");
    }

    if (failures) { std::cerr<<failures<<" tools/fx check(s) failed\n"; return 1; }
    std::cout<<"PASS tools panel, key presser, floating window, glow, cylinder layers, settings\n";
    return 0;
}
