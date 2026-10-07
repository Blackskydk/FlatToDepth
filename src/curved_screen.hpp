#pragma once
#include "screen_fx.hpp"
#include "window_control.hpp"
#include <d3dcompiler.h>
#include <cstring>

// The curved screen, drawn by FlatToDepth itself.
//
// OpenXR's cylinder layer is an optional extension and SteamVR does not have it (nor any other curved layer type), so on
// such a runtime the screen is drawn here instead: each eye's picture is texture-mapped onto a patch of a vertical
// cylinder and rendered into a full-view projection layer, from the very eye poses the runtime gave for this frame. The
// ambient glow is drawn into the same image, behind the picture, so a curved screen has no separate glow layer.
//
// The geometry is the one the rest of the program already uses for a curved screen (see onScreen() in window_control.hpp):
// positions on the screen are "unrolled", x being the arc length from the window's centre line and y the height, and the
// cylinder's axis runs through window-space (0, *, radius), towards the viewer.
namespace curved {

constexpr int Columns=128;              // a curved strip is cut into this many columns
constexpr float NearPlane=0.05f;        // metres: anything nearer than this to the eye is clipped
constexpr float MaxStripArc=3.0f;       // radians either side of the centre line: a strip never wraps right round and overlaps itself

// One corner of the mesh: a clip-space position (not yet divided by w) and a texture coordinate.
struct Vertex { float x,y,z,w,u,v; };

// A patch of the screen: where it lies along it and how big it is (unrolled metres), and which part of its texture it shows.
struct Strip {
    float centre=0,width=0,height=0;
    float u0=0,u1=1,v0=0,v1=1;          // texture columns left to right, rows top to bottom
};

// The point on the screen at unrolled (x,y), in the space the window pose is expressed in.
inline XrVector3f surfacePoint(const XrPosef& window,float radius,float x,float y) {
    if (radius<=0) return pose::add(window.position,pose::rotate(window.orientation,{x,y,0}));
    const float phi=x/radius;
    return pose::add(window.position,pose::rotate(window.orientation,{radius*std::sin(phi),y,radius*(1-std::cos(phi))}));
}

// Takes a point in the same space as the eye's pose to clip space for that eye. The depth coordinates are arranged so that
// the GPU clips at NearPlane and so that texture coordinates stay perspective-correct however the strip is turned.
inline Vertex project(const XrPosef& view,const XrFovf& fov,XrVector3f world,float u,float v) {
    const XrVector3f p=pose::unrotate(view.orientation,pose::sub(world,view.position));   // view space: x right, y up, -z ahead
    const float depth=-p.z;
    const float tl=std::tan(fov.angleLeft),tr=std::tan(fov.angleRight),tu=std::tan(fov.angleUp),td=std::tan(fov.angleDown);
    return {(2*p.x-(tr+tl)*depth)/(tr-tl),(2*p.y-(tu+td)*depth)/(tu-td),depth-NearPlane,depth,u,v};
}

// The triangle strip for one patch: a column of two vertices (top, bottom) per step along the screen. A flat screen needs
// just two columns, a curved one is cut finely so it reads as a smooth curve. Nothing is produced for a patch that has
// no width.
inline void buildStrip(std::vector<Vertex>& out,const XrPosef& view,const XrFovf& fov,const XrPosef& window,float radius,const Strip& s) {
    out.clear();
    float left=s.centre-s.width/2,right=s.centre+s.width/2,u0=s.u0,u1=s.u1;
    if (!(right>left)) return;
    if (radius>0) {   // never further round than MaxStripArc each side, the texture trimmed by the same amount
        const float limit=MaxStripArc*radius;
        const float l=std::max(left,-limit),r=std::min(right,limit);
        if (!(r>l)) return;
        const float span=right-left,a=s.u0+(s.u1-s.u0)*(l-left)/span,b=s.u0+(s.u1-s.u0)*(r-left)/span;
        left=l; right=r; u0=a; u1=b;
    }
    const int columns=radius>0 ? Columns : 1;
    for (int i=0;i<=columns;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(columns);
        const float x=left+(right-left)*t,u=u0+(u1-u0)*t;
        out.push_back(project(view,fov,surfacePoint(window,radius,x, s.height/2),u,s.v0));
        out.push_back(project(view,fov,surfacePoint(window,radius,x,-s.height/2),u,s.v1));
    }
}

// --- The renderer ---------------------------------------------------------------------------------------------------
class Renderer {
public:
    bool ready() const { return vs_ && ps_ && layout_; }
    bool hasGlow() const { return hasGlow_; }

    // Compiles the shaders and makes the fixed state. False means this PC cannot draw it (the curved screen is then
    // simply not offered).
    bool initialize(ID3D11Device* device,ID3D11DeviceContext* context) {
        device_=device; context_=context;
        static const char* source=
            "Texture2D tex : register(t0);\n"
            "SamplerState smp : register(s0);\n"
            "struct VIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
            "struct VOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
            "VOut vs(VIn i) { VOut o; o.pos = i.pos; o.uv = i.uv; return o; }\n"
            "float4 ps(VOut i) : SV_Target { return tex.Sample(smp, i.uv); }\n";
        auto compile=[&](const char* entry,const char* profile,ComPtr<ID3DBlob>& blob) {
            ComPtr<ID3DBlob> errors;
            const HRESULT result=D3DCompile(source,std::strlen(source),"curved_screen",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&errors);
            if (FAILED(result)) {
                log(std::string("Curved screen unavailable: the shader did not compile (")+hex(static_cast<uint32_t>(result))+")"+
                    (errors ? std::string(": ")+static_cast<const char*>(errors->GetBufferPointer()) : std::string()));
                return false;
            }
            return true;
        };
        ComPtr<ID3DBlob> vsBlob,psBlob;
        if (!compile("vs","vs_4_0",vsBlob) || !compile("ps","ps_4_0",psBlob)) return false;
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=static_cast<UINT>(sizeof(Vertex)*2*(Columns+1)); bd.Usage=D3D11_USAGE_DYNAMIC;
        bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=FALSE; dd.StencilEnable=FALSE;
        D3D11_BLEND_DESC bo{}; bo.RenderTarget[0].BlendEnable=FALSE; bo.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        D3D11_BLEND_DESC ba=bo; ba.RenderTarget[0].BlendEnable=TRUE; ba.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA; ba.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        ba.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD; ba.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE; ba.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO; ba.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        D3D11_TEXTURE2D_DESC gd{}; gd.Width=gd.Height=fx::GlowTex; gd.MipLevels=1; gd.ArraySize=1; gd.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; gd.SampleDesc.Count=1;
        gd.Usage=D3D11_USAGE_DEFAULT; gd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const bool good=
            SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(),vsBlob->GetBufferSize(),nullptr,&vs_)) &&
            SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(),psBlob->GetBufferSize(),nullptr,&ps_)) &&
            SUCCEEDED(device->CreateInputLayout(elements,2,vsBlob->GetBufferPointer(),vsBlob->GetBufferSize(),&layout_)) &&
            SUCCEEDED(device->CreateBuffer(&bd,nullptr,&vertices_)) && SUCCEEDED(device->CreateSamplerState(&sd,&sampler_)) &&
            SUCCEEDED(device->CreateRasterizerState(&rd,&raster_)) && SUCCEEDED(device->CreateDepthStencilState(&dd,&noDepth_)) &&
            SUCCEEDED(device->CreateBlendState(&bo,&opaque_)) && SUCCEEDED(device->CreateBlendState(&ba,&alpha_)) &&
            SUCCEEDED(device->CreateTexture2D(&gd,nullptr,&glow_)) && SUCCEEDED(device->CreateShaderResourceView(glow_.Get(),nullptr,&glowView_));
        if (!good) { log("Curved screen unavailable: could not create the drawing state"); releaseAll(); return false; }
        return true;
    }

    // (Re)makes the two eye pictures for a visible picture of this size cut from a source of this format.
    bool configure(UINT width,UINT height,DXGI_FORMAT sourceFormat) {
        if (!ready()) return false;
        if (pictures_[0] && pictures_[1] && width==width_ && height==height_ && sourceFormat==sourceFormat_) return true;
        resetPictures();
        const bool bgra=sourceFormat==DXGI_FORMAT_B8G8R8A8_UNORM || sourceFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || sourceFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS;
        D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height; d.MipLevels=1; d.ArraySize=1; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.Format=bgra ? DXGI_FORMAT_B8G8R8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_TYPELESS; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=bgra ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=1;
        for (int eye=0;eye<2;++eye) {
            if (FAILED(device_->CreateTexture2D(&d,nullptr,&pictures_[eye])) || FAILED(device_->CreateShaderResourceView(pictures_[eye].Get(),&v,&pictureViews_[eye]))) {
                log("Curved screen: could not create the picture textures"); resetPictures(); return false;
            }
        }
        width_=width; height_=height; sourceFormat_=sourceFormat;
        return true;
    }
    void resetPictures() { for (int i=0;i<2;++i) { pictures_[i].Reset(); pictureViews_[i].Reset(); } width_=height_=0; sourceFormat_=DXGI_FORMAT_UNKNOWN; }
    void reset() { resetPictures(); hasGlow_=false; }

    // Takes this eye's part of the game's picture. The box is the eye's visible area in the snapshot.
    void copyPicture(UINT eye,ID3D11Texture2D* snapshot,const D3D11_BOX& box) {
        if (!pictures_[eye] || box.right-box.left!=width_ || box.bottom-box.top!=height_) return;
        context_->CopySubresourceRegion(pictures_[eye].Get(),0,0,0,0,snapshot,0,&box);
    }

    // The glow, from the same colours the flat screen's glow layer uses.
    void setGlow(const fx::GlowState& state,float strength,float aspect) {
        if (!ready() || !state.valid()) return;
        fx::renderGlow(glowPixels_,state,strength,aspect,false);
        context_->UpdateSubresource(glow_.Get(),0,nullptr,glowPixels_.data(),fx::GlowTex*4,0);
        hasGlow_=true;
    }
    void clearGlow() { hasGlow_=false; }

    // Draws one eye: the glow (if given and there is one), then the picture, onto a black background, into the target.
    void draw(ID3D11RenderTargetView* target,UINT width,UINT height,UINT eye,const XrPosef& view,const XrFovf& fov,
        const XrPosef& window,float radius,const Strip& picture,const Strip* glow) {
        if (!ready() || !pictureViews_[eye] || !target) return;
        const float black[4]{0,0,0,1};
        context_->OMSetRenderTargets(1,&target,nullptr);
        context_->ClearRenderTargetView(target,black);
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context_->RSSetViewports(1,&viewport);
        context_->RSSetState(raster_.Get());
        context_->OMSetDepthStencilState(noDepth_.Get(),0);
        context_->IASetInputLayout(layout_.Get());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context_->VSSetShader(vs_.Get(),nullptr,0);
        context_->PSSetShader(ps_.Get(),nullptr,0);
        ID3D11SamplerState* samplers[]{sampler_.Get()}; context_->PSSetSamplers(0,1,samplers);
        const float factor[4]{};
        if (glow && hasGlow_) {
            context_->OMSetBlendState(alpha_.Get(),factor,0xffffffff);
            drawStrip(glowView_.Get(),view,fov,window,radius,*glow);
        }
        context_->OMSetBlendState(opaque_.Get(),factor,0xffffffff);
        drawStrip(pictureViews_[eye].Get(),view,fov,window,radius,picture);
        ID3D11ShaderResourceView* none[]{nullptr}; context_->PSSetShaderResources(0,1,none);
        ID3D11RenderTargetView* noTarget[]{nullptr}; context_->OMSetRenderTargets(1,noTarget,nullptr);
    }

private:
    void drawStrip(ID3D11ShaderResourceView* texture,const XrPosef& view,const XrFovf& fov,const XrPosef& window,float radius,const Strip& strip) {
        buildStrip(scratch_,view,fov,window,radius,strip);
        if (scratch_.size()<4) return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(vertices_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return;
        std::memcpy(mapped.pData,scratch_.data(),scratch_.size()*sizeof(Vertex));
        context_->Unmap(vertices_.Get(),0);
        const UINT stride=sizeof(Vertex),offset=0; ID3D11Buffer* buffers[]{vertices_.Get()};
        context_->IASetVertexBuffers(0,1,buffers,&stride,&offset);
        ID3D11ShaderResourceView* views[]{texture}; context_->PSSetShaderResources(0,1,views);
        context_->Draw(static_cast<UINT>(scratch_.size()),0);
    }
    void releaseAll() {
        vs_.Reset(); ps_.Reset(); layout_.Reset(); vertices_.Reset(); sampler_.Reset(); raster_.Reset(); noDepth_.Reset();
        opaque_.Reset(); alpha_.Reset(); glow_.Reset(); glowView_.Reset(); resetPictures();
    }
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vertices_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> noDepth_;
    ComPtr<ID3D11BlendState> opaque_,alpha_;
    ComPtr<ID3D11Texture2D> pictures_[2],glow_;
    ComPtr<ID3D11ShaderResourceView> pictureViews_[2],glowView_;
    std::vector<Vertex> scratch_;
    std::vector<uint32_t> glowPixels_;
    UINT width_=0,height_=0;
    DXGI_FORMAT sourceFormat_=DXGI_FORMAT_UNKNOWN;
    bool hasGlow_=false;
};

}
