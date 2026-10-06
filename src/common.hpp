#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
inline std::ofstream logFile;
inline void log(const std::string& s) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::cout << '[' << ms << " ms] " << s << std::endl;
    if (logFile) logFile << '[' << ms << " ms] " << s << std::endl;
}
inline void startLog(const char* name) {
    std::filesystem::create_directories("logs");
    logFile.open(std::filesystem::path("logs") / name, std::ios::trunc);
}
inline std::string hex(uint64_t n) { std::ostringstream s; s << "0x" << std::hex << n; return s.str(); }
inline void hr(HRESULT result, const char* call) {
    if (FAILED(result)) throw std::runtime_error(std::string(call) + " HRESULT=" + hex(static_cast<uint32_t>(result)));
}
inline void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

struct StereoLayout {
    UINT width, height;       // one eye of the export
    UINT cropX=0, shown=0;    // visible part of each eye; shown==0 means all of it
    static StereoLayout from(const D3D11_TEXTURE2D_DESC& d) {
        require(d.Width >= 2 && d.Width % 2 == 0 && d.Height > 0, "Full SBS requires positive dimensions and even width");
        require(d.ArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1, "Expected one non-MSAA, single-mip SBS texture");
        require(d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
                "Unsupported source format; MVP accepts RGBA8/BGRA8 only");
        return {d.Width / 2, d.Height};
    }
    // A 16:9 game on an ultrawide desktop is exported with black bars either side. Keep only the centred part
    // with the given width:height aspect (0 keeps everything). Pictures within 2% of it are left alone.
    StereoLayout cropped(double aspect) const {
        StereoLayout l = *this; l.cropX = 0; l.shown = 0;
        if (aspect > 0 && width > height * aspect * 1.02) {
            const UINT w = static_cast<UINT>(std::lround(height * aspect)) & ~1u;
            l.shown = w; l.cropX = (width - w) / 2;
        }
        return l;
    }
    UINT visibleWidth() const { return shown ? shown : width; }
    D3D11_BOX box(UINT eye, bool swap = false) const {
        const UINT left = (swap ? 1 - eye : eye) * width + cropX;
        return {left, 0, 0, left + visibleWidth(), height, 1};
    }
};
// Deliberate binocular disparity: border/grid at window depth; near and far blocks.
// Red single stripe identifies left; cyan double stripe identifies right.
inline std::vector<uint32_t> calibration(UINT eyeWidth, UINT height) {
    const UINT fullWidth = 2 * eyeWidth;
    std::vector<uint32_t> p(static_cast<size_t>(fullWidth) * height);
    for (UINT eye=0; eye<2; ++eye) for (UINT y=0; y<height; ++y) for (UINT x=0; x<eyeWidth; ++x) {
        uint32_t c = 0xff181008;
        if (x % 64 < 2 || y % 64 < 2) c = 0xff484848;
        if (x<4 || y<4 || x+4>=eyeWidth || y+4>=height) c=0xffdddddd;
        const int nearX = static_cast<int>(eyeWidth/3) + (eye==0 ? 8 : -8);
        const int farX = static_cast<int>(2*eyeWidth/3) + (eye==0 ? -8 : 8);
        if (std::abs(static_cast<int>(x)-nearX)<28 && std::abs(static_cast<int>(y)-static_cast<int>(height/2))<48) c=0xff30b0ff;
        if (std::abs(static_cast<int>(x)-farX)<28 && std::abs(static_cast<int>(y)-static_cast<int>(height/2))<48) c=0xffff9030;
        if (y>16 && y<48 && ((x>16 && x<32) || (eye==1 && x>40 && x<56))) c=eye==0 ? 0xff3030ff : 0xffffff30;
        p[static_cast<size_t>(y)*fullWidth+eye*eyeWidth+x]=c;
    }
    return p;
}
inline ComPtr<ID3D11Texture2D> testTexture(ID3D11Device* device, UINT eyeWidth=1024, UINT height=576) {
    auto pixels=calibration(eyeWidth,height);
    D3D11_TEXTURE2D_DESC d{};
    d.Width=eyeWidth*2; d.Height=height; d.MipLevels=1; d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), d.Width*4, 0};
    ComPtr<ID3D11Texture2D> texture;
    hr(device->CreateTexture2D(&d,&data,&texture),"Create calibration texture");
    return texture;
}
