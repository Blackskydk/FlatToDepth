#pragma once
#include "picker_art.hpp"
#include "tools_art.hpp"
#include "screen_fx.hpp"

// One OpenXR swapchain holding a CPU-rendered texture. The texture is re-uploaded only when its look changes, so
// idle frames cost nothing. The runtime has no colour-scale extension to fade layers with, which is why fading is
// done by re-rendering the alpha.
class OverlaySheet {
    XrSwapchain chain_=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> images_;
    ComPtr<ID3D11Texture2D> staging_;
    ComPtr<ID3D11DeviceContext> context_;
    bool bgra_=false;
public:
    bool ready() const { return chain_!=XR_NULL_HANDLE; }
    bool bgra() const { return bgra_; }
    XrSwapchain handle() const { return chain_; }
    void initialize(XrSession session,ID3D11Device* device,ID3D11DeviceContext* context,const std::vector<int64_t>& formats,
        uint32_t width,uint32_t height,const char* name) {
        auto has=[&](DXGI_FORMAT f) { return std::find(formats.begin(),formats.end(),static_cast<int64_t>(f))!=formats.end(); };
        DXGI_FORMAT format;
        if (has(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        else if (has(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) { format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; bgra_=true; }
        else { log(std::string(name)+" disabled: runtime offers no 8-bit sRGB swapchain format"); return; }
        context_=context;
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width; d.Height=height; d.MipLevels=1; d.ArraySize=1; d.Format=format; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        hr(device->CreateTexture2D(&d,nullptr,&staging_),"Create overlay staging texture");
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=format; ci.sampleCount=1; ci.width=width; ci.height=height; ci.faceCount=1; ci.arraySize=1; ci.mipCount=1;
        XR(xrCreateSwapchain(session,&ci,&chain_));
        uint32_t n=0; XR(xrEnumerateSwapchainImages(chain_,0,&n,nullptr));
        images_.assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        XR(xrEnumerateSwapchainImages(chain_,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images_.data())));
        log(std::string(name)+" swapchain="+std::to_string(width)+"x"+std::to_string(height)+" DXGI_format="+std::to_string(format));
    }
    void destroy() {
        if (chain_) xrDestroySwapchain(chain_);
        chain_=XR_NULL_HANDLE; images_.clear(); staging_.Reset(); context_.Reset();
    }
    // Copies width*height packed pixels into the next swapchain image and releases it to the compositor.
    void upload(const std::vector<uint32_t>& pixels,uint32_t width) {
        context_->UpdateSubresource(staging_.Get(),0,nullptr,pixels.data(),width*4,0);
        uint32_t index=0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; XR(xrAcquireSwapchainImage(chain_,&ai,&index));
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout=XR_INFINITE_DURATION;
        XR(xrWaitSwapchainImage(chain_,&wi));
        context_->CopyResource(images_.at(index).texture,staging_.Get());
        context_->Flush();
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; XR(xrReleaseSwapchainImage(chain_,&ri));
    }
};

// The shared atlas: grab bar, resize handle, lasers and cursors.
class UiOverlay {
    OverlaySheet sheet_;
    std::vector<uint32_t> pixels_;
    ui::Visual shown_{};
    bool uploaded_=false;
public:
    bool ready() const { return sheet_.ready(); }
    XrSwapchain handle() const { return sheet_.handle(); }
    void initialize(XrSession session,ID3D11Device* device,ID3D11DeviceContext* context,const std::vector<int64_t>& formats) {
        sheet_.initialize(session,device,context,formats,ui::AtlasW,ui::AtlasH,"UI overlay");
    }
    void destroy() { sheet_.destroy(); uploaded_=false; }
    // Call before submitting layers that reference the swapchain. No-op when nothing changed.
    void upload(const ui::Visual& v) {
        if (!ready() || (uploaded_ && v==shown_)) return;
        ui::render(pixels_,v,sheet_.bgra());
        sheet_.upload(pixels_,ui::AtlasW);
        shown_=v; uploaded_=true;
    }
};

// The game menu panel.
class PickerOverlay {
    OverlaySheet sheet_;
    std::vector<uint32_t> pixels_;
    PickerVisual shown_{};
    bool uploaded_=false;
public:
    bool ready() const { return sheet_.ready(); }
    XrSwapchain handle() const { return sheet_.handle(); }
    void initialize(XrSession session,ID3D11Device* device,ID3D11DeviceContext* context,const std::vector<int64_t>& formats) {
        sheet_.initialize(session,device,context,formats,picker::TexW,picker::TexH,"Game menu");
    }
    void destroy() { sheet_.destroy(); uploaded_=false; }
    void upload(const PickerVisual& v) {
        if (!ready() || (uploaded_ && v==shown_)) return;
        picker::render(pixels_,v,sheet_.bgra());
        sheet_.upload(pixels_,picker::TexW);
        shown_=v; uploaded_=true;
    }
};

// The tools panel.
class ToolsOverlay {
    OverlaySheet sheet_;
    std::vector<uint32_t> pixels_;
    ToolsVisual shown_{};
    bool uploaded_=false;
public:
    bool ready() const { return sheet_.ready(); }
    XrSwapchain handle() const { return sheet_.handle(); }
    void initialize(XrSession session,ID3D11Device* device,ID3D11DeviceContext* context,const std::vector<int64_t>& formats) {
        sheet_.initialize(session,device,context,formats,tools::TexW,tools::TexH,"Tools panel");
    }
    void destroy() { sheet_.destroy(); uploaded_=false; }
    void upload(const ToolsVisual& v) {
        if (!ready() || (uploaded_ && v==shown_)) return;
        tools::render(pixels_,v,sheet_.bgra());
        sheet_.upload(pixels_,tools::TexW);
        shown_=v; uploaded_=true;
    }
};

// The ambient glow: a soft wash of the picture's colours, drawn behind the screen.
class GlowOverlay {
    OverlaySheet sheet_;
    std::vector<uint32_t> pixels_;
    bool uploaded_=false;
public:
    bool ready() const { return sheet_.ready(); }
    bool hasContent() const { return uploaded_; }
    // The next game's picture is not this one's: hide the glow until a reading of the new picture arrives.
    void invalidate() { uploaded_=false; }
    XrSwapchain handle() const { return sheet_.handle(); }
    void initialize(XrSession session,ID3D11Device* device,ID3D11DeviceContext* context,const std::vector<int64_t>& formats) {
        sheet_.initialize(session,device,context,formats,fx::GlowTex,fx::GlowTex,"Ambient glow");
    }
    void destroy() { sheet_.destroy(); uploaded_=false; }
    // Re-renders from the latest colours; the caller decides when they have changed enough to bother.
    void upload(const fx::GlowState& state,float strength,float aspect) {
        if (!ready() || !state.valid()) return;
        fx::renderGlow(pixels_,state,strength,aspect,sheet_.bgra());
        sheet_.upload(pixels_,fx::GlowTex);
        uploaded_=true;
    }
};
