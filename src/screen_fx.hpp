#pragma once
#include "ui_atlas.hpp"
#include "common.hpp"

// Two things that make the screen sit better in the room: the ambient glow (a soft wash of the picture's own colours
// around the screen, instead of a black void) and the floating window (each eye hides a sliver of one edge so the
// screen's edges appear nearer than the picture, which stops things that pop out of the screen being cut off by it).
namespace fx {

// --- Floating window ---------------------------------------------------------------------------------------------
// A thing in front of the screen is seen further to the right by the left eye than by the right eye. If the picture's
// own edges are to read as in front of the screen, the left eye must see its left edge further in and the right eye its
// right edge further in. Hiding `shift` source pixels at one edge of each eye does exactly that.
inline UINT floatShift(UINT visibleWidth,float fraction) {
    if (!(fraction>0) || visibleWidth<8) return 0;
    return std::min<UINT>(static_cast<UINT>(std::lround(static_cast<float>(visibleWidth)*fraction)),visibleWidth/8);
}
struct EyeSlice {
    UINT x=0,w=0;           // columns of this eye's picture that are shown
    float centre=0,width=0; // where that slice sits along the screen (metres from the middle, unrolled) and how wide it is
};
inline EyeSlice eyeSlice(UINT eye,UINT visibleWidth,UINT shift,float screenWidth) {
    EyeSlice s;
    s.x=eye==0 ? shift : 0; s.w=visibleWidth-shift;
    const float perPixel=screenWidth/static_cast<float>(visibleWidth);
    s.width=static_cast<float>(s.w)*perPixel;
    s.centre=(eye==0 ? 0.5f : -0.5f)*static_cast<float>(shift)*perPixel;
    return s;
}

// --- Ambient glow ------------------------------------------------------------------------------------------------
constexpr uint32_t GlowTex=128;        // the glow texture is GlowTex x GlowTex; it is a soft gradient, so it stays small and cheap to redraw
constexpr float GlowMargin=0.55f;      // how far the glow reaches beyond each edge, in picture heights
constexpr float GlowGap=0.3f;          // metres behind the picture that the glow layer sits, see glowPlacement()
constexpr float GlowSettleSeconds=1.5f;// how long the glow takes to follow the picture's colours (a time constant, not a delay)

// The glow layer used to lie in exactly the picture's plane, and the compositor cannot say which of two coplanar layers is
// in front, so it can pick differently from one frame to the next. The glow now sits GlowGap behind the picture and is
// grown by the same ratio, so it covers the same angles as before.
struct GlowPlacement { float back; float grow; };
inline GlowPlacement glowPlacement(float distance) {
    distance=std::max(distance,0.5f);
    return {GlowGap,(distance+GlowGap)/distance};
}
// How far a reading `seconds` after the previous one moves the glow towards the picture's colours. The glow follows the
// scene's overall mood, not its frames, so it is smoothed over about a second and a half: a lively scene must not make
// the whole background flash.
inline float glowBlend(float seconds) {
    return 1-std::exp(-std::clamp(seconds,0.0f,1.0f)/GlowSettleSeconds);
}

// Average colour of the picture on a small grid, smoothed over time so a flickering scene does not flicker the room.
struct GlowState {
    uint32_t w=0,h=0;
    std::vector<float> rgb;            // w*h cells of R,G,B in 0..255
    bool valid() const { return w>0 && h>0 && rgb.size()==static_cast<size_t>(w)*h*3; }
    // Folds in a new reading of cw*ch RGBA/BGRA pixels; cells move `blend` of the way to it (1 replaces them).
    // Returns the largest change in any channel, so the caller can skip re-rendering when nothing moved.
    float update(const uint8_t* data,uint32_t pitch,uint32_t cw,uint32_t ch,bool bgra,float blend) {
        if (!data || cw==0 || ch==0) return 0;
        const bool fresh=!(valid() && w==cw && h==ch);
        if (fresh) { w=cw; h=ch; rgb.assign(static_cast<size_t>(cw)*ch*3,0.0f); blend=1; }
        float change=0;
        for (uint32_t y=0;y<ch;++y) for (uint32_t x=0;x<cw;++x) {
            const uint8_t* p=data+static_cast<size_t>(y)*pitch+static_cast<size_t>(x)*4;
            const float target[3]{static_cast<float>(bgra ? p[2] : p[0]),static_cast<float>(p[1]),static_cast<float>(bgra ? p[0] : p[2])};
            float* cell=&rgb[(static_cast<size_t>(y)*cw+x)*3];
            for (int c=0;c<3;++c) { const float next=cell[c]+(target[c]-cell[c])*blend; change=std::max(change,std::fabs(next-cell[c])); cell[c]=next; }
        }
        return fresh ? 255.0f : change;
    }
    // Largest difference in any channel of any cell; 255 when the grids are not comparable.
    float distance(const GlowState& other) const {
        if (!valid() || !other.valid() || w!=other.w || h!=other.h) return 255.0f;
        float d=0;
        for (size_t i=0;i<rgb.size();++i) d=std::max(d,std::fabs(rgb[i]-other.rgb[i]));
        return d;
    }
    // Bilinear sample at fractional cell coordinates (cell centres at +0.5), clamped to the grid.
    void sample(float gx,float gy,float out[3]) const {
        gx=std::clamp(gx-0.5f,0.0f,static_cast<float>(w-1)); gy=std::clamp(gy-0.5f,0.0f,static_cast<float>(h-1));
        const uint32_t x0=static_cast<uint32_t>(gx),y0=static_cast<uint32_t>(gy),x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
        const float fx=gx-static_cast<float>(x0),fy=gy-static_cast<float>(y0);
        for (int c=0;c<3;++c) {
            auto at=[&](uint32_t x,uint32_t y) { return rgb[(static_cast<size_t>(y)*w+x)*3+c]; };
            out[c]=(at(x0,y0)*(1-fx)+at(x1,y0)*fx)*(1-fy)+(at(x0,y1)*(1-fx)+at(x1,y1)*fx)*fy;
        }
    }
};

// Renders the glow texture for a screen of the given width:height aspect. The texture covers the picture plus
// GlowMargin picture-heights on every side; outside the picture each point takes the colour of the nearest part of the
// picture's edge and fades away with distance. Straight alpha in sRGB, like the other overlay textures.
inline void renderGlow(std::vector<uint32_t>& px,const GlowState& state,float strength,float aspect,bool bgra) {
    px.assign(static_cast<size_t>(GlowTex)*GlowTex,0);
    if (!state.valid() || aspect<=0) return;
    strength=std::clamp(strength,0.0f,1.0f);
    const float spanX=aspect+2*GlowMargin,spanY=1+2*GlowMargin;
    for (uint32_t ty=0;ty<GlowTex;++ty) for (uint32_t tx=0;tx<GlowTex;++tx) {
        const float qx=((static_cast<float>(tx)+0.5f)/static_cast<float>(GlowTex)-0.5f)*spanX,qy=(0.5f-(static_cast<float>(ty)+0.5f)/static_cast<float>(GlowTex))*spanY;
        const float dx=std::max(std::fabs(qx)-aspect/2,0.0f),dy=std::max(std::fabs(qy)-0.5f,0.0f);
        const float reach=std::min(std::sqrt(dx*dx+dy*dy)/GlowMargin,1.0f);
        // Behind the picture the glow is fully transparent (its colour is kept so the edge blends smoothly): the picture
        // hides that part anyway, and if the compositor ever drew the glow over the picture instead of under it, a see-through
        // middle means nothing is wrong with the picture. Only the wash around the edges is ever visible.
        const bool behindPicture=dx<=0 && dy<=0;
        const float alpha=behindPicture ? 0.0f : strength*(1-reach)*(1-reach);
        if (!behindPicture && alpha<=0.002f) continue;
        const float cx=std::clamp(qx,-aspect/2,aspect/2),cy=std::clamp(qy,-0.5f,0.5f);
        float rgb[3]; state.sample((cx/aspect+0.5f)*static_cast<float>(state.w),(0.5f-cy)*static_cast<float>(state.h),rgb);
        ui::detail::Px p{rgb[0],rgb[1],rgb[2],alpha};
        px[static_cast<size_t>(ty)*GlowTex+tx]=ui::detail::pack(p,bgra,1.0f);
    }
}

// Averages the picture on the GPU: copies it into a texture with a mip chain, lets the GPU build the chain, and reads
// back one small level (about a dozen rows) through two staging textures without ever waiting for the GPU. A reading
// therefore arrives a few frames after it was taken, which is fine for a glow. If the GPU cannot build the chain for
// the picture's format, configure() says so and the glow is simply unavailable.
class GlowSampler {
public:
    // The level whose height is closest to a dozen rows, never fewer than ten (unless the picture is smaller).
    static UINT levelFor(UINT height) {
        UINT level=0;
        while ((height>>(level+1))>=10) ++level;
        return level;
    }
    bool ready() const { return mips_ && srv_ && staging_[0] && staging_[1]; }
    bool bgra() const { return bgra_; }
    UINT cellsWide() const { return cellsW_; }
    UINT cellsHigh() const { return cellsH_; }
    void reset() {
        mips_.Reset(); srv_.Reset(); staging_[0].Reset(); staging_[1].Reset();
        queued_=0; tail_=0; width_=height_=0; format_=DXGI_FORMAT_UNKNOWN;
    }
    // (Re)creates the textures for a picture of this size and format; false means the glow is unavailable.
    bool configure(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format) {
        if (ready() && width==width_ && height==height_ && format==format_) return true;
        reset();
        UINT support=0;
        if (FAILED(device->CheckFormatSupport(format,&support)) ||
            !(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
            log("Ambient glow unavailable: this GPU cannot build mip levels for DXGI format "+std::to_string(format)); return false;
        }
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width; d.Height=height; d.MipLevels=0; d.ArraySize=1; d.Format=format; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET; d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(device->CreateTexture2D(&d,nullptr,&mips_)) || FAILED(device->CreateShaderResourceView(mips_.Get(),nullptr,&srv_))) {
            log("Ambient glow unavailable: could not create the averaging texture"); reset(); return false;
        }
        level_=levelFor(height); cellsW_=std::max(1u,width>>level_); cellsH_=std::max(1u,height>>level_);
        D3D11_TEXTURE2D_DESC s{};
        s.Width=cellsW_; s.Height=cellsH_; s.MipLevels=1; s.ArraySize=1; s.Format=format; s.SampleDesc.Count=1;
        s.Usage=D3D11_USAGE_STAGING; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        for (auto& st:staging_) if (FAILED(device->CreateTexture2D(&s,nullptr,&st))) { log("Ambient glow unavailable: could not create the readback texture"); reset(); return false; }
        width_=width; height_=height; format_=format;
        bgra_=format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        log("Ambient glow sampling "+std::to_string(cellsW_)+"x"+std::to_string(cellsH_)+" cells from "+std::to_string(width)+"x"+std::to_string(height)+" (mip level "+std::to_string(level_)+")");
        return true;
    }
    // Queues one reading of `box` (width x height as configured) of `source`. Skipped if two are still unread.
    void capture(ID3D11DeviceContext* context,ID3D11Texture2D* source,const D3D11_BOX& box) {
        if (!ready() || queued_>=2 || box.right-box.left!=width_ || box.bottom-box.top!=height_) return;
        context->CopySubresourceRegion(mips_.Get(),0,0,0,0,source,0,&box);
        context->GenerateMips(srv_.Get());
        context->CopySubresourceRegion(staging_[(tail_+queued_)%2].Get(),0,0,0,0,mips_.Get(),level_,nullptr);
        ++queued_;
    }
    // Folds the oldest finished reading into `state` if the GPU has it ready (never waits). Returns the change, or a
    // negative number when there was nothing to read yet.
    float read(ID3D11DeviceContext* context,GlowState& state,float blend) {
        if (!ready() || queued_==0) return -1;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result=context->Map(staging_[tail_].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
        if (result==DXGI_ERROR_WAS_STILL_DRAWING) return -1;
        if (FAILED(result)) { log("Ambient glow readback failed HRESULT="+hex(static_cast<uint32_t>(result))); queued_=0; return -1; }
        const float change=state.update(static_cast<const uint8_t*>(mapped.pData),mapped.RowPitch,cellsW_,cellsH_,bgra_,blend);
        context->Unmap(staging_[tail_].Get(),0);
        tail_=(tail_+1)%2; --queued_;
        return change;
    }
private:
    ComPtr<ID3D11Texture2D> mips_,staging_[2];
    ComPtr<ID3D11ShaderResourceView> srv_;
    UINT width_=0,height_=0,level_=0,cellsW_=0,cellsH_=0,queued_=0,tail_=0;
    DXGI_FORMAT format_=DXGI_FORMAT_UNKNOWN;
    bool bgra_=false;
};

}
