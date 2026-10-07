#include "common.hpp"
#include "bmp_dump.hpp"
#include "curved_screen.hpp"
#include <cstring>

// The curved screen's geometry (no GPU needed) and its renderer, run on WARP, the software Direct3D 11 device every Windows
// PC has, so it needs no graphics card and runs on the build servers too.
//   FlatToDepthCurvedTest [--dump <folder>] [--hardware]    --dump also writes the rendered images there, to be looked at;
//   --hardware renders on the real graphics card instead of WARP.

static int failures=0;
#define CHECK(cond,msg) do { if (!(cond)) { ++failures; std::cerr<<"FAIL line "<<__LINE__<<": "<<(msg)<<'\n'; } } while (0)
static bool nearly(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<=eps; }

static XrFovf fovOf(float half) { return {-half,half,half,-half}; }
static const XrPosef Eye{{0,0,0,1},{0,0,0}};
static const XrPosef Window{{0,0,0,1},{0,0,-2.5f}};     // 2.5 m ahead, facing the viewer

static void geometry() {
    using namespace curved;
    const auto fov=fovOf(0.7853982f);    // tan = 1 on every side
    std::vector<Vertex> v;

    // A flat strip is one quad: two columns, four vertices.
    buildStrip(v,Eye,fov,Window,0,Strip{0,2,1});
    CHECK(v.size()==4,"a flat strip has four vertices");
    if (v.size()==4) {
        // Top left corner: 1 m left and 0.5 m up at 2.5 m depth is 0.4 left and 0.2 up in normalised coordinates.
        CHECK(nearly(v[0].x/v[0].w,-0.4f) && nearly(v[0].y/v[0].w,0.2f) && nearly(v[0].w,2.5f) && nearly(v[0].u,0) && nearly(v[0].v,0),"top left corner");
        CHECK(nearly(v[1].y/v[1].w,-0.2f) && nearly(v[1].v,1),"bottom left corner");
        CHECK(nearly(v[2].x/v[2].w,0.4f) && nearly(v[2].u,1),"right edge");
        CHECK(nearly(v[0].z,2.5f-NearPlane),"the depth is arranged for clipping at the near plane");
    }
    // The part of the picture that is shown picks the texture columns, and a strip off to one side sits off to that side.
    buildStrip(v,Eye,fov,Window,0,Strip{0.5f,1,1,0.25f,0.75f,0,1});
    CHECK(v.size()==4 && nearly(v[0].u,0.25f) && nearly(v[2].u,0.75f) && nearly(v[0].x/v[0].w,0.0f) && nearly(v[2].x/v[2].w,0.4f),"a strip off to the side shows the columns it was given");
    buildStrip(v,Eye,fov,Window,0,Strip{0,0,1});
    CHECK(v.empty(),"a strip with no width draws nothing");

    // A curved strip is cut finely, and every point of it lies on the cylinder.
    const float R=2.5f;
    buildStrip(v,Eye,fov,Window,R,Strip{0,4,2});
    CHECK(v.size()==static_cast<size_t>(2*(Columns+1)),"a curved strip is cut into columns");
    {
        bool onCylinder=true,monotone=true; float widest=0;
        for (int i=0;i<=Columns;++i) {
            const float x=-2+4*static_cast<float>(i)/Columns;
            const auto p=surfacePoint(Window,R,x,0.7f);
            // The axis runs through (0,*,0) here: the window is R in front of it.
            onCylinder&=nearly(std::hypot(p.x-0,p.z-0),R,1e-3f) && nearly(p.y,Window.position.y+0.7f);
            if (i>0) monotone&=v[static_cast<size_t>(2*i)].u>v[static_cast<size_t>(2*i-2)].u;
            widest=std::max(widest,std::fabs(v[static_cast<size_t>(2*i)].x/v[static_cast<size_t>(2*i)].w));
        }
        CHECK(onCylinder,"every point of a curved strip is the cylinder's radius from its axis");
        CHECK(monotone,"the texture runs left to right across a curved strip");
        CHECK(nearly(v[0].u,0) && nearly(v[static_cast<size_t>(2*Columns)].u,1),"the whole texture is used");
        // Mirror image: column i and column n-i are equally far off the centre line, one each side.
        bool mirrored=true;
        for (int i=0;i<=Columns;++i) {
            const auto& a=v[static_cast<size_t>(2*i)]; const auto& b=v[static_cast<size_t>(2*(Columns-i))];
            mirrored&=nearly(a.x/a.w,-b.x/b.w,1e-3f) && nearly(a.w,b.w,1e-3f);
        }
        CHECK(mirrored,"a curved strip in front of the viewer is symmetric");
        // With the viewer on the axis the whole screen is at the same distance, so the edges are drawn taller than they
        // would be on a flat screen of the same width: they are not further away.
        CHECK(nearly(v[0].w,R*std::cos(2.0f/R),1e-3f) && v[0].w<R,"with the viewer on the axis the curve's edge is nearer along the view direction than its middle");
    }
    // A strip longer than the cylinder can hold is trimmed, and its texture with it.
    buildStrip(v,Eye,fov,Window,1.0f,Strip{0,100,1});
    CHECK(!v.empty() && nearly(v.front().u,0.5f-MaxStripArc*1.0f/100,1e-3f) && nearly(v[v.size()-2].u,0.5f+MaxStripArc*1.0f/100,1e-3f),"an over-long strip is trimmed to the arc limit");
    // Behind the viewer is clipped; to one side of the head is shifted the right way.
    CHECK(project(Eye,fov,{0,0,1},0,0).z<0,"a point behind the eye is clipped");
    const XrPosef right{{0,0,0,1},{0.03f,0,0}};
    CHECK(project(right,fov,{0,0,-2.5f},0,0).x<project(Eye,fov,{0,0,-2.5f},0,0).x,"an eye further right sees the same point further left");
    // An off-centre field of view (as the headset's eyes have) moves the picture the other way.
    const XrFovf lopsided{-0.9f,0.5f,0.6f,-0.6f};
    CHECK(project(Eye,lopsided,{0,0,-2.5f},0,0).x>0,"a narrower inner field of view pushes the centre towards the outer edge");
    // Turning the head turns the picture.
    const XrPosef turned{{0,std::sin(0.2f),0,std::cos(0.2f)},{0,0,0}};      // looking 0.4 rad to the left
    CHECK(project(turned,fov,{0,0,-2.5f},0,0).x>0,"with the head turned left the centre of the screen is to the right");
}

// --- Rendering on WARP -------------------------------------------------------------------------------------------------
struct Rig {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> snapshot,target,staging; ComPtr<ID3D11RenderTargetView> rtv;
    static constexpr UINT PW=256,PH=144,TS=512;
    curved::Renderer renderer;
    bool hardware=false;
    bool init() {
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDevice(nullptr,hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context))) return false;
        // The picture, side by side for two eyes. Left eye: red / green over blue / yellow. Right eye: all of that with
        // a white frame, so the eyes can be told apart.
        std::vector<uint32_t> px(static_cast<size_t>(PW)*2*PH);
        for (UINT y=0;y<PH;++y) for (UINT x=0;x<PW*2;++x) {
            const UINT ex=x%PW,eye=x/PW;
            const bool right=ex>=PW/2,bottom=y>=PH/2;
            uint32_t c=!bottom ? (right ? 0xff00ff00u : 0xff0000ffu) : (right ? 0xff00ffffu : 0xffff0000u);   // RGBA bytes: green / red / yellow / blue
            if (eye==1 && (ex<3 || ex>=PW-3 || y<3 || y>=PH-3)) c=0xffffffffu;
            if (eye==1 && (ex%32==0 || y%32==0)) c=0xff000000u|((c>>1)&0x7f7f7fu);   // a faint grid on the right eye only, to judge straightness by
            px[static_cast<size_t>(y)*PW*2+x]=c;
        }
        D3D11_TEXTURE2D_DESC d{}; d.Width=PW*2; d.Height=PH; d.MipLevels=1; d.ArraySize=1; d.SampleDesc.Count=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.Usage=D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA init{px.data(),PW*2*4,0};
        if (FAILED(device->CreateTexture2D(&d,&init,&snapshot))) return false;
        D3D11_TEXTURE2D_DESC t{}; t.Width=t.Height=TS; t.MipLevels=1; t.ArraySize=1; t.SampleDesc.Count=1; t.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS; t.Usage=D3D11_USAGE_DEFAULT; t.BindFlags=D3D11_BIND_RENDER_TARGET;
        if (FAILED(device->CreateTexture2D(&t,nullptr,&target))) return false;
        D3D11_RENDER_TARGET_VIEW_DESC r{}; r.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; r.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateRenderTargetView(target.Get(),&r,&rtv))) return false;
        t.Usage=D3D11_USAGE_STAGING; t.BindFlags=0; t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&t,nullptr,&staging))) return false;
        if (!renderer.initialize(device.Get(),context.Get())) return false;
        return renderer.configure(PW,PH,DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    // Draws one eye and returns the pixels (RGBA bytes, packed little-endian).
    std::vector<uint32_t> render(UINT eye,const XrPosef& view,float half,float radius,const curved::Strip& picture,const curved::Strip* glow) {
        for (UINT e=0;e<2;++e) { const D3D11_BOX box{e*PW,0,0,e*PW+PW,PH,1}; renderer.copyPicture(e,snapshot.Get(),box); }
        renderer.draw(rtv.Get(),TS,TS,eye,view,fovOf(half),Window,radius,picture,glow);
        context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Map the test target");
        std::vector<uint32_t> out(static_cast<size_t>(TS)*TS);
        for (UINT y=0;y<TS;++y) std::memcpy(&out[static_cast<size_t>(y)*TS],static_cast<const uint8_t*>(m.pData)+static_cast<size_t>(y)*m.RowPitch,TS*4);
        context->Unmap(staging.Get(),0);
        return out;
    }
};
static uint32_t at(const std::vector<uint32_t>& p,int x,int y) { return p[static_cast<size_t>(y)*Rig::TS+static_cast<size_t>(x)]|0xff000000u; }
// Where a point at (x,y) metres on a screen facing the viewer 2.5 m ahead lands in the image, for a view of the given half angle.
static void pixelOf(float x,float y,float half,int& px,int& py) {
    const float t=std::tan(half);
    px=static_cast<int>(((x/2.5f)/t*0.5f+0.5f)*Rig::TS); py=static_cast<int>((0.5f-(y/2.5f)/t*0.5f)*Rig::TS);
}
// The first and last rows in a column that are not black.
static bool extent(const std::vector<uint32_t>& p,int x,int& top,int& bottom) {
    top=-1; bottom=-1;
    for (int y=0;y<static_cast<int>(Rig::TS);++y) if ((p[static_cast<size_t>(y)*Rig::TS+static_cast<size_t>(x)]&0x00ffffffu)!=0) { if (top<0) top=y; bottom=y; }
    return top>=0;
}

static void rendering(const char* dumpTo,bool hardware) {
    Rig rig; rig.hardware=hardware;
    if (!rig.init()) { std::cout<<"SKIP curved screen rendering: no WARP device or no shader compiler on this PC\n"; return; }
    const float half=0.6f;
    const curved::Strip full{0,3.2f,1.8f};                        // the whole picture: 3.2 m wide, 16:9
    auto dump=[&](const char* name,const std::vector<uint32_t>& px) { if (dumpTo) writeBmp((std::string(dumpTo)+"/"+name).c_str(),px,Rig::TS,Rig::TS); };
    int px,py;

    // Flat: the four colours are where they should be, and nothing is drawn outside the picture.
    const auto flat=rig.render(0,Eye,half,0,full,nullptr);
    dump("flat.bmp",flat);
    pixelOf(-0.7f,0.45f,half,px,py); CHECK(at(flat,px,py)==0xff0000ffu,"flat: top left is red");
    pixelOf(0.7f,0.45f,half,px,py);  CHECK(at(flat,px,py)==0xff00ff00u,"flat: top right is green");
    pixelOf(-0.7f,-0.45f,half,px,py);CHECK(at(flat,px,py)==0xffff0000u,"flat: bottom left is blue");
    pixelOf(0.7f,-0.45f,half,px,py); CHECK(at(flat,px,py)==0xff00ffffu,"flat: bottom right is yellow");
    CHECK(at(flat,4,4)==0xff000000u && at(flat,Rig::TS/2,4)==0xff000000u,"flat: outside the picture is black");
    int t0,b0,t1,b1,t2,b2;
    extent(flat,Rig::TS/2,t0,b0); extent(flat,70,t1,b1); extent(flat,Rig::TS-70,t2,b2);
    CHECK(t0==t1 && t0==t2 && b0==b1 && b0==b2,"flat: a screen facing you has the same height all the way across");
    // The other eye shows its own picture, with its white frame.
    const auto flat1=rig.render(1,Eye,half,0,full,nullptr);
    pixelOf(1.59f,0.0f,half,px,py); CHECK(at(flat1,px,py)==0xffffffffu,"flat: the right eye shows the right eye's picture");
    CHECK(at(flat,px,py)!=0xffffffffu,"flat: the left eye does not");

    // Floating window: showing only the columns 10..100% moves the picture's left edge in.
    const curved::Strip floating{0.16f,2.88f,1.8f,0.1f,1.0f,0,1};
    const auto flt=rig.render(0,Eye,half,0,floating,nullptr);
    pixelOf(-1.5f,0.0f,half,px,py); CHECK(at(flt,px,py)==0xff000000u && at(flat,px,py)!=0xff000000u,"floating window: the left edge of the picture is hidden");

    // Curved: same colours in the same places, but now the picture is bent round the viewer, so its edges are nearer and
    // drawn taller than its middle, and the whole thing is still symmetric about the centre.
    const auto bent=rig.render(0,Eye,half,2.5f,full,nullptr);
    dump("curved.bmp",bent);
    pixelOf(0,0.45f,half,px,py);
    CHECK(at(bent,Rig::TS/2-60,Rig::TS/2-40)==0xff0000ffu && at(bent,Rig::TS/2+60,Rig::TS/2-40)==0xff00ff00u &&
          at(bent,Rig::TS/2-60,Rig::TS/2+40)==0xffff0000u && at(bent,Rig::TS/2+60,Rig::TS/2+40)==0xff00ffffu,"curved: the four colours are where they should be");
    extent(bent,Rig::TS/2,t0,b0); extent(bent,70,t1,b1); extent(bent,Rig::TS-70,t2,b2);
    CHECK(t0>=0 && t1>=0 && t2>=0,"curved: the picture reaches both sides");
    CHECK((b1-t1)>(b0-t0)+6 && (b2-t2)>(b0-t0)+6,"curved: the edges are nearer, so taller, than the middle");
    CHECK(std::abs((b1-t1)-(b2-t2))<=2 && std::abs((t1+b1)-(t2+b2))<=2,"curved: the two sides are mirror images");
    // A screen that wraps right round (curvature 1) is clipped where it passes behind the viewer, not drawn through them.
    const auto wrapped=rig.render(0,Eye,half,1.2f,curved::Strip{0,8,1.8f},nullptr);
    dump("wrapped.bmp",wrapped);
    CHECK(at(wrapped,Rig::TS/2,Rig::TS/2)!=0xff000000u,"wrapped: the middle of the screen is drawn");

    // The glow: a bright wash around the picture, strongest right at its edge, gone far away, never over the picture.
    fx::GlowState white; white.w=2; white.h=2; white.rgb.assign(2*2*3,255.0f);
    rig.renderer.setGlow(white,1.0f,16.0f/9.0f);
    CHECK(rig.renderer.hasGlow(),"glow: a reading was accepted");
    const curved::Strip halo{0,3.2f*(16.0f/9.0f+1.1f)/(16.0f/9.0f),1.8f*2.1f};
    const auto lit=rig.render(0,Eye,half,0,full,&halo);
    dump("glow.bmp",lit);
    pixelOf(0,0.9f,half,px,py);
    const uint32_t justAbove=at(lit,px,py-4),farAbove=at(lit,px,std::max(py-60,0));
    CHECK((justAbove&255)>(farAbove&255) && (justAbove&255)>100,"glow: brighter right at the picture's edge than further out");
    CHECK(at(lit,Rig::TS/2-60,Rig::TS/2-40)==0xff0000ffu,"glow: the picture is drawn over it, untouched");
    rig.renderer.clearGlow();
    CHECK(at(rig.render(0,Eye,half,0,full,&halo),px,py-4)==0xff000000u,"glow: nothing is drawn once it is cleared");
    // Curved with the glow: the same picture, now with a wash that bends with it.
    rig.renderer.setGlow(white,1.0f,16.0f/9.0f);
    const auto bentLit=rig.render(0,Eye,half,2.5f,full,&halo);
    dump("curved-glow.bmp",bentLit);
    CHECK(at(bentLit,Rig::TS/2-60,Rig::TS/2-40)==0xff0000ffu,"curved glow: the picture is still on top");
    {
        std::string adapter="?";
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> a; DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(rig.device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&a)) && SUCCEEDED(a->GetDesc(&desc))) {
            adapter.clear(); for (const wchar_t* c=desc.Description;*c;++c) adapter+=static_cast<char>(*c<128 ? *c : '?');
        }
        std::cout<<"rendered on "<<(hardware ? "the graphics card" : "WARP")<<" ("<<adapter<<")\n";
    }
}

int main(int argc,char** argv) {
    try {
        geometry();
        const char* dumpTo=nullptr; bool hardware=false;
        for (int i=1;i<argc;++i) { if (std::strcmp(argv[i],"--dump")==0 && i+1<argc) dumpTo=argv[++i]; else if (std::strcmp(argv[i],"--hardware")==0) hardware=true; }
        rendering(dumpTo,hardware);
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
    if (failures) { std::cerr<<failures<<" curved screen check(s) failed\n"; return 1; }
    std::cout<<"PASS curved screen: geometry, texture orientation, flat and curved extents, floating window, glow\n";
    return 0;
}
