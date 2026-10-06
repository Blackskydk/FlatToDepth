#include "source.hpp"
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <array>
#include <atomic>
#include <conio.h>
#include <cstring>
#include <memory>

static std::atomic<bool> stop{false};
static BOOL WINAPI consoleHandler(DWORD type) {
    if (type==CTRL_C_EVENT || type==CTRL_BREAK_EVENT || type==CTRL_CLOSE_EVENT) { stop=true; return TRUE; }
    return FALSE;
}
static XrInstance diagnosticInstance=XR_NULL_HANDLE;
static void xrCheck(XrResult r, const char* call) {
    if (XR_FAILED(r)) {
        char text[XR_MAX_RESULT_STRING_SIZE]{};
        if (diagnosticInstance) xrResultToString(diagnosticInstance,r,text);
        throw std::runtime_error(std::string(call)+" OpenXR="+std::to_string(r)+" "+text);
    }
}
#define XR(call) xrCheck((call),#call)
#include "xr_input.hpp"
#include "ui_overlay.hpp"
#include "process_watch.hpp"
#include "config.hpp"
#include "keys.hpp"
#include "screen_layer.hpp"
#include "scan.hpp"

struct EyeChain {
    XrSwapchain handle=XR_NULL_HANDLE;
    std::vector<XrSwapchainImageD3D11KHR> images;
    void destroy() { if (handle) xrDestroySwapchain(handle); handle=XR_NULL_HANDLE; images.clear(); }
};
class App {
    XrInstance instance_=XR_NULL_HANDLE;
    XrSystemId system_=XR_NULL_SYSTEM_ID;
    XrSession session_=XR_NULL_HANDLE;
    XrSpace local_=XR_NULL_HANDLE, head_=XR_NULL_HANDLE;
    XrSystemProperties systemProps_{XR_TYPE_SYSTEM_PROPERTIES};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> snapshot_;
    std::array<EyeChain,2> chains_;
    StereoLayout layout_{};
    std::vector<int64_t> formats_;
    Config config_;
    std::unique_ptr<ControllerInput> input_;
    WindowControl windowControl_;
    UiOverlay overlay_;
    PickerOverlay pickerOverlay_;
    ToolsOverlay toolsOverlay_;
    GlowOverlay glowOverlay_;
    ToolsPanel toolsPanel_;
    fx::GlowSampler glowSampler_;
    fx::GlowState glow_,glowShown_;
    KeyPresser keys_;
    WindowControl::Mode lastMode_=WindowControl::Mode::Idle;
    UiTarget lastHover_[2]{UiTarget::None,UiTarget::None};
    XrTime lastFrameTime_=0;
    bool restorePlacement_=true,recenterHeld_=false,testMode_=false;
    // The curved screen: needs the runtime's cylinder layer. curveBase_ is the radius that suits the screen's distance
    // from you when it was last placed or moved (0 = flat); it does not follow your head around afterwards.
    bool cylinderOk_=false;
    float curveBase_=0;
    XrPosef headPose_{{0,0,0,1},{0,0,0}};
    bool toolsChordHeld_=false,swapChordHeld_=false;
    std::wstring toolsStatus_; bool toolsWarn_=false;
    std::chrono::steady_clock::time_point toolsStatusUntil_{},nextGlowUpload_{},nextRumbleCheck_{};
    bool running_=false, exit_=false, recenter_=true;
    XrTime pendingRecenter_=0;
    XrPosef screen_{{0,0,0,1},{0,0,-2.5f}};
    XrEnvironmentBlendMode blend_=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    // The game menu. In launcher mode the bridge starts on the menu, launches the chosen game through Steam, shows
    // that game with its own profile, and returns to the menu when the game closes.
    enum class Phase { Choose, Launching, Active };
    std::filesystem::path mainConfig_;
    bool launcher_=false,needRebuild_=false,noticeWarn_=false;
    Phase phase_=Phase::Active;
    int game_=-1;
    GamePicker picker_;
    std::wstring notice_;
    // The menu lists the games Steam says are installed, then a few that are not (greyed) so a short list still shows what
    // else is supported. steamKnown_ is false when Steam could not be found at all: every game is then offered, and Steam
    // is left to say no.
    std::vector<SteamApp> steamApps_;
    bool steamKnown_=true;
    std::vector<size_t> menuGames_;     // menu entry -> index into Games
    std::vector<char> menuEnabled_;     // menu entry -> can be started
    std::chrono::steady_clock::time_point launchDeadline_{},nextGameCheck_{},nextInstalledCheck_{};
    // The glow, two game eyes, the bar, the handle, the tools button, a beam and cursor per hand, the tools panel and its
    // pointers; or the menu panel with the same pointers. The runtime may offer fewer, see layerCap in frame().
    static constexpr uint32_t MaxLayers=16;
public:
    // game>=0 fixes the profile (no menu); launcher=true starts on the menu.
    App(const Config& c,std::filesystem::path mainConfig,bool launcher,int game):config_(c),mainConfig_(std::move(mainConfig)),launcher_(launcher),game_(game) {
        windowControl_.configure(windowSettings(false));
        rebuildMenu();
    }
    // Shown in place of the menu's hint until something is chosen.
    void notify(const std::wstring& text,bool warn) { notice_=text; noticeWarn_=warn; }
    ~App() {
        if (input_) input_->shutdown();
        keys_.releaseNow();
        overlay_.destroy(); pickerOverlay_.destroy(); toolsOverlay_.destroy(); glowOverlay_.destroy();
        for (auto& c:chains_) c.destroy();
        if (head_) xrDestroySpace(head_);
        if (local_) xrDestroySpace(local_);
        if (session_) xrDestroySession(session_);
        diagnosticInstance=XR_NULL_HANDLE;
        if (instance_) xrDestroyInstance(instance_);
    }
    void initialize(bool probe) {
        uint32_t count=0;
        XR(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr));
        std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});
        XR(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,extensions.data()));
        bool d3d=false, refresh=false,frameProfile=false,cylinder=false;
        for (const auto& e:extensions) {
            log("Extension "+std::string(e.extensionName)+" version="+std::to_string(e.extensionVersion));
            d3d|=std::strcmp(e.extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME)==0;
            refresh|=std::strcmp(e.extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)==0;
            frameProfile|=std::strcmp(e.extensionName,"XR_VALVE_frame_controller_interaction")==0;
            cylinder|=std::strcmp(e.extensionName,XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)==0;
        }
        require(d3d,"Runtime does not support XR_KHR_D3D11_enable");
        std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        if (refresh) enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        if (frameProfile) enabled.push_back("XR_VALVE_frame_controller_interaction");
        if (cylinder) enabled.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
        cylinderOk_=cylinder;
        if (!cylinder) log("Runtime has no XR_KHR_composition_layer_cylinder: the screen stays flat");
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName,"FlatToDepth");
        strcpy_s(ci.applicationInfo.engineName,"FlatToDepth native D3D11");
        ci.applicationInfo.applicationVersion=1; ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        ci.enabledExtensionCount=static_cast<uint32_t>(enabled.size()); ci.enabledExtensionNames=enabled.data();
        XR(xrCreateInstance(&ci,&instance_)); diagnosticInstance=instance_;
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        XR(xrGetInstanceProperties(instance_,&props));
        log("Runtime="+std::string(props.runtimeName)+" version="+std::to_string(XR_VERSION_MAJOR(props.runtimeVersion))+"."+
            std::to_string(XR_VERSION_MINOR(props.runtimeVersion))+"."+std::to_string(XR_VERSION_PATCH(props.runtimeVersion)));
        XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO}; gi.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XR(xrGetSystem(instance_,&gi,&system_));
        XR(xrGetSystemProperties(instance_,system_,&systemProps_));
        log("Headset="+std::string(systemProps_.systemName)+" max_swapchain="+std::to_string(systemProps_.graphicsProperties.maxSwapchainImageWidth)+
            "x"+std::to_string(systemProps_.graphicsProperties.maxSwapchainImageHeight)+" max_layers="+std::to_string(systemProps_.graphicsProperties.maxLayerCount));
        require(systemProps_.graphicsProperties.maxLayerCount>=2,"Runtime needs at least two quad layers");
        XR(xrEnumerateViewConfigurations(instance_,system_,0,&count,nullptr));
        std::vector<XrViewConfigurationType> configs(count);
        XR(xrEnumerateViewConfigurations(instance_,system_,count,&count,configs.data()));
        bool stereo=false;
        for (auto c:configs) { log("Supported view configuration="+std::to_string(c)); stereo|=c==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; }
        require(stereo,"Runtime does not support PRIMARY_STEREO");
        XR(xrEnumerateViewConfigurationViews(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr));
        require(count==2,"PRIMARY_STEREO must have two views");
        std::vector<XrViewConfigurationView> views(count,{XR_TYPE_VIEW_CONFIGURATION_VIEW});
        XR(xrEnumerateViewConfigurationViews(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,views.data()));
        for (UINT i=0;i<count;++i) log("Recommended eye "+std::to_string(i)+"="+std::to_string(views[i].recommendedImageRectWidth)+"x"+
            std::to_string(views[i].recommendedImageRectHeight)+" samples="+std::to_string(views[i].recommendedSwapchainSampleCount));
        XR(xrEnumerateEnvironmentBlendModes(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr));
        std::vector<XrEnvironmentBlendMode> modes(count);
        XR(xrEnumerateEnvironmentBlendModes(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()));
        require(std::find(modes.begin(),modes.end(),blend_)!=modes.end(),"MVP requires opaque environment blend mode");
        if (probe) return;
        PFN_xrGetD3D11GraphicsRequirementsKHR requirements=nullptr;
        XR(xrGetInstanceProcAddr(instance_,"xrGetD3D11GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)));
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        XR(requirements(instance_,system_,&req));
        ComPtr<IDXGIFactory1> factory; hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
        ComPtr<IDXGIAdapter1> chosen;
        for (UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT result=factory->EnumAdapters1(i,&adapter);
            if (result==DXGI_ERROR_NOT_FOUND) break;
            hr(result,"EnumAdapters1");
            DXGI_ADAPTER_DESC1 d{}; hr(adapter->GetDesc1(&d),"GetDesc1");
            if (d.AdapterLuid.HighPart==req.adapterLuid.HighPart && d.AdapterLuid.LowPart==req.adapterLuid.LowPart) { chosen=adapter; break; }
        }
        require(chosen!=nullptr,"No DXGI adapter matches OpenXR LUID");
        const D3D_FEATURE_LEVEL candidates[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
        std::vector<D3D_FEATURE_LEVEL> levels;
        for (auto l:candidates) if (l>=req.minFeatureLevel) levels.push_back(l);
        require(!levels.empty(),"Unsupported runtime minimum D3D feature level");
        D3D_FEATURE_LEVEL actual{};
        hr(D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels.data(),
            static_cast<UINT>(levels.size()),D3D11_SDK_VERSION,&device_,&actual,&context_),"D3D11CreateDevice on runtime adapter");
        log("D3D11 device LUID="+hex(static_cast<uint32_t>(req.adapterLuid.HighPart))+":"+hex(req.adapterLuid.LowPart)+" feature_level="+hex(actual));
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR}; binding.device=device_.Get();
        XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO}; si.next=&binding; si.systemId=system_;
        XR(xrCreateSession(instance_,&si,&session_)); log("OpenXR stereo session created");
        input_=std::make_unique<ControllerInput>(); input_->initialize(instance_,session_,frameProfile);
        if (refresh) {
            PFN_xrGetDisplayRefreshRateFB getRate=nullptr;
            PFN_xrEnumerateDisplayRefreshRatesFB listRates=nullptr;
            XR(xrGetInstanceProcAddr(instance_,"xrGetDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&getRate)));
            XR(xrGetInstanceProcAddr(instance_,"xrEnumerateDisplayRefreshRatesFB",reinterpret_cast<PFN_xrVoidFunction*>(&listRates)));
            float rate=0; XrResult result=getRate(session_,&rate);
            log("Refresh rate result="+std::to_string(result)+" Hz="+std::to_string(rate));
            uint32_t n=0; result=listRates(session_,0,&n,nullptr);
            if (XR_SUCCEEDED(result)) {
                std::vector<float> rates(n); result=listRates(session_,n,&n,rates.data());
                if (XR_SUCCEEDED(result)) for (float r:rates) log("Supported refresh Hz="+std::to_string(r));
            }
        } else log("Display refresh extension unavailable; predictedDisplayPeriod will be logged (not a guaranteed hardware refresh rate)");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO}; space.poseInReferenceSpace.orientation.w=1;
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; XR(xrCreateReferenceSpace(session_,&space,&local_));
        space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW; XR(xrCreateReferenceSpace(session_,&space,&head_));
        XR(xrEnumerateSwapchainFormats(session_,0,&count,nullptr)); formats_.resize(count);
        XR(xrEnumerateSwapchainFormats(session_,count,&count,formats_.data()));
        for (auto f:formats_) log("OpenXR swapchain DXGI_format="+std::to_string(f));
        // The window UI is optional: without it the game still shows, it just cannot be moved.
        try { overlay_.initialize(session_,device_.Get(),context_.Get(),formats_); }
        catch (const std::exception& e) { log("UI overlay disabled: "+std::string(e.what())); overlay_.destroy(); }
        if (launcher_) {
            try { pickerOverlay_.initialize(session_,device_.Get(),context_.Get(),formats_); }
            catch (const std::exception& e) { log("Game menu disabled: "+std::string(e.what())); pickerOverlay_.destroy(); }
        }
        // The tools panel and the ambient glow are optional too: without them the game still shows.
        try { toolsOverlay_.initialize(session_,device_.Get(),context_.Get(),formats_); }
        catch (const std::exception& e) { log("Tools panel disabled: "+std::string(e.what())); toolsOverlay_.destroy(); }
        try { glowOverlay_.initialize(session_,device_.Get(),context_.Get(),formats_); }
        catch (const std::exception& e) { log("Ambient glow disabled: "+std::string(e.what())); glowOverlay_.destroy(); }
    }
    void rebuild(ID3D11Texture2D* source) {
        for (auto& c:chains_) c.destroy(); snapshot_.Reset();
        glowSampler_.reset(); glow_=fx::GlowState{}; glowShown_=fx::GlowState{}; glowOverlay_.invalidate();
        if (!source) return;
        D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d); layout_=StereoLayout::from(d).cropped(config_.cropAspect);
        if (!testMode_) config_.migrateWidth(static_cast<float>(layout_.visibleWidth())/layout_.width);
        require(layout_.visibleWidth()<=systemProps_.graphicsProperties.maxSwapchainImageWidth && layout_.height<=systemProps_.graphicsProperties.maxSwapchainImageHeight,
            "Source eye dimensions exceed runtime swapchain limits; reduce the game resolution");
        const int64_t format=(d.Format==DXGI_FORMAT_B8G8R8A8_UNORM || d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ?
            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        require(std::find(formats_.begin(),formats_.end(),format)!=formats_.end(),"Runtime lacks source-compatible sRGB swapchain format");
        d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=0; d.Usage=D3D11_USAGE_DEFAULT;
        hr(device_->CreateTexture2D(&d,nullptr,&snapshot_),"Create local stereo snapshot");
        if (glowOverlay_.ready()) glowSampler_.configure(device_.Get(),layout_.visibleWidth(),layout_.height,d.Format);
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format=format; ci.sampleCount=1; ci.width=layout_.visibleWidth(); ci.height=layout_.height;
        ci.faceCount=1; ci.arraySize=1; ci.mipCount=1;
        for (auto& c:chains_) {
            XR(xrCreateSwapchain(session_,&ci,&c.handle)); uint32_t n=0;
            XR(xrEnumerateSwapchainImages(c.handle,0,&n,nullptr));
            c.images.assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            XR(xrEnumerateSwapchainImages(c.handle,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(c.images.data())));
        }
        log("Eye swapchains="+std::to_string(layout_.width)+"x"+std::to_string(layout_.height)+" DXGI_format="+std::to_string(format)+
            " screen_aspect="+std::to_string(static_cast<float>(layout_.visibleWidth())/layout_.height)+
            " export_eye="+std::to_string(layout_.width)+"x"+std::to_string(layout_.height)+" crop_x="+std::to_string(layout_.cropX));
    }
    void events() {
        while (true) {
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            XrResult result=xrPollEvent(instance_,&event);
            if (result==XR_EVENT_UNAVAILABLE) break;
            xrCheck(result,"xrPollEvent");
            if (event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) exit_=true;
            if (event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED && input_) input_->profiles();
            if (event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                auto& e=*reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
                if (e.referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL) pendingRecenter_=e.changeTime;
            }
            if (event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto& e=*reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
                log("OpenXR session state="+std::to_string(e.state));
                if (e.state==XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR(xrBeginSession(session_,&bi)); running_=true;
                }
                if (e.state==XR_SESSION_STATE_STOPPING) { XR(xrEndSession(session_)); running_=false; }
                if (e.state==XR_SESSION_STATE_LOSS_PENDING || e.state==XR_SESSION_STATE_EXITING) exit_=true;
            }
        }
    }
    // The window UI is cosmetic: if the runtime rejects it, drop it and keep showing the game.
    void uploadOverlay(const ui::Visual& visual) {
        try { overlay_.upload(visual); }
        catch (const std::exception& e) { log("UI overlay disabled: "+std::string(e.what())); overlay_.destroy(); }
    }
    void uploadMenu(const PickerVisual& visual) {
        try { pickerOverlay_.upload(visual); }
        catch (const std::exception& e) { log("Game menu disabled: "+std::string(e.what())); pickerOverlay_.destroy(); }
    }

    // --- Game menu -------------------------------------------------------------------------------------------
    // Looks at what Steam has installed and builds the menu from it.
    void refreshSteam() {
        const auto libraries=steamLibraries();
        steamKnown_=!libraries.empty();
        steamApps_=scanSteamApps(libraries);
        rebuildMenu();
    }
    void rebuildMenu() {
        std::vector<size_t> have,missing;
        for (size_t g=0;g<Games.size();++g) {
            const SteamApp* app=findSteamApp(steamApps_,Games[g].steamAppId);
            ((!steamKnown_ || (app && app->installed())) ? have : missing).push_back(g);
        }
        menuGames_=have; menuEnabled_.assign(have.size(),1);
        for (const size_t g : missing) { if (menuGames_.size()>=picker::PerPage) break; menuGames_.push_back(g); menuEnabled_.push_back(0); }
    }
    // Switches to a game's profile (eye order, crop, window size and placement) and starts showing its picture.
    void activateGame(size_t index) {
        try {
            const auto path=profilePath(mainConfig_,index);
            ensureSettings(path);
            Config profile; profile.load(path);
            config_=profile;
        } catch (const std::exception& e) {
            log("Could not load the profile for "+std::string(Games[index].id)+": "+e.what());
            notice_=L"Could not read the settings for that game; see logs/flattodepth.log."; noticeWarn_=true;
            phase_=Phase::Choose; game_=-1; return;
        }
        windowControl_.configure(windowSettings(false)); windowControl_.reset();
        lastMode_=WindowControl::Mode::Idle;
        toolsPanel_.hide(); if (input_) input_->clearGameMotors();
        game_=static_cast<int>(index); phase_=Phase::Active; notice_.clear(); noticeWarn_=false;
        recenter_=true; restorePlacement_=true; needRebuild_=true;
        log(std::string("Game active: ")+Games[index].id+" profile="+profilePath(mainConfig_,index).string());
    }
    // The user picked a tile: start the game through Steam (launch options included) unless it is already running.
    void chooseGame(size_t index) {
        notice_.clear(); noticeWarn_=false;
        if (processRunning(Games[index].process)) { activateGame(index); return; }
        if (!launchSteamGame(Games[index].steamAppId)) {
            notice_=L"Could not ask Steam to start the game. Is Steam running?"; noticeWarn_=true;
            log("ShellExecute steam://rungameid/"+std::to_string(Games[index].steamAppId)+" failed"); return;
        }
        game_=static_cast<int>(index); phase_=Phase::Launching; launchDeadline_=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        log(std::string("Launching ")+Games[index].id+" through Steam (app "+std::to_string(Games[index].steamAppId)+")");
    }
    void returnToMenu(const wchar_t* why=nullptr) {
        phase_=Phase::Choose; game_=-1; picker_.invalidate(); windowControl_.reset();
        toolsPanel_.hide(); if (input_) input_->clearGameMotors();
        if (why) { notice_=why; noticeWarn_=true; } else { notice_.clear(); noticeWarn_=false; }
    }
    // Once a second: notice games starting or closing, however they were started.
    void updateGames() {
        const auto now=std::chrono::steady_clock::now();
        if (now<nextGameCheck_) return;
        nextGameCheck_=now+std::chrono::seconds(1);
        const int running=runningGame();
        switch (phase_) {
        case Phase::Choose:
            if (running>=0) { activateGame(static_cast<size_t>(running)); break; }
            if (now>=nextInstalledCheck_) {
                nextInstalledCheck_=now+std::chrono::seconds(10);
                refreshSteam();
            }
            break;
        case Phase::Launching:
            if (running>=0) activateGame(static_cast<size_t>(running));
            else if (now>launchDeadline_) {
                log("Launch timed out waiting for "+std::string(Games[game_].id));
                returnToMenu((std::wstring(Games[game_].title)+L" did not start within two minutes.").c_str());
            }
            break;
        case Phase::Active:
            if (running<0) { log("Game closed: back to the menu"); returnToMenu(); }
            else if (running!=game_) activateGame(static_cast<size_t>(running));
            break;
        }
    }
    PickerVisual menuVisual(const GamePicker::Output& out,GamePicker::Mode mode) const {
        PickerVisual v; v.mode=mode; v.hover=out.hover; v.backHover=out.backHover; v.prevHover=out.prevHover; v.nextHover=out.nextHover;
        v.pressed=out.pressed[0] || out.pressed[1]; v.page=picker_.page();
        for (size_t m=0;m<menuGames_.size();++m) v.entries.push_back({Games[menuGames_[m]].title,Games[menuGames_[m]].subtitle,menuEnabled_[m]!=0});
        const std::wstring title=game_>=0 ? Games[game_].title : L"the game";
        if (mode==GamePicker::Mode::Choose) {
            v.hint=!notice_.empty() ? notice_ : menuGames_.empty() ? L"No games are set up. See docs/GAMES.md to add one." : L"Point at a game and pull the trigger or squeeze the grip";
            v.warn=noticeWarn_ && !notice_.empty();
        } else if (mode==GamePicker::Mode::Message) {
            v.message=L"Starting "+title+L"..."; v.hint=L"This can take a moment. Steam may be asking something on the desktop.";
        } else {
            v.message=L"Waiting for "+title+L" to start drawing..."; v.hint=L"If this never finishes, its Geo-11 fix may be missing: run Install.cmd again (see docs/INSTALL.md).";
        }
        return v;
    }
    // --- Settings, window controls and the tools panel -------------------------------------------------------------
    WindowControl::Settings windowSettings(bool withTools) const {
        WindowControl::Settings s; s.pushPullRate=config_.pushPullRate; s.autoHide=config_.uiAutoHide; s.tools=withTools; return s;
    }
    // The tools button and panel exist while a game is showing and the panel's texture could be made.
    bool toolsAllowed() const { return !testMode_ && game_>=0 && phase_==Phase::Active && toolsOverlay_.ready(); }
    float activeRadius() const { return cylinderOk_ ? curveBase_ : 0.0f; }
    void updateCurveRadius() { curveBase_=curveRadius(pose::length(pose::sub(screen_.position,headPose_.position)),config_.curve); }
    void setStatus(const std::wstring& text,bool warn=false) {
        toolsStatus_=text; toolsWarn_=warn; toolsStatusUntil_=std::chrono::steady_clock::now()+std::chrono::seconds(7);
    }
    void toggleTools() {
        if (!toolsAllowed()) return;
        toolsPanel_.toggle(headPose_); toolsStatus_.clear();
        log(toolsPanel_.open() ? "Tools panel opened" : "Tools panel closed");
    }
    void swapEyes() {
        // Only while a game is showing: the setting belongs to that game's file, and the calibration pattern has a
        // known eye order of its own.
        if (testMode_ || game_<0 || phase_!=Phase::Active) { log("Swap eyes ignored: no game is showing"); return; }
        config_.swap=!config_.swap; config_.save(L"screen",L"swap_eyes",config_.swap ? 1.0f : 0.0f);
        log(std::string("Eyes swapped from the headset: swap_eyes=")+(config_.swap ? "1" : "0"));
        setStatus(config_.swap ? L"Eyes swapped. If the depth looks right now, you are done." : L"Eyes back to the normal order.");
    }
    ToolsVisual toolsVisual(const ToolsPanel::Output& out) const {
        ToolsVisual v;
        v.game=static_cast<size_t>(std::max(game_,0)); v.hover=out.hover; v.closeHover=out.closeHover; v.pressed=out.pressed[0] || out.pressed[1];
        v.swapEyes=config_.swap; v.curve=levelOf(CurveLevels,config_.curve); v.glow=config_.glow; v.floatWindow=levelOf(FloatLevels,config_.floatWindow);
        v.rumble=config_.rumble; v.curveAvailable=cylinderOk_; v.rumbleAvailable=input_ && input_->hapticsAvailable();
        if (std::chrono::steady_clock::now()<toolsStatusUntil_) { v.status=toolsStatus_; v.warn=toolsWarn_; }
        return v;
    }
    void applyTool(const tools::Item& item) {
        using tools::Kind;
        switch (item.kind) {
        case Kind::Key: {
            const GameKey& key=Games[game_].keys[item.key];
            const std::string fkey="F"+std::to_string(key.vk-VK_F1+1);
            const std::wstring name=std::wstring(key.label)+L" (F"+std::to_wstring(key.vk-VK_F1+1)+L")";
            switch (keys_.press(key.vk,foregroundIs(Games[game_].process))) {
            case KeyPresser::Result::Sent: setStatus(name+L" sent to the game."); log("Tools panel: sent "+fkey+" to the game"); break;
            case KeyPresser::Result::NotInFront:
                setStatus(L"The game window is not in front, so "+name+L" was not sent. Click the game on the desktop, then try again.",true);
                log("Tools panel: "+fkey+" not sent, the game window does not have the keyboard"); break;
            case KeyPresser::Result::Busy: break;
            case KeyPresser::Result::Refused: setStatus(L"That key is not allowed.",true); break;
            }
            break;
        }
        case Kind::SwapEyes: swapEyes(); break;
        case Kind::Curve:
            if (!cylinderOk_) { setStatus(L"This runtime has no curved screens (XR_KHR_composition_layer_cylinder).",true); break; }
            config_.curve=CurveLevels[(levelOf(CurveLevels,config_.curve)+1)%4]; config_.save(L"screen",L"curvature",config_.curve);
            updateCurveRadius(); setStatus(std::wstring(L"Curved screen: ")+tools::levelName(levelOf(CurveLevels,config_.curve))+L". At the wrong distance? Set curve_pose_at_axis=1 in the settings file.");
            log("Curved screen from the headset: curvature="+std::to_string(config_.curve)); break;
        case Kind::Glow:
            config_.glow=!config_.glow; config_.save(L"screen",L"glow",config_.glow ? 1.0f : 0.0f);
            setStatus(config_.glow ? L"Ambient glow on." : L"Ambient glow off."); break;
        case Kind::FloatWindow:
            config_.floatWindow=FloatLevels[(levelOf(FloatLevels,config_.floatWindow)+1)%4]; config_.save(L"screen",L"float_window",config_.floatWindow);
            setStatus(std::wstring(L"Float window: ")+tools::levelName(levelOf(FloatLevels,config_.floatWindow))+L". It hides a sliver of each edge so pop-out stays in the frame."); break;
        case Kind::Rumble:
            config_.rumble=!config_.rumble; config_.save(L"haptics",L"enabled",config_.rumble ? 1.0f : 0.0f);
            if (!input_->hapticsAvailable()) setStatus(L"The runtime has no vibration for these controllers.",true);
            else { setStatus(config_.rumble ? L"Rumble on." : L"Rumble off."); if (config_.rumble) for (int i=0;i<2;++i) input_->tick(i,0.6f,120); }
            break;
        case Kind::Recenter:
            recenter_=true; restorePlacement_=false; windowControl_.reset(); picker_.invalidate();
            toolsPanel_.place(headPose_); log("Recentered from the tools panel"); setStatus(L"Screen recentered."); break;
        }
    }
    void uploadTools(const ToolsVisual& visual) {
        try { toolsOverlay_.upload(visual); }
        catch (const std::exception& e) { log("Tools panel disabled: "+std::string(e.what())); toolsOverlay_.destroy(); toolsPanel_.hide(); }
    }
    void uploadGlow() {
        try { glowOverlay_.upload(glow_,config_.glowStrength,static_cast<float>(layout_.visibleWidth())/static_cast<float>(layout_.height)); glowShown_=glow_; }
        catch (const std::exception& e) { log("Ambient glow disabled: "+std::string(e.what())); glowOverlay_.destroy(); }
    }
    // Every few frames queue a reading of the picture's average colours, fold in whatever the GPU has finished, and
    // refresh the glow texture once the colours have moved enough to see.
    void glowStep(uint64_t number) {
        if (!config_.glow || !glowOverlay_.ready() || !glowSampler_.ready() || !snapshot_) return;
        if (number%6==0) glowSampler_.capture(context_.Get(),snapshot_.Get(),layout_.box(0,config_.swap));
        const float change=glowSampler_.read(context_.Get(),glow_,0.4f);
        if (change<0) return;
        const auto now=std::chrono::steady_clock::now();
        if (now>=nextGlowUpload_ && (!glowOverlay_.hasContent() || glow_.distance(glowShown_)>=2.5f)) {
            uploadGlow(); nextGlowUpload_=now+std::chrono::milliseconds(150);
        }
    }

    void frame(ID3D11Texture2D* source, uint64_t number) {
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState state{XR_TYPE_FRAME_STATE};
        const auto before=std::chrono::steady_clock::now();
        XR(xrWaitFrame(session_,&wi,&state));
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO}; XR(xrBeginFrame(session_,&bi));
        bool ended=false;
        try {
            keys_.tick();
            // Prove the atlas upload path once at startup rather than the first time someone points at the bar.
            if (number==1 && overlay_.ready()) { uploadOverlay(ui::Visual{}); if (overlay_.ready()) log("UI overlay atlas uploaded"); }
            if (number==1 && pickerOverlay_.ready()) { uploadMenu(menuVisual(GamePicker::Output{},GamePicker::Mode::Choose)); if (pickerOverlay_.ready()) log("Game menu texture uploaded"); }
            if (number==1 && toolsOverlay_.ready()) { uploadTools(ToolsVisual{}); if (toolsOverlay_.ready()) log("Tools panel texture uploaded"); }
            std::array<ScreenLayer,MaxLayers> slot{};
            std::array<const XrCompositionLayerBaseHeader*,MaxLayers> layers{};
            const uint32_t layerCap=std::min<uint32_t>(MaxLayers,systemProps_.graphicsProperties.maxLayerCount);
            uint32_t layerCount=0;
            if (pendingRecenter_ && state.predictedDisplayTime>=pendingRecenter_) { recenter_=true; restorePlacement_=false; pendingRecenter_=0; }
            auto controllers=input_->sync(local_,state.predictedDisplayTime);
            const bool recenterEdge=controllers.recenter && !recenterHeld_;
            if (recenterEdge) {
                recenter_=true; restorePlacement_=false; windowControl_.reset(); picker_.invalidate(); log("Controller recenter: both grips + A");
            }
            recenterHeld_=controllers.recenter;
            if (!controllers.focused) { windowControl_.reset(); picker_.reset(); toolsPanel_.reset(); }
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION}; XR(xrLocateSpace(head_,local_,state.predictedDisplayTime,&head));
            const auto headFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            const bool headOk=(head.locationFlags & headFlags)==headFlags;
            if (headOk) headPose_=head.pose;
            // Launcher mode shows the game menu instead of the game until a game's picture is arriving.
            const bool havePicture=source && layout_.visibleWidth()>0;
            const bool menuMode=launcher_ && !testMode_ && (phase_!=Phase::Active || !havePicture);
            const auto menuKind=phase_==Phase::Choose ? GamePicker::Mode::Choose :
                phase_==Phase::Launching ? GamePicker::Mode::Message : GamePicker::Mode::Status;
            GamePicker::Output menu{};
            if (menuMode && headOk) {
                if (!picker_.placed()) { picker_.place(head.pose); picker_.reset(); }
                GamePicker::Hand hands[2];
                for (int i=0;i<2;++i) hands[i]={controllers.pointerValid[i],controllers.pointer[i],controllers.click[i],0};
                menu=picker_.update(hands,menuKind,menuEnabled_);
                if (menu.chosen>=0) chooseGame(menuGames_.at(static_cast<size_t>(menu.chosen)));
                else if (menu.back) { log("Launch cancelled from the menu"); returnToMenu(); }
            }
            // Shortcuts that work without pointing at anything: both grips plus B opens the tools, plus X swaps the eyes.
            const bool toolsOk=toolsAllowed() && !menuMode;
            if (!toolsOk && toolsPanel_.open()) toolsPanel_.hide();
            if (headOk && recenterEdge && toolsPanel_.open()) toolsPanel_.place(head.pose);
            if (headOk && toolsOk && controllers.toolsChord && !toolsChordHeld_) toggleTools();
            if (toolsOk && controllers.swapChord && !swapChordHeld_) swapEyes();
            toolsChordHeld_=controllers.toolsChord; swapChordHeld_=controllers.swapChord;
            // The tools panel comes before the window controls, so a hand pointing at it is not also pointing at the bar.
            ToolsPanel::Output panelOut{};
            if (toolsPanel_.open() && toolsOk && controllers.focused && headOk) {
                ToolsPanel::Hand hands[2];
                for (int i=0;i<2;++i) hands[i]={controllers.pointerValid[i],controllers.pointer[i],controllers.click[i],0};
                panelOut=toolsPanel_.update(hands,tools::items(static_cast<size_t>(game_)).size());
                for (int i=0;i<2;++i) if (panelOut.entered[i]) input_->tick(i,0.2f,10);
                if (panelOut.clickHand>=0) input_->tick(panelOut.clickHand,0.5f,25);
                if (panelOut.close) { toolsPanel_.hide(); log("Tools panel closed"); }
                else if (panelOut.clicked>=0) applyTool(tools::items(static_cast<size_t>(game_)).at(static_cast<size_t>(panelOut.clicked)));
            }
            WindowControl::Output panel{};
            bool uiLive=false;
            windowControl_.configure(windowSettings(toolsOk));
            windowControl_.setRadius(activeRadius());
            if (!menuMode && !recenter_ && !controllers.recenter && controllers.focused && headOk && havePicture) {
                WindowControl::Hand hands[2];
                for (int i=0;i<2;++i) hands[i]={controllers.pointerValid[i] && !panelOut.pointer[i],controllers.pointer[i],controllers.click[i],controllers.stickY[i]};
                const float dt=lastFrameTime_ ? static_cast<float>(state.predictedDisplayTime-lastFrameTime_)*1e-9f : 1.0f/90;
                panel=windowControl_.update(hands,head.pose,screen_,config_.width,static_cast<float>(layout_.height)/layout_.visibleWidth(),dt);
                uiLive=true;
                if (windowControl_.mode()!=lastMode_) {
                    using Mode=WindowControl::Mode;
                    log(windowControl_.mode()==Mode::Move ? "Window grab: move (thumbstick pushes/pulls)" :
                        windowControl_.mode()==Mode::Resize ? "Window grab: resize corner" : "Window released");
                    if (windowControl_.mode()!=Mode::Idle) for (int i=0;i<2;++i) if (panel.pressed[i]) input_->tick(i,0.5f,25);
                    lastMode_=windowControl_.mode();
                }
                for (int i=0;i<2;++i) if (panel.hover[i]!=lastHover_[i]) {
                    if (isHandle(panel.hover[i]) && panel.pointer[i]) input_->tick(i,0.2f,10);
                    lastHover_[i]=panel.hover[i];
                }
                if (windowControl_.mode()==WindowControl::Mode::Move) updateCurveRadius();   // stay concentric while it is carried about
                if (panel.toolsClicked) toggleTools();
                if (panel.released) config_.savePlacement(screen_,head.pose);
            }
            lastFrameTime_=state.predictedDisplayTime;
            // Grips alone do nothing; both together are the recenter chord and keep that A press away from the game.
            // While the menu is up there is no game to play: nothing may reach one that is still starting.
            const bool menuCaptured[2]{true,true};
            const bool captured[2]{panel.capture[0] || panelOut.pointer[0],panel.capture[1] || panelOut.pointer[1]};
            input_->publish(controllers,menuMode ? menuCaptured : captured,controllers.grip[0] && controllers.grip[1],number);
            // A game that dies with its motors running cannot turn them off, so the bridge does once it is gone.
            if (input_->gameMotors()!=0 && std::chrono::steady_clock::now()>=nextRumbleCheck_) {
                nextRumbleCheck_=std::chrono::steady_clock::now()+std::chrono::seconds(1);
                if (game_<0 || !processRunning(Games[game_].process)) { input_->clearGameMotors(); log("Game rumble cleared: the game is not running"); }
            }
            // The game's rumble, played on the controllers; silent in the menu or while the headset has lost them.
            input_->rumble(input_->gameMotors(),config_.rumbleStrength,config_.rumbleSplit,config_.rumble && controllers.focused && !menuMode && !testMode_);
            std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}}; uint32_t n=0;
            XrViewState viewState{XR_TYPE_VIEW_STATE};
            XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO}; li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime=state.predictedDisplayTime; li.space=local_;
            XR(xrLocateViews(session_,&li,&viewState,2,&n,views.data()));
            const auto valid=XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            const bool viewsOk=state.shouldRender && (viewState.viewStateFlags & valid)==valid;
            // Adds the given UI quads on top of whatever is already in the layer list.
            auto pushUi=[&](const std::vector<UiQuad>& parts) {
                for (const auto& part:parts) {
                    const XrSwapchain sheet=part.sheet==2 ? toolsOverlay_.handle() : part.sheet==1 ? pickerOverlay_.handle() : overlay_.handle();
                    if (layerCount>=layerCap || !sheet) break;
                    auto& slotted=slot[layerCount]; slotted.curved=false;
                    auto& q=slotted.quad; q.type=XR_TYPE_COMPOSITION_LAYER_QUAD; q.space=local_;
                    q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
                    q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                    q.subImage.swapchain=sheet;
                    q.subImage.imageRect={{static_cast<int32_t>(part.rect.x),static_cast<int32_t>(part.rect.y)},
                        {static_cast<int32_t>(part.rect.w),static_cast<int32_t>(part.rect.h)}};
                    q.pose=part.pose; q.size=part.size;
                    layers[layerCount]=slotted.header(); ++layerCount;
                }
            };
            if (viewsOk && menuMode) {
                if (headOk && picker_.placed() && pickerOverlay_.ready()) {
                    uploadMenu(menuVisual(menu,menuKind));
                    ui::Visual pointers; pointers.held[0]=menu.pressed[0]; pointers.held[1]=menu.pressed[1];
                    uploadOverlay(pointers);
                    if (pickerOverlay_.ready() && overlay_.ready()) pushUi(buildPickerQuads(picker_,menu,head.pose));
                    else if (pickerOverlay_.ready()) pushUi({buildPickerQuads(picker_,menu,head.pose).front()});
                }
            } else if (viewsOk && source) {
                if (recenter_) {
                    XrSpaceLocation h{XR_TYPE_SPACE_LOCATION}; XR(xrLocateSpace(head_,local_,state.predictedDisplayTime,&h));
                    // Cached valid poses can exist while the wireless headset is idle.
                    // Wait for actual tracking before anchoring the first window.
                    const auto flags=XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                        XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
                    if ((h.locationFlags & flags)==flags) {
                        screen_=config_.initialPose(h.pose,restorePlacement_);
                        headPose_=h.pose; updateCurveRadius();
                        if (!restorePlacement_) config_.savePlacement(screen_,h.pose);
                        recenter_=false; log("Window placed in LOCAL space; head pose no longer changes its world position");
                    }
                }
                if (!recenter_) {
                    // One snapshot for both eyes; legacy producer can still race this copy.
                    context_->CopyResource(snapshot_.Get(),source);
                    glowStep(number);
                    const float radius=effectiveRadius(activeRadius(),config_.width);
                    const float height=config_.width*static_cast<float>(layout_.height)/static_cast<float>(layout_.visibleWidth());
                    // The glow goes first, so the picture is drawn over it.
                    if (config_.glow && glowOverlay_.hasContent() && glowSampler_.ready() && layerCap>=3) {
                        const float aspect=static_cast<float>(layout_.visibleWidth())/static_cast<float>(layout_.height);
                        auto& g=slot[layerCount];
                        fillScreenLayer(g,local_,screen_,radius,config_.curveAtAxis,0,config_.width*(aspect+2*fx::GlowMargin)/aspect,height*(1+2*fx::GlowMargin),
                            glowOverlay_.handle(),{{0,0},{static_cast<int32_t>(fx::GlowTex),static_cast<int32_t>(fx::GlowTex)}},XR_EYE_VISIBILITY_BOTH,
                            XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT);
                        layers[layerCount++]=g.header();
                    }
                    // Floating window: each eye shows a slightly narrower slice of its picture, see fx::floatShift.
                    const UINT shift=fx::floatShift(layout_.visibleWidth(),config_.floatWindow);
                    for (UINT eye=0;eye<2;++eye) {
                        auto& c=chains_[eye]; uint32_t index=0;
                        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO}; XR(xrAcquireSwapchainImage(c.handle,&ai,&index));
                        XrSwapchainImageWaitInfo sw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; sw.timeout=XR_INFINITE_DURATION;
                        XR(xrWaitSwapchainImage(c.handle,&sw));
                        const auto box=layout_.box(eye,config_.swap);
                        context_->CopySubresourceRegion(c.images.at(index).texture,0,0,0,0,snapshot_.Get(),0,&box);
                        context_->Flush();
                        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO}; XR(xrReleaseSwapchainImage(c.handle,&ri));
                        if (number==1 || number%300==0) log("Submitted eye="+std::to_string(eye)+" XR_image_index="+std::to_string(index)+
                            " SBS_crop_x="+std::to_string(box.left)+" width="+std::to_string(layout_.visibleWidth())+" height="+std::to_string(layout_.height)+
                            " curved_radius="+std::to_string(radius)+(radius>0 ? std::string(" curve_pose_at_axis=")+(config_.curveAtAxis ? "1" : "0")+" (set it to the other value if the curved screen is at the wrong distance)" : std::string())+
                            " float_shift_px="+std::to_string(shift));
                        const auto slice=fx::eyeSlice(eye,layout_.visibleWidth(),shift,config_.width);
                        auto& s=slot[layerCount];
                        fillScreenLayer(s,local_,screen_,radius,config_.curveAtAxis,slice.centre,slice.width,height,c.handle,
                            {{static_cast<int32_t>(slice.x),0},{static_cast<int32_t>(slice.w),static_cast<int32_t>(layout_.height)}},
                            eye==0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT,0);
                        layers[layerCount++]=s.header();
                    }
                    // One atlas serves both the window controls and the tools panel's pointers, so it is made once.
                    ui::Visual atlas=visualFor(panel,toolsPanel_.open());
                    for (int i=0;i<2;++i) atlas.held[i]=atlas.held[i] || panelOut.pressed[i];
                    if (uiLive && overlay_.ready()) {
                        // Bar, handle, beams and cursors go on top of the game, in order.
                        const auto parts=buildUiQuads(panel,screen_,head.pose);
                        if (!parts.empty()) uploadOverlay(atlas);
                        if (overlay_.ready()) pushUi(parts);
                    }
                    if (toolsPanel_.open() && toolsOk && headOk && toolsOverlay_.ready()) {
                        // The tools panel floats in front of everything, with its own pointers.
                        uploadTools(toolsVisual(panelOut));
                        if (overlay_.ready()) uploadOverlay(atlas);
                        if (toolsOverlay_.ready()) {
                            const auto parts=buildToolsQuads(toolsPanel_,panelOut,head.pose);
                            pushUi(overlay_.ready() ? parts : std::vector<UiQuad>{parts.front()});
                        }
                    }
                }
            }
            XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=state.predictedDisplayTime; ei.environmentBlendMode=blend_;
            ei.layerCount=layerCount; ei.layers=layerCount ? layers.data() : nullptr;
            ended=true; XR(xrEndFrame(session_,&ei));
            if (number==1 || number%300==0) {
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
                log("Frame="+std::to_string(number)+" submitted_eye_layers="+std::to_string(layerCount)+" shouldRender="+std::to_string(state.shouldRender)+
                    " predicted_period_ms="+std::to_string(state.predictedDisplayPeriod/1e6)+" CPU_wait_and_submit_ms="+std::to_string(elapsed)+
                    " view_flags="+hex(viewState.viewStateFlags));
                hr(device_->GetDeviceRemovedReason(),"D3D11 device health");
            }
        } catch (...) {
            if (!ended) {
                XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=state.predictedDisplayTime; ei.environmentBlendMode=blend_;
                xrEndFrame(session_,&ei);
            }
            throw;
        }
    }
    void run(bool test, double seconds, const std::wstring& follow) {
        testMode_=test;
        KatangaSource source; ProcessFollower follower(follow);
        ComPtr<ID3D11Texture2D> pattern;
        if (test) {
            config_.swap=false; // Calibration has known eye order, independent of Geo-11's export order.
            pattern=testTexture(device_.Get()); rebuild(pattern.Get());
            log("Stereo calibration uses known eye order (ignores source swap_eyes): left=red single marker; right=cyan double marker; orange near / blue far");
        }
        else if (launcher_) {
            // Start on whatever game is already running; otherwise show the menu.
            refreshSteam();
            const int running=runningGame();
            if (running>=0) activateGame(static_cast<size_t>(running)); else { phase_=Phase::Choose; log("Game menu: waiting for a choice"); }
        }
        else log("Waiting for Geo-11 katanga_vr GPU export; no synthetic/mono fallback. The game keeps game/controller focus.");
        const auto start=std::chrono::steady_clock::now(); uint64_t number=0;
        while (!stop && !exit_) {
            events();
            if (follower.finished()) { log("Followed game process has ended; closing the bridge"); break; }
            if (seconds>0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=seconds) break;
            while (_kbhit()) {
                const int key=_getch();
                if (key=='r' || key=='R') { recenter_=true; restorePlacement_=false; picker_.invalidate(); }
                if (key=='s' || key=='S') swapEyes();
                if (key==27) stop=true;
            }
            if (!running_) { input_->neutral(); Sleep(20); continue; }
            if (launcher_ && !test) updateGames();
            if (!test && (source.poll(device_.Get()) || needRebuild_)) { needRebuild_=false; rebuild(source.texture.Get()); }
            frame(test ? pattern.Get() : source.texture.Get(),++number);
        }
        if (running_ && !exit_) { const auto r=xrRequestExitSession(session_); log("Exit session request result="+std::to_string(r)); }
        log("Stopped after frames="+std::to_string(number));
    }
};
// FlatToDepth.exe --scan: no headset needed. Reports which installed Steam games FlatToDepth can show in 3D.
static int runScan(bool all,unsigned draft) {
    const auto libraries=steamLibraries();
    if (libraries.empty()) { std::cout<<"Steam was not found on this PC, so there are no games to scan.\n"; return 1; }
    const auto apps=scanSteamApps(libraries);
    if (draft) {
        const SteamApp* app=findSteamApp(apps,draft);
        if (!app) { std::cout<<"Steam app "<<draft<<" is not installed in any of your Steam libraries.\n"; return 1; }
        const auto a=analyseGame(*app);
        std::cout<<"; Draft entry for games.user.ini. "<<verdictName(a.verdict)<<": "<<a.note<<".\n"<<draftEntry(a);
        return 0;
    }
    std::cout<<"Looking at "<<apps.size()<<" Steam games. This reads each game's program files, so it can take a little while.\n"<<std::flush;
    const auto results=scanInstalledGames(apps,[](const SteamApp&) { std::cout<<'.'<<std::flush; });
    std::cout<<"\n\n"<<scanReport(results,all);
    return 0;
}
int main(int argc,char** argv) {
    startLog("flattodepth.log"); SetConsoleCtrlHandler(consoleHandler,TRUE);
    try {
        bool test=false,probe=false,help=false,scan=false,scanAll=false; unsigned draftApp=0; double seconds=0; std::filesystem::path config="flattodepth.ini"; std::wstring follow; std::string gameId;
        for (int i=1;i<argc;++i) {
            const std::string a=argv[i];
            if (a=="--test") test=true;
            else if (a=="--probe") probe=true;
            else if (a=="--seconds" && i+1<argc) { seconds=std::stod(argv[++i]); require(std::isfinite(seconds)&&seconds>0,"--seconds must be positive"); }
            else if (a=="--config" && i+1<argc) config=argv[++i];
            else if (a=="--follow" && i+1<argc) { const std::string name=argv[++i]; follow.assign(name.begin(),name.end()); }
            else if (a=="--game" && i+1<argc) gameId=argv[++i];
            else if (a=="--help") help=true;
            else if (a=="--scan") scan=true;
            else if (a=="--all") scanAll=true;
            else if (a=="--draft" && i+1<argc) { draftApp=static_cast<unsigned>(std::stoul(argv[++i])); scan=true; }
            else throw std::runtime_error("Unknown or incomplete argument: "+a);
        }
        // The games come from the catalog files next to the main config, so they are known only once --config is.
        std::vector<std::string> problems;
        loadGames(std::filesystem::absolute(config).parent_path(),&problems);
        if (scan) return runScan(scanAll,draftApp);
        if (help) {
            std::cout<<"FlatToDepth [--probe | --test] [--seconds N] [--config path] [--game id] [--follow game.exe]\n"
                "FlatToDepth --scan [--all] | --scan --draft <steam app number>: see which of your Steam games can be shown in 3D.\n"
                "Default: start on the in-headset game menu (or on the game that is already running). Console keys: R recenters, S swaps the eyes. Esc/Ctrl+C: exit.\n"
                "--game id: no menu, bridge that game with its own settings. Games:";
            for (const auto& g:Games) std::cout<<' '<<g.id;
            std::cout<<"\n--follow: exit by itself once that process (for example game.exe) has ended.\n";
            return 0;
        }
        int game=-1;
        if (!gameId.empty()) {
            game=findGame(gameId);
            if (game<0) { std::string known; for (const auto& g:Games) known+=std::string(known.empty()?"":", ")+g.id; throw std::runtime_error("Unknown game '"+gameId+"'. Known: "+known); }
        }
        // The menu is the default. Calibration and a fixed --game keep the single-game behaviour.
        const bool launcher=!test && !probe && game<0;
        const auto first=game>=0 ? profilePath(config,static_cast<size_t>(game)) : config;
        ensureSettings(config);
        if (game>=0) ensureSettings(first);
        Config c; c.load(first);
        App app(c,config,launcher,game); app.initialize(probe);
        if (Games.empty()) app.notify(L"No games found: games.catalog.ini is missing. Reinstall FlatToDepth.",true);
        else if (!problems.empty()) app.notify(L"Some entries in the games files were skipped; see logs/flattodepth.log.",true);
        if (!probe) app.run(test,seconds,follow);
        return 0;
    } catch (const std::exception& e) { log("ERROR: "+std::string(e.what())); return 1; }
}
