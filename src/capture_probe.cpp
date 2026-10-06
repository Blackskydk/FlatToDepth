#include "source.hpp"
#include <cstring>

int main(int argc,char** argv) {
    startLog("capture-probe.log");
    try {
        const double seconds=argc>1 ? std::stod(argv[1]) : 15.0;
        require(std::isfinite(seconds)&&seconds>0,"Duration must be positive");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Capture probe D3D11 device");
        ComPtr<IDXGIDevice> dxgi; hr(device.As(&dxgi),"Get DXGI device");
        ComPtr<IDXGIAdapter> adapter; hr(dxgi->GetAdapter(&adapter),"Get probe adapter");
        DXGI_ADAPTER_DESC adapterDesc{}; hr(adapter->GetDesc(&adapterDesc),"Get probe adapter description");
        log("Capture probe adapter LUID="+hex(static_cast<uint32_t>(adapterDesc.AdapterLuid.HighPart))+":"+hex(adapterDesc.AdapterLuid.LowPart));
        KatangaSource source; const auto start=std::chrono::steady_clock::now();
        log("Waiting for real Geo-11 export. Diagnostic pixel readback only; normal FlatToDepth rendering stays on GPU.");
        while (std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds) {
            source.poll(device.Get());
            if (source.texture) {
                D3D11_TEXTURE2D_DESC d{}; source.texture->GetDesc(&d); const auto layout=StereoLayout::from(d);
                d.MiscFlags=0; d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ComPtr<ID3D11Texture2D> staging; hr(device->CreateTexture2D(&d,nullptr,&staging),"Probe staging");
                context->CopyResource(staging.Get(),source.texture.Get());
                D3D11_MAPPED_SUBRESOURCE m{}; hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Probe source readback");
                uint64_t differences=0;
                for (UINT y=0;y<layout.height;++y) {
                    auto row=static_cast<const uint8_t*>(m.pData)+static_cast<size_t>(y)*m.RowPitch;
                    for (UINT x=0;x<layout.width;++x) differences += std::memcmp(row+x*4,row+(x+layout.width)*4,4)!=0;
                }
                context->Unmap(staging.Get(),0);
                log("Captured actual full-SBS eye="+std::to_string(layout.width)+"x"+std::to_string(layout.height)+
                    " differing_eye_pixels="+std::to_string(differences)+" / "+std::to_string(static_cast<uint64_t>(layout.width)*layout.height));
                log(differences ? "Separate eye content found; stereo geometry/order/comfort still need headset validation" :
                    "Eyes identical at this instant; menu/black frame is possible. Repeat during visible gameplay");
                if (argc>2) {
                    // Optional picture of the whole export (both eyes side by side, 1/8 scale) to look at the framing.
                    context->CopyResource(staging.Get(),source.texture.Get());
                    hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m),"Probe picture readback");
                    const UINT w=layout.width*2/8,h=layout.height/8;
                    std::vector<uint8_t> bmp(54+static_cast<size_t>(w)*h*4);
                    auto put32=[&](size_t at,uint32_t v) { for (int i=0;i<4;++i) bmp[at+i]=static_cast<uint8_t>(v>>(8*i)); };
                    bmp[0]='B'; bmp[1]='M'; put32(2,static_cast<uint32_t>(bmp.size())); put32(10,54); put32(14,40);
                    put32(18,w); put32(22,static_cast<uint32_t>(-static_cast<int32_t>(h))); bmp[26]=1; bmp[28]=32; put32(34,w*h*4);
                    const bool bgra=d.Format==DXGI_FORMAT_B8G8R8A8_UNORM || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                    for (UINT y=0;y<h;++y) for (UINT x=0;x<w;++x) {
                        const uint8_t* p=static_cast<const uint8_t*>(m.pData)+static_cast<size_t>(y*8)*m.RowPitch+static_cast<size_t>(x)*8*4;
                        uint8_t* o=&bmp[54+(static_cast<size_t>(y)*w+x)*4];
                        o[0]=bgra ? p[0] : p[2]; o[1]=p[1]; o[2]=bgra ? p[2] : p[0]; o[3]=255;
                    }
                    context->Unmap(staging.Get(),0);
                    std::ofstream(argv[2],std::ios::binary).write(reinterpret_cast<const char*>(bmp.data()),static_cast<std::streamsize>(bmp.size()));
                    log(std::string("Wrote export picture ")+argv[2]);
                }
                return 0;
            }
            Sleep(50);
        }
        log("ERROR: no Katanga GPU export received before timeout; check Geo-11 installation and katanga_vr mode"); return 1;
    } catch (const std::exception& e) { log("ERROR: "+std::string(e.what())); return 1; }
}
