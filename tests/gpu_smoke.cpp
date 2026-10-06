#include "source.hpp"
#include "screen_fx.hpp"
#include <cstring>

// Hardware-only: tests a second D3D device opening the Katanga mapping,
// copies both eyes, and checks every pixel including the swapped-eye path.
int main(int argc,char** argv) {
    startLog("gpu-smoke.log");
    HANDLE mapping=nullptr; DWORD* mapped=nullptr;
    try {
        ComPtr<ID3D11Device> producer,consumer;
        ComPtr<ID3D11DeviceContext> pc,cc;
        hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&producer,nullptr,&pc),"Producer device");
        ComPtr<IDXGIDevice> dxgi; hr(producer.As(&dxgi),"Producer DXGI device");
        ComPtr<IDXGIAdapter> adapter; hr(dxgi->GetAdapter(&adapter),"Producer adapter");
        hr(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&consumer,nullptr,&cc),"Consumer device");
        const bool publish=argc>1 && std::strcmp(argv[1],"--publish")==0;
        const UINT w=publish ? 1024 : 192, h=publish ? 576 : 128;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=w*2; desc.Height=h; desc.ArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        const auto pixels=calibration(w,h);
        D3D11_SUBRESOURCE_DATA data{pixels.data(),w*8,0};
        ComPtr<ID3D11Texture2D> shared; hr(producer->CreateTexture2D(&desc,&data,&shared),"Create shared SBS");
        pc->Flush();
        // Test publisher finishes its initial upload before the consumer reads.
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT,0};
        ComPtr<ID3D11Query> uploaded; hr(producer->CreateQuery(&queryDesc,&uploaded),"Create upload query");
        pc->End(uploaded.Get()); pc->Flush(); BOOL complete=FALSE;
        const auto uploadStart=std::chrono::steady_clock::now();
        while (pc->GetData(uploaded.Get(),&complete,sizeof(complete),0)==S_FALSE) {
            require(std::chrono::steady_clock::now()-uploadStart<std::chrono::seconds(5),"GPU upload timed out"); Sleep(1);
        }
        ComPtr<IDXGIResource> resource; hr(shared.As(&resource),"Shared resource");
        HANDLE gpuHandle=nullptr; hr(resource->GetSharedHandle(&gpuHandle),"GetSharedHandle");
        require(reinterpret_cast<uintptr_t>(gpuHandle)<=UINT32_MAX,"Unexpected non-DWORD KMT handle");
        const std::wstring mappingName=publish ? L"Local\\KatangaMappedFile" : L"Local\\FlatToDepthGpuSmoke_"+std::to_wstring(GetCurrentProcessId());
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,8,mappingName.c_str());
        require(mapping!=nullptr,"Create Katanga mapping failed");
        require(GetLastError()!=ERROR_ALREADY_EXISTS,"Katanga export already exists; close other stereo exporters before the synthetic test");
        mapped=static_cast<DWORD*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,8)); require(mapped!=nullptr,"Map test mapping failed");
        mapped[1]=0; InterlockedExchange(reinterpret_cast<volatile LONG*>(mapped),static_cast<LONG>(reinterpret_cast<uintptr_t>(gpuHandle)));
        log("Synthetic Katanga handle="+hex(reinterpret_cast<uintptr_t>(gpuHandle))+" full-SBS="+std::to_string(w*2)+"x"+std::to_string(h));
        if (publish) {
            log("Publishing static stereo pattern for 60 seconds; start FlatToDepth in default capture mode now"); Sleep(60000);
        } else {
            KatangaSource source(mappingName); require(source.poll(consumer.Get()) && source.texture,"Katanga source failed to import texture");
            const auto layout=StereoLayout::from(desc); require(layout.width==w && layout.height==h,"Bad full-SBS dimensions");
            auto invalid=desc; invalid.Width=383;
            bool rejected=false; try { StereoLayout::from(invalid); } catch (const std::exception&) { rejected=true; }
            require(rejected,"Odd SBS width was accepted");
            invalid=desc; invalid.SampleDesc.Count=4; rejected=false;
            try { StereoLayout::from(invalid); } catch (const std::exception&) { rejected=true; }
            require(rejected,"MSAA SBS source was accepted");
            auto snapshotDesc=desc; snapshotDesc.BindFlags=0; snapshotDesc.MiscFlags=0;
            ComPtr<ID3D11Texture2D> snapshot; hr(consumer->CreateTexture2D(&snapshotDesc,nullptr,&snapshot),"Create snapshot");
            cc->CopyResource(snapshot.Get(),source.texture.Get());
            auto eyeDesc=snapshotDesc; eyeDesc.Width=w; eyeDesc.Usage=D3D11_USAGE_STAGING; eyeDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> readback; hr(consumer->CreateTexture2D(&eyeDesc,nullptr,&readback),"Create test readback");
            for (bool swap:{false,true}) for (UINT eye=0;eye<2;++eye) {
                const auto box=layout.box(eye,swap); cc->CopySubresourceRegion(readback.Get(),0,0,0,0,snapshot.Get(),0,&box);
                D3D11_MAPPED_SUBRESOURCE m{}; hr(cc->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"Read back split eye");
                bool matches=true;
                for (UINT y=0;y<h;++y) {
                    const auto row=static_cast<const uint8_t*>(m.pData)+static_cast<size_t>(y)*m.RowPitch;
                    const auto expected=pixels.data()+static_cast<size_t>(y)*w*2+box.left;
                    matches &= std::memcmp(row,expected,w*4)==0;
                }
                cc->Unmap(readback.Get(),0); require(matches,"Eye pixel comparison failed");
                log("PASS eye="+std::to_string(eye)+" swap="+std::to_string(swap)+" all pixels match");
            }
            log("PASS shared GPU import, snapshot, full-SBS split, swapped eyes, invalid layout rejection");
            // The ambient glow averages the picture on the GPU (mip chain) and reads one small level back without
            // waiting. A picture that is red on the left and blue on the right must come back as exactly that.
            {
                const UINT gw=1920,gh=1080;
                std::vector<uint32_t> colours(static_cast<size_t>(gw)*gh);
                for (UINT y=0;y<gh;++y) for (UINT x=0;x<gw;++x) colours[static_cast<size_t>(y)*gw+x]=x<gw/2 ? 0xff2828c8u : 0xffc82828u;   // RGBA bytes: (200,40,40) / (40,40,200)
                auto pictureDesc=desc; pictureDesc.Width=gw; pictureDesc.Height=gh; pictureDesc.BindFlags=0; pictureDesc.MiscFlags=0;
                D3D11_SUBRESOURCE_DATA init{colours.data(),gw*4,0};
                ComPtr<ID3D11Texture2D> picture; hr(consumer->CreateTexture2D(&pictureDesc,&init,&picture),"Create glow test picture");
                fx::GlowSampler sampler;
                require(sampler.configure(consumer.Get(),gw,gh,DXGI_FORMAT_R8G8B8A8_UNORM),"Glow sampler unavailable: the GPU cannot build mip levels for RGBA8");
                require(sampler.cellsWide()==30 && sampler.cellsHigh()==16,"Glow sampler picked an unexpected mip level");
                const D3D11_BOX whole{0,0,0,gw,gh,1};
                sampler.capture(cc.Get(),picture.Get(),whole); cc->Flush();
                fx::GlowState state; float change=-1; const auto glowStart=std::chrono::steady_clock::now();
                while ((change=sampler.read(cc.Get(),state,1.0f))<0) {
                    require(std::chrono::steady_clock::now()-glowStart<std::chrono::seconds(5),"Glow readback timed out"); Sleep(1);
                }
                require(state.valid() && state.w==30 && state.h==16,"Glow grid has the wrong size");
                auto cell=[&](UINT x,UINT y,int c) { return state.rgb[(static_cast<size_t>(y)*30+x)*3+c]; };
                for (UINT y:{2u,8u,13u}) {
                    require(std::fabs(cell(4,y,0)-200)<=4 && std::fabs(cell(4,y,1)-40)<=4 && std::fabs(cell(4,y,2)-40)<=4,"Glow: the left of the picture is not red");
                    require(std::fabs(cell(25,y,0)-40)<=4 && std::fabs(cell(25,y,1)-40)<=4 && std::fabs(cell(25,y,2)-200)<=4,"Glow: the right of the picture is not blue");
                }
                // A second reading while nothing is queued has nothing to give, and two can be queued at once.
                require(sampler.read(cc.Get(),state,1.0f)<0,"Glow sampler returned a reading that was never taken");
                sampler.capture(cc.Get(),picture.Get(),whole); sampler.capture(cc.Get(),picture.Get(),whole); sampler.capture(cc.Get(),picture.Get(),whole); cc->Flush();
                int readings=0; const auto drainStart=std::chrono::steady_clock::now();
                while (readings<2) {
                    if (sampler.read(cc.Get(),state,1.0f)>=0) ++readings;
                    else { require(std::chrono::steady_clock::now()-drainStart<std::chrono::seconds(5),"Glow readback timed out"); Sleep(1); }
                }
                Sleep(50); require(sampler.read(cc.Get(),state,1.0f)<0,"Glow sampler queued a third reading it should have dropped");
                log("PASS ambient glow sampler: mip chain average and non-blocking readback of a red/blue picture");
            }
        }
        InterlockedExchange(reinterpret_cast<volatile LONG*>(mapped),0);
        UnmapViewOfFile(mapped); CloseHandle(mapping); return 0;
    } catch (const std::exception& e) {
        log("ERROR: "+std::string(e.what()));
        if (mapped) { InterlockedExchange(reinterpret_cast<volatile LONG*>(mapped),0); UnmapViewOfFile(mapped); }
        if (mapping) CloseHandle(mapping); return 1;
    }
}
