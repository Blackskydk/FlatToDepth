#include "common.hpp"
#include "bmp_dump.hpp"
#include "controller_frame.hpp"
#include "process_watch.hpp"
#include "window_control.hpp"
#include <cstdio>

static int failures=0;
#define CHECK(cond,msg) do { if (!(cond)) { ++failures; std::cerr<<"FAIL line "<<__LINE__<<": "<<(msg)<<'\n'; } } while (0)
static bool nearly(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }
static bool nearly(XrVector3f a,XrVector3f b,float eps=1e-3f) { return pose::length(pose::sub(a,b))<=eps; }

using Hand=WindowControl::Hand;
using Mode=WindowControl::Mode;
constexpr float Frame=1.0f/90;

// A seated user: head at the origin, a 3.2 m wide 16:9 window 2.5 m ahead, hands lower and to the sides.
struct Rig {
    WindowControl control;
    XrPosef window{{0,0,0,1},{0,0,-2.5f}},head{{0,0,0,1},{0,0,0}};
    float width=3.2f;
    const float aspect=9.0f/16;
    Hand hands[2];
    WindowControl::Output last;
    int released=0;
    XrVector3f handOrigin[2]{{-0.3f,-0.4f,0},{0.3f,-0.4f,0}};

    static XrPosef aim(XrVector3f from,XrVector3f to) {
        // facing() points +Z at a position, so aim it away from the target: -Z then looks at the target.
        return {pose::facing(from,pose::sub(from,pose::sub(to,from)),{0,0,0,1}),from};
    }
    XrVector3f spot(float x,float y) const { return pose::add(window.position,pose::rotate(window.orientation,{x,y,0})); }
    void point(int h,XrVector3f target) { hands[h].valid=true; hands[h].aim=aim(handOrigin[h],target); }
    void step(int frames=1) {
        for (int i=0;i<frames;++i) { last=control.update(hands,head,window,width,aspect,Frame); released+=last.released; }
    }
    WindowLayout layout() const { return windowLayout(width,aspect,pose::length(pose::sub(window.position,head.position))); }
    // Hover over the bar, then press: how every grab starts.
    void grabBar(int h) {
        point(h,spot(0,layout().barY)); step(3);
        hands[h].click=true; step();
    }
    void grabHandle(int h) {
        const auto l=layout();
        point(h,spot(l.handleX,l.handleY)); step(3);
        hands[h].click=true; step();
    }
    void letGo(int h) { hands[h].click=false; step(); }
};


static void dump(const char* path) {
    const ui::Visual states[]={
        {16,0,0,{false,false}},{16,1,1,{false,false}},{16,0,2,{true,false}},{8,0,0,{false,false}}};
    std::vector<uint32_t> sheet,one;
    for (const auto& v : states) { ui::render(one,v,false); sheet.insert(sheet.end(),one.begin(),one.end()); }
    writeBmp(path,sheet,ui::AtlasW,ui::AtlasH*static_cast<uint32_t>(std::size(states)));
    std::cout<<"Wrote "<<path<<'\n';
}

int main(int argc,char** argv) {
    if (argc==3 && std::string(argv[1])=="--dump-ui") { dump(argv[2]); return 0; }

    // facing(): +Z at the head, no roll against world up.
    {
        const auto q=pose::facing({2,1,-3},{0,0,0},{0,0,0,1});
        const auto n=pose::normalize(XrVector3f{-2,-1,3});
        CHECK(nearly(pose::rotate(q,{0,0,1}),n,1e-4f),"facing normal");
        CHECK(nearly(pose::rotate(q,{1,0,0}).y,0,1e-4f),"facing keeps the window upright (right axis horizontal)");
        const auto straightUp=pose::facing({0,3,0},{0,0,0},pose::fromBasis({1,0,0},{0,0,-1},{0,1,0}));
        CHECK(nearly(pose::rotate(straightUp,{0,0,1}),{0,-1,0},1e-4f),"facing overhead, degenerate horizon");
    }
    // Ray against the window plane.
    {
        XrPosef window{{0,0,0,1},{0,0,-2}};
        const auto hit=hitPlane({{0,0,0,1},{0.5f,0.25f,0}},window);
        CHECK(hit.ok && nearly(hit.t,2) && nearly(hit.x,0.5f) && nearly(hit.y,0.25f),"straight-ahead hit");
        CHECK(!hitPlane({pose::fromBasis({-1,0,0},{0,1,0},{0,0,-1}),{0,0,0}},window).ok,"pointing away must not hit");
        CHECK(!hitPlane({{0,0,0,1},{0,0,-3}},window).ok,"hand behind the plane must not hit");
    }

    // The bar and handle only show while a laser is on or next to them; pointing at the game keeps them hidden.
    {
        Rig r; const auto l=r.layout();
        r.point(1,r.spot(0,0)); r.step(60);
        CHECK(r.last.hover[1]==UiTarget::Window,"window centre is the game, not a handle");
        CHECK(!r.last.capture[1] && !r.last.pointer[1],"aiming at the game must leave the game input alone and draw no laser");
        CHECK(r.last.alpha<1e-6f,"pointing at the game does not reveal the bar");
        r.point(1,r.spot(l.barW/4,l.barY)); r.step(30);
        CHECK(r.last.alpha>0.99f,"the bar fades in while the laser is on it");
        CHECK(r.last.hover[1]==UiTarget::Bar && r.last.capture[1] && r.last.pointer[1],"bar hover captures the hand");
        CHECK(!r.last.capture[0],"the other controller keeps playing");
        // Just outside the grab area but inside the reveal margin: it stays shown, but the game keeps the hand.
        r.point(1,r.spot(0,l.barY-1.15f*l.barH)); r.step(2);
        CHECK(r.last.alpha>0.99f && r.last.hover[1]==UiTarget::None && !r.last.capture[1] && !r.last.pointer[1],"near the bar: shown, not grabbable");
        r.point(1,r.spot(l.handleX,l.handleY)); r.step(2);
        CHECK(r.last.hover[1]==UiTarget::Corner && r.last.capture[1] && r.last.alpha>0.99f,"pointing at the handle shows it and captures the hand");
        // Leaving them hides both again, quickly.
        r.point(1,r.spot(0,0)); r.step(90/2);
        CHECK(r.last.alpha<1e-6f,"the bar disappears soon after the laser leaves it");
        r.point(1,{3,6,-2}); r.step(30);
        CHECK(r.last.hover[1]==UiTarget::None && r.last.alpha<1e-6f,"nothing hovered or shown when aiming off to the side");
        // The bar is only a drag target in its own spot: the window's own corner is the game, not a handle.
        r.point(1,r.spot(l.width/2-0.05f,-l.height/2+0.05f)); r.step(30);
        CHECK(r.last.hover[1]==UiTarget::Window,"the game's own bottom-right corner is not the handle");
    }

    // Holding the trigger while sweeping onto the bar does not grab it: a fresh press is required.
    {
        Rig r; const auto l=r.layout();
        r.hands[1].click=true; r.point(1,r.spot(0,0)); r.step(5);
        r.point(1,r.spot(0,l.barY)); r.step(5);
        CHECK(r.control.mode()==Mode::Idle,"sweeping across the bar with a held trigger must not grab");
        r.letGo(1); r.hands[1].click=true; r.step();
        CHECK(r.control.mode()==Mode::Move,"fresh press on the bar grabs");
    }

    // Move: the window follows the laser, keeps facing the head, and releasing saves once.
    {
        Rig r; r.grabBar(1);
        CHECK(r.control.mode()==Mode::Move && r.last.pressed[1] && r.last.held==UiTarget::Bar,"bar press starts a move");
        const auto start=r.window.position;
        r.hands[1].aim.position.x+=0.5f;   // translate only: same laser direction
        r.step(1);
        CHECK(nearly(r.window.position,pose::add(start,{0.5f,0,0}),2e-3f),"translating the hand carries the window with it");
        r.step(120);
        const auto n=pose::normalize(pose::sub(r.head.position,r.window.position));
        CHECK(pose::dot(pose::rotate(r.window.orientation,{0,0,1}),n)>0.999f,"window turns to face the viewer");
        CHECK(nearly(pose::rotate(r.window.orientation,{1,0,0}).y,0,1e-3f),"window stays upright");
        CHECK(r.hands[0].click==false && !r.last.capture[0],"other hand unaffected");
        r.letGo(1);
        CHECK(r.released==1 && r.control.mode()==Mode::Idle,"release ends the move exactly once");
        r.step(10); CHECK(r.released==1,"no repeat release");
    }

    // Thumbstick push/pull along the laser.
    {
        Rig r; r.grabBar(1);
        const auto hand=r.hands[1].aim.position;
        const float before=pose::length(pose::sub(r.window.position,hand));
        r.hands[1].stickY=1; r.step(45);
        const float after=pose::length(pose::sub(r.window.position,hand));
        CHECK(nearly(after/before,std::exp(1.1f*0.5f),0.01f),"half a second of full push scales the distance by e^(rate*t)");
        const auto direction=pose::normalize(pose::sub(r.window.position,hand));
        r.hands[1].stickY=0.1f; r.step(30);
        CHECK(nearly(pose::length(pose::sub(r.window.position,hand)),after,1e-4f),"inside the dead zone nothing moves");
        r.hands[1].stickY=-1; r.step(90*6);
        CHECK(pose::length(pose::sub(r.window.position,r.head.position))>=WindowControl::MinHeadDistance-1e-3f,"cannot be pulled into your face");
        CHECK(pose::length(pose::sub(r.window.position,hand))>=WindowControl::MinHandDistance-1e-3f,"cannot be pulled into the controller");
        const auto closest=r.window.position;
        r.step(30); CHECK(nearly(r.window.position,closest,1e-5f),"holding pull at the limit does not wind up");
        r.hands[1].stickY=1; r.step(10);
        CHECK(pose::length(pose::sub(r.window.position,closest))>1e-3f,"pushing back out responds immediately");
        (void)direction;
    }
    {
        Rig r; r.grabBar(1); r.hands[1].stickY=1; r.step(90*20);
        CHECK(pose::length(pose::sub(r.window.position,r.hands[1].aim.position))<=WindowControl::MaxHandDistance*1.001f,"push distance is capped");
    }

    // Resize from the handle: the window grows out from its centre to both sides, aspect ratio preserved.
    {
        Rig r; const auto l=r.layout();
        const auto center=r.window.position;
        const float hs=l.handle;
        auto leftEdge=[&] { return r.window.position.x-r.width/2; };
        auto rightEdge=[&] { return r.window.position.x+r.width/2; };
        r.grabHandle(1);
        CHECK(r.control.mode()==Mode::Resize && r.last.held==UiTarget::Corner,"handle press starts a resize");
        r.step(2);
        CHECK(nearly(r.width,3.2f,1e-3f),"grabbing the handle does not jump the size");
        // Drag the corner to 1.5x: it should end up at centre + (2.4,-1.35), plus where on the handle it was grabbed.
        r.point(1,pose::add(center,{2.4f+0.58f*hs,-1.35f-0.58f*hs,0}));
        r.step(2);
        CHECK(nearly(r.width,4.8f,5e-3f),"dragging the handle outwards grows the window");
        CHECK(nearly(r.window.position,center,1e-5f),"the centre stays put");
        CHECK(nearly(leftEdge(),-2.4f,5e-3f) && nearly(rightEdge(),2.4f,5e-3f),"it grows to both sides, not just the right");
        // Shrinking works the same way from the centre.
        r.point(1,pose::add(center,{0.8f+0.58f*hs,-0.45f-0.58f*hs,0})); r.step(2);
        CHECK(nearly(r.width,1.6f,5e-3f) && nearly(r.window.position,center,1e-5f),"shrinks about the centre");
        // Aim at the plane's far side of the window centre: below the minimum.
        r.point(1,center); r.step(2);
        CHECK(nearly(r.width,0.3f,1e-4f) && nearly(r.window.position,center,1e-5f),"width clamps at the minimum");
        // A laser that leaves the window plane holds the size.
        r.point(1,pose::add(center,{3,3,0})); r.step(2);
        const float held=r.width;
        r.point(1,{0.3f,5,0.5f}); r.step(2);
        CHECK(nearly(r.width,held,1e-6f),"no change while the laser is not on the plane");
        r.point(1,{30,-30,-2.5f}); r.step(2);
        CHECK(r.width<=20.0f+1e-4f && r.width>19.9f,"width clamps at the maximum");
        r.letGo(1);
        CHECK(r.released==1,"resize release is reported once");
        CHECK(nearly(r.window.orientation.w,1,1e-6f) && nearly(r.window.position,center,1e-5f),"resizing never moves or rotates the window");
    }
    // A drag belongs to the hand that started it; the other hand cannot steal or end it.
    {
        Rig r; r.grabBar(1);
        r.hands[0].click=true; r.point(0,r.spot(0,0)); r.step(5);
        CHECK(r.control.mode()==Mode::Move && r.last.pressed[1] && !r.last.pressed[0],"second hand does not interfere");
        r.hands[1].valid=false; r.step();
        CHECK(r.control.mode()==Mode::Idle && r.released==1,"losing tracking ends the drag cleanly");
    }
    // reset() (focus loss, recenter) requires releasing and re-pressing.
    {
        Rig r; r.grabBar(1); r.control.reset();
        r.step(3); CHECK(r.control.mode()==Mode::Idle,"reset aborts the drag and ignores the held button");
        r.letGo(1); r.hands[1].click=true; r.step();
        CHECK(r.control.mode()==Mode::Move,"a new press works after reset");
    }
    // Without auto-hide the bar stays visible even when nobody points at it.
    {
        Rig r; r.control.configure({1.1f,false}); r.step(30);
        CHECK(r.last.alpha>0.99f,"auto_hide=0 keeps the UI visible");
    }

    // Geometry handed to the compositor.
    {
        Rig r; const auto l=r.layout();
        r.point(1,r.spot(0,l.barY)); r.step(30);
        const auto quads=buildUiQuads(r.last,r.window,r.head);
        CHECK(quads.size()==4,"bar + handle + one beam + one cursor");
        CHECK(nearly(quads[0].pose.position,r.spot(0,l.barY),1e-5f) && nearly(quads[0].size.width/quads[0].size.height,8.0f,1e-4f),"bar placed under the window, 8:1");
        CHECK(nearly(quads[1].pose.position,r.spot(l.handleX,l.handleY),1e-5f),"the handle sits just outside the bottom-right corner");
        const float beam=pose::length(pose::sub(r.last.rayEnd[1],r.last.rayStart[1]));
        CHECK(nearly(quads[2].size.width,beam,1e-4f),"beam spans controller to cursor");
        const auto beamAxis=pose::rotate(quads[2].pose.orientation,{1,0,0});
        CHECK(nearly(beamAxis,pose::normalize(pose::sub(r.last.rayEnd[1],r.last.rayStart[1])),1e-3f),"beam runs along the laser");
        // Seen from behind the hand the beam is nearly end-on, so only require the face to be turned towards the viewer.
        const auto faceN=pose::rotate(quads[2].pose.orientation,{0,0,1});
        CHECK(nearly(pose::dot(faceN,beamAxis),0,1e-3f) && pose::dot(faceN,pose::normalize(pose::sub(r.head.position,quads[2].pose.position)))>0.1f,
            "beam face is perpendicular to the laser and turned to the viewer");
        for (const auto& q : quads) CHECK(std::isfinite(q.pose.position.x+q.pose.position.y+q.pose.position.z+q.size.width+q.size.height),"finite quad");
        Rig idle; idle.control.configure({1.1f,true}); idle.step(10);
        CHECK(buildUiQuads(idle.last,idle.window,idle.head).empty(),"hidden UI submits no layers");
        // Bar keeps roughly the same apparent size at any distance.
        const auto distant=windowLayout(3.2f,9.0f/16,10), nearby=windowLayout(3.2f,9.0f/16,2);
        CHECK(distant.barH>nearby.barH*2,"UI scales up with distance");
        const auto tiny=windowLayout(0.3f,9.0f/16,5);
        CHECK(tiny.barW<=0.3f*0.6f,"UI stays inside tiny windows");
    }

    // Curved screen: a section of a vertical cylinder, tangent to the window plane at its centre.
    {
        auto rotY=[](float a) { return XrQuaternionf{0,std::sin(a/2),0,std::cos(a/2)}; };
        auto rotX=[](float a) { return XrQuaternionf{std::sin(a/2),0,0,std::cos(a/2)}; };
        const XrPosef window{{0,0,0,1},{0,0,-2.5f}};
        const float R=2.5f;     // concentric with a viewer standing at the origin
        CHECK(nearly(screenAxis(window,R),{0,0,0}),"the axis is the viewer when the radius equals the distance");
        CHECK(nearly(curveRadius(2.5f,1.0f),2.5f) && nearly(curveRadius(2.5f,0.5f),5.0f) && curveRadius(2.5f,0)==0 && curveRadius(2.5f,0.005f)==0,
            "curvature 1 wraps round the viewer, smaller is gentler, 0 is flat");
        CHECK(nearly(effectiveRadius(2.5f,10),10/MaxArc,1e-4f) && effectiveRadius(0,10)==0 && nearly(effectiveRadius(2.5f,3),2.5f),
            "a very wide screen is not wrapped further than the maximum arc");

        // From the axis every direction meets the wall at the radius, and arc length is radius times angle.
        {
            const auto straight=hitScreen({{0,0,0,1},{0,0,0}},window,R);
            CHECK(straight.ok && nearly(straight.t,R) && nearly(straight.x,0) && nearly(straight.y,0),"curved: straight ahead from the axis");
            const float phi=0.5f,alpha=0.2f;
            const auto q=pose::multiply(rotY(-phi),rotX(alpha));
            const auto angled=hitScreen({q,{0,0,0}},window,R);
            CHECK(angled.ok && nearly(angled.x,R*phi,2e-3f) && nearly(angled.y,R*std::tan(alpha),2e-3f) && nearly(angled.t,R/std::cos(alpha),2e-3f),
                "curved: arc length is radius times the angle, height follows the elevation");
            const auto left=hitScreen({rotY(0.7f),{0,0,0}},window,R);
            CHECK(left.ok && nearly(left.x,-R*0.7f,2e-3f),"curved: left of centre is negative");
        }
        // Round trip: aiming from anywhere inside at a point on the surface returns that point.
        {
            const XrVector3f hands[]{{0,0,0},{0.5f,-0.3f,0.5f},{-0.9f,0.2f,-0.4f},{0.3f,-0.5f,1.0f}};
            const float us[]{0,0.9f,-1.4f,2.0f,-2.3f};
            const float vs[]{0,-1.0f,0.7f};
            int checked=0;
            for (const auto& hand:hands) for (float u:us) for (float v:vs) {
                const auto target=onScreen(window,R,u,v).position;
                const auto got=hitScreen(Rig::aim(hand,target),window,R);
                CHECK(got.ok && nearly(got.x,u,3e-3f) && nearly(got.y,v,3e-3f) && nearly(got.world,target,3e-3f),"curved: aiming at a surface point hits it");
                ++checked;
            }
            CHECK(checked==60,"round trip coverage");
        }
        CHECK(!hitScreen({{0,0,0,1},{0,0,-6}},window,R).ok,"curved: a hand behind the screen cannot hit it");
        {
            const auto behind=hitScreen({rotY(3.14159265f),{0,0,0}},window,R);
            CHECK(!behind.ok || std::fabs(behind.x)>R*3.0f,"curved: pointing backwards lands far outside any screen's width");
            XrPosef up{pose::fromBasis({1,0,0},{0,0,1},{0,-1,0}),{0,0,0}};   // forward (-Z) is world up
            CHECK(!hitScreen(up,window,R).ok,"curved: straight up never meets the wall");
        }
        // Radius 0 is the flat window, unchanged.
        {
            const auto a=hitScreen({{0,0,0,1},{0.5f,0.25f,0}},{{0,0,0,1},{0,0,-2}},0), b=hitPlane({{0,0,0,1},{0.5f,0.25f,0}},{{0,0,0,1},{0,0,-2}});
            CHECK(a.ok==b.ok && nearly(a.x,b.x) && nearly(a.y,b.y) && nearly(a.t,b.t),"radius 0 behaves exactly like the flat plane");
        }
        // Surface poses: on the cylinder, facing its axis, upright.
        {
            const auto axis=screenAxis(window,R);
            for (float u:{0.0f,0.8f,-1.6f}) {
                const auto p=onScreen(window,R,u,0.3f);
                const auto toAxis=pose::sub(axis,{p.position.x,axis.y,p.position.z});
                CHECK(nearly(pose::length(toAxis),R,1e-4f) && nearly(p.position.y,0.3f),"surface point lies on the cylinder");
                CHECK(pose::dot(pose::rotate(p.orientation,{0,0,1}),pose::normalize(toAxis))>0.9999f,"surface pose faces the axis");
                CHECK(nearly(pose::rotate(p.orientation,{0,1,0}).y,1,1e-5f),"surface pose stays upright");
            }
            const auto centre=onScreen(window,R,0,0);
            CHECK(nearly(centre.position,window.position,1e-5f) && nearly(centre.orientation.w,1,1e-5f),"the centre of the curved screen is the window pose");
            const auto flat=onScreen(window,0,1.2f,-0.4f);
            CHECK(nearly(flat.position,{1.2f,-0.4f,-2.5f}) && nearly(flat.orientation.w,1),"flat placement is a plain offset in the plane");
        }
        // The bar, handle and resizing all work in arc length on a curved screen.
        {
            Rig r; r.control.setRadius(R);
            const auto l=r.layout();
            auto surface=[&](float u,float v) { return onScreen(r.window,R,u,v).position; };
            r.point(1,surface(0,l.barY)); r.step(10);
            CHECK(r.last.hover[1]==UiTarget::Bar && r.last.capture[1] && r.last.alpha>0.5f && nearly(r.last.radius,R),"curved: the bar is found on the surface");
            r.point(1,surface(l.handleX,l.handleY)); r.step(3);
            CHECK(r.last.hover[1]==UiTarget::Corner,"curved: the handle is found out at the bottom-right corner");
            r.point(1,surface(0,0)); r.step(2);
            CHECK(r.last.hover[1]==UiTarget::Window,"curved: the picture is the game");
            r.point(1,surface(r.width/2-0.05f,-l.height/2+0.05f)); r.step(2);
            CHECK(r.last.hover[1]==UiTarget::Window,"curved: the picture's own corner is not the handle");
            // Resize by dragging the handle, with the surface coordinates of the flat test.
            const auto centre=r.window; const float hs=l.handle;
            r.point(1,surface(l.handleX,l.handleY)); r.step(3); r.hands[1].click=true; r.step(); r.step(2);
            CHECK(r.control.mode()==Mode::Resize && nearly(r.width,3.2f,1e-3f),"curved: grabbing the handle does not jump the size");
            r.point(1,onScreen(centre,R,2.4f+0.58f*hs,-1.35f-0.58f*hs).position); r.step(2);
            CHECK(nearly(r.width,4.8f,6e-3f) && nearly(r.window.position,centre.position,1e-5f),"curved: dragging the handle grows the screen about its centre");
            r.letGo(1);
            // The compositor quads sit on the surface and face its axis.
            r.control.setRadius(R);
            const auto l2=r.layout();
            r.point(1,onScreen(r.window,R,0,l2.barY).position); r.step(30);
            const auto quads=buildUiQuads(r.last,r.window,r.head);
            CHECK(quads.size()==4,"curved: bar + handle + beam + cursor");
            const auto axis=screenAxis(r.window,R);
            auto towardsAxis=[&](XrVector3f at) { return pose::normalize(pose::sub({axis.x,at.y,axis.z},at)); };   // horizontal, at the same height
            const auto handlePose=onScreen(r.window,R,l2.handleX,l2.handleY);
            CHECK(nearly(quads[1].pose.position,handlePose.position,1e-5f) &&
                pose::dot(pose::rotate(quads[1].pose.orientation,{0,0,1}),towardsAxis(handlePose.position))>0.9999f,
                "curved: the handle sits on the surface facing the viewer, not in the flat plane");
            CHECK(nearly(quads[0].pose.position,onScreen(r.window,R,0,l2.barY).position,1e-5f),"curved: the bar sits under the centre");
            const auto dotNormal=pose::rotate(quads[3].pose.orientation,{0,0,1});
            CHECK(pose::dot(dotNormal,towardsAxis(r.last.rayEnd[1]))>0.99f,"curved: the cursor lies on the surface under the laser");
            for (const auto& q:quads) CHECK(std::isfinite(q.pose.position.x+q.pose.position.y+q.pose.position.z+q.size.width+q.size.height),"finite curved quad");
        }
        // Radius cannot change under a resize drag, so the surface does not shift under the hand.
        {
            Rig r; r.control.setRadius(R); const auto l=r.layout();
            r.point(1,onScreen(r.window,R,l.handleX,l.handleY).position); r.step(3); r.hands[1].click=true; r.step();
            CHECK(r.control.mode()==Mode::Resize,"curved: the radius test starts a resize");
            r.control.setRadius(9); CHECK(nearly(r.control.radius(),R),"radius is held while resizing");
            r.letGo(1); r.control.setRadius(9); CHECK(nearly(r.control.radius(),9),"radius can change again afterwards");
        }
    }

    // The tools button: left of the bar, a press and not a drag.
    {
        Rig r; r.control.configure({1.1f,true,0.3f,20.0f,true});
        const auto l=r.layout();
        CHECK(!l.hasTools,"the Rig computes layouts without tools");
        const auto lt=windowLayout(r.width,r.aspect,2.5f,true);
        CHECK(lt.hasTools && lt.toolsX<-lt.barW/2 && nearly(lt.toolsY,lt.barY) && lt.tools>lt.barH,"the button sits left of the bar, level with it");
        auto spot=[&](float x,float y) { return r.spot(x,y); };
        r.point(1,spot(lt.toolsX,lt.toolsY)); r.step(10);
        CHECK(r.last.hover[1]==UiTarget::Tools && r.last.capture[1] && r.last.pointer[1] && r.last.alpha>0.5f,"pointing at the button shows it and captures the hand");
        CHECK(!r.last.toolsClicked,"hovering is not a press");
        r.hands[1].click=true; r.step();
        CHECK(r.last.toolsClicked && r.control.mode()==Mode::Idle,"a fresh press is reported once and starts no drag");
        r.step(3); CHECK(!r.last.toolsClicked,"holding does not repeat");
        r.letGo(1);
        // Sweeping onto the button with the trigger already down is not a press.
        r.point(1,spot(0,0)); r.hands[1].click=true; r.step(3);
        r.point(1,spot(lt.toolsX,lt.toolsY)); r.step();      // the first frame on the button is when a press would fire
        CHECK(!r.last.toolsClicked && r.last.hover[1]==UiTarget::Tools,"a held trigger swept onto the button does not press it");
        r.step(3); CHECK(!r.last.toolsClicked,"and keeps not pressing while it stays down");
        r.letGo(1);
        // The bar next to it is still the bar, and the controls reveal together.
        r.point(1,spot(0,0)); r.step(60);
        CHECK(r.last.alpha<1e-6f,"hidden when pointing at the game");
        r.point(1,spot(lt.toolsX,lt.toolsY-1.2f*lt.tools)); r.step(8);
        CHECK(r.last.alpha>0.5f && r.last.hover[1]==UiTarget::None,"just below the button: revealed, not pressable");
        r.point(1,spot(0,lt.barY)); r.step(2);
        CHECK(r.last.hover[1]==UiTarget::Bar,"the bar still works with the button beside it");
        // Without tools enabled the spot is just empty space.
        Rig plain; const auto flatLayout=windowLayout(plain.width,plain.aspect,2.5f,false);
        plain.point(1,plain.spot(lt.toolsX,lt.toolsY)); plain.step(3);
        CHECK(plain.last.hover[1]==UiTarget::None && !plain.last.capture[1] && flatLayout.hasTools==false,"no button, no capture when tools are off");
        // The atlas draws it, and it follows the panel state.
        std::vector<uint32_t> px; ui::Visual v; v.alpha=ui::FadeSteps; ui::render(px,v,false);
        auto alpha=[&](ui::Rect rect,uint32_t x,uint32_t y) { return (px[static_cast<size_t>(rect.y+y)*ui::AtlasW+rect.x+x]>>24)&255; };
        CHECK(alpha(ui::ToolsRect,64,64)>150 && alpha(ui::ToolsRect,1,1)==0,"the button is a round disc");
        std::vector<uint32_t> open; v.tools=2; ui::render(open,v,false);
        CHECK(open!=px,"an open panel lights the button");
        v.alpha=0; ui::render(px,v,false); CHECK(alpha(ui::ToolsRect,64,64)==0,"the button fades with the bar");
        CHECK(visualFor(r.last,true).tools==2 && visualFor(r.last,false).tools==0,"the visual knows when the panel is open");
    }

    // Per-hand gamepad masking.
    {
        ControllerFrame f;
        f.hands[0].sThumbLX=1000; f.hands[0].bLeftTrigger=200; f.hands[0].wButtons=XINPUT_GAMEPAD_LEFT_SHOULDER;
        f.hands[1].sThumbRY=-2000; f.hands[1].bRightTrigger=255; f.hands[1].wButtons=XINPUT_GAMEPAD_A;
        bool none[2]{false,false},left[2]{true,false},right[2]{false,true},both[2]{true,true};
        auto all=f.gamepad(none);
        CHECK(all.sThumbLX==1000 && all.bLeftTrigger==200 && all.sThumbRY==-2000 && all.bRightTrigger==255 &&
            all.wButtons==(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_A),"uncaptured pad passes through");
        auto noLeft=f.gamepad(left);
        CHECK(noLeft.sThumbLX==0 && noLeft.bLeftTrigger==0 && noLeft.wButtons==XINPUT_GAMEPAD_A && noLeft.bRightTrigger==255,"left hand masked");
        auto noRight=f.gamepad(right);
        CHECK(noRight.sThumbRY==0 && noRight.bRightTrigger==0 && noRight.wButtons==XINPUT_GAMEPAD_LEFT_SHOULDER && noRight.sThumbLX==1000,"right hand masked: A cannot jump");
        auto nothing=f.gamepad(both);
        const XINPUT_GAMEPAD zero{};
        CHECK(std::memcmp(&nothing,&zero,sizeof(nothing))==0,"both hands masked");
    }

    // UI art.
    {
        std::vector<uint32_t> px;
        auto alpha=[&](ui::Rect r,uint32_t x,uint32_t y) { return (px[static_cast<size_t>(r.y+y)*ui::AtlasW+r.x+x]>>24)&255; };
        ui::Visual v; v.alpha=ui::FadeSteps; ui::render(px,v,false);
        CHECK(alpha(ui::BarRect,512,64)>200 && alpha(ui::BarRect,2,2)==0,"bar is opaque in the middle and transparent outside the pill");
        CHECK(alpha(ui::DotRect[0],32,32)>200 && alpha(ui::DotRect[0],0,0)==0,"cursor dot");
        CHECK(alpha(ui::LaserRect[0],64,16)>150 && alpha(ui::LaserRect[0],64,0)<8 && alpha(ui::LaserRect[0],0,16)<8,"beam is soft-edged and fades in");
        CHECK(alpha(ui::CornerRect,104,100)>200,"bracket elbow");
        v.alpha=0; ui::render(px,v,false);
        CHECK(alpha(ui::BarRect,512,64)==0 && alpha(ui::CornerRect,104,100)==0,"fully faded bar and handle");
        CHECK(alpha(ui::DotRect[0],32,32)>200,"cursor does not fade with the bar");
        std::vector<uint32_t> rgba,bgra; v.alpha=16; ui::render(rgba,v,false); ui::render(bgra,v,true);
        // The beam is light blue (R != B), so it shows whether red and blue were swapped.
        const size_t at=static_cast<size_t>(ui::LaserRect[0].y+16)*ui::AtlasW+ui::LaserRect[0].x+64;
        CHECK((rgba[at]>>24)==(bgra[at]>>24) && (rgba[at]&255)==((bgra[at]>>16)&255) && ((rgba[at]>>16)&255)==(bgra[at]&255) &&
            (rgba[at]&255)!=((rgba[at]>>16)&255),"BGRA path swaps red and blue only");
        v.bar=2; ui::Visual w=v; w.bar=0;
        CHECK(!(v==w),"visual states compare unequal");
    }

    // A 16:9 game on an ultrawide export: only the centred 16:9 picture is shown.
    {
        D3D11_TEXTURE2D_DESC d{}; d.Width=10240; d.Height=1440; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
        const auto wide=StereoLayout::from(d).cropped(16.0/9);
        CHECK(wide.width==5120 && wide.shown==2560 && wide.cropX==1280 && wide.visibleWidth()==2560,"ultrawide export crops to the centred 2560x1440");
        const auto left=wide.box(0),right=wide.box(1),swapped=wide.box(0,true);
        CHECK(left.left==1280 && left.right==3840 && left.top==0 && left.bottom==1440,"left eye box covers the 16:9 picture");
        CHECK(right.left==5120+1280 && right.right==5120+3840,"right eye box covers the 16:9 picture");
        CHECK(swapped.left==right.left,"swap_eyes still reads the other eye, with the same crop");
        const auto all=StereoLayout::from(d).cropped(0);
        CHECK(all.visibleWidth()==5120 && all.box(1).left==5120 && all.box(1).right==10240,"crop_aspect=0 keeps the whole export");
        d.Width=5120; const auto normal=StereoLayout::from(d).cropped(16.0/9);
        CHECK(normal.visibleWidth()==2560 && normal.cropX==0,"a 16:9 export is left alone");
        d.Width=5200; const auto close=StereoLayout::from(d).cropped(16.0/9);
        CHECK(close.visibleWidth()==2600 && close.cropX==0,"within 2% of 16:9 is not cropped");
        d.Width=3440*2; d.Height=1440; const auto ultra=StereoLayout::from(d).cropped(16.0/9);
        CHECK(ultra.shown%2==0 && ultra.shown==2560 && ultra.cropX==(3440-2560)/2,"21:9 export crops to an even width");
        d.Width=2*1920; d.Height=1080; const auto fourThree=StereoLayout::from(d).cropped(4.0/3);
        CHECK(fourThree.shown==1440 && fourThree.cropX==240,"other aspects work too");
    }
    // The bridge follows the game it was started for.
    {
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr,path,MAX_PATH);
        const std::wstring self=std::filesystem::path(path).filename().wstring();
        CHECK(processRunning(self),"a running process is found by name");
        std::wstring upper=self; for (auto& c : upper) c=static_cast<wchar_t>(towupper(c));
        CHECK(processRunning(upper),"names match case-insensitively");
        CHECK(!processRunning(L"flattodepth-no-such-game.exe"),"an absent process is not found");
        ProcessFollower absent(L"flattodepth-no-such-game.exe");
        CHECK(!absent.finished(),"a game that has not shown up yet is given time to start");
        ProcessFollower present(self);
        CHECK(!present.finished() && !ProcessFollower(L"").finished() && !ProcessFollower(L"").enabled(),"alive or unset: keep running");
    }

    if (failures) { std::cerr<<failures<<" window-control check(s) failed\n"; return 1; }
    std::cout<<"PASS pointer window control: hover reveal, edge-triggered grab, laser-attached move, thumbstick push/pull, centred resize, input masking, UI art, picture crop, process follower\n";
    return 0;
}
