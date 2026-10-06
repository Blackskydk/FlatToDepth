#pragma once
#include "common.hpp"

// Legacy Katanga ABI: first DWORD is a DXGI KMT handle, not pixel data.
// There is no verified producer fence/frame-id protocol in this interface.
class KatangaSource {
    HANDLE mapping_ = nullptr;
    const volatile DWORD* handle_ = nullptr;
    DWORD current_ = 0;
    std::wstring mappingName_;
    std::chrono::steady_clock::time_point nextPoll_{};
public:
    ComPtr<ID3D11Texture2D> texture;
    explicit KatangaSource(std::wstring mappingName=L"Local\\KatangaMappedFile"):mappingName_(std::move(mappingName)) {}
    KatangaSource(const KatangaSource&) = delete;
    KatangaSource& operator=(const KatangaSource&) = delete;
    ~KatangaSource() { closeMapping(); }
    void closeMapping() {
        if (handle_) UnmapViewOfFile(const_cast<const DWORD*>(handle_));
        if (mapping_) CloseHandle(mapping_);
        handle_=nullptr; mapping_=nullptr;
    }
    bool poll(ID3D11Device* device) {
        const auto now=std::chrono::steady_clock::now();
        if (now<nextPoll_) return false;
        nextPoll_=now+std::chrono::milliseconds(500);
        // Reopen to detect producer restart with a fresh named mapping.
        closeMapping();
        mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName_.c_str());
        if (mapping_) handle_=static_cast<const volatile DWORD*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,sizeof(DWORD)));
        const DWORD candidate=handle_ ? *handle_ : 0;
        if (!candidate) {
            const bool changed=texture!=nullptr;
            if (changed) log("Katanga export disconnected; submitting no layers");
            current_=0; texture.Reset(); return changed;
        }
        if (candidate==current_ && texture) return false;
        ComPtr<ID3D11Texture2D> opened;
        const auto result=device->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(candidate)),IID_PPV_ARGS(&opened));
        if (FAILED(result)) {
            log("OpenSharedResource handle="+hex(candidate)+" HRESULT="+hex(static_cast<uint32_t>(result))+"; check same GPU and exporter lifetime");
            current_=0; const bool changed=texture!=nullptr; texture.Reset(); return changed;
        }
        D3D11_TEXTURE2D_DESC d{}; opened->GetDesc(&d);
        StereoLayout::from(d);
        require(!(d.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX), "Unexpected keyed-mutex exporter; its key protocol must be verified before consumption");
        log("Katanga full-SBS handle="+hex(candidate)+" dimensions="+std::to_string(d.Width)+"x"+std::to_string(d.Height)+
            " DXGI_format="+std::to_string(d.Format)+" misc="+hex(d.MiscFlags));
        log("Legacy export has no verified cross-process fence; producer tearing/stale frames remain an acceptance check");
        texture=opened; current_=candidate; return true;
    }
};
