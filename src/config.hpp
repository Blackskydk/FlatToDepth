#pragma once
#include "common.hpp"
#include "window_control.hpp"

// The steps the tools panel cycles through, and what each means. A setting in the INI may be any value in range; the
// panel shows the nearest step.
constexpr float CurveLevels[4]{0.0f,0.3f,0.55f,1.0f};         // curvature: 0 flat, 1 wraps right round you
constexpr float FloatLevels[4]{0.0f,0.008f,0.016f,0.025f};    // floating window: fraction of the picture hidden at one edge of each eye
inline int levelOf(const float (&levels)[4],float value) {
    int best=0;
    for (int i=1;i<4;++i) if (std::fabs(levels[i]-value)<std::fabs(levels[best]-value)) best=i;
    return best;
}

struct Config {
    float distance=2.5f, width=3.2f, vertical=0;
    bool swap=false;
    bool uiAutoHide=true;
    float pushPullRate=1.1f;
    double cropAspect=16.0/9;   // keep only the centred part of the export with this aspect; 0 keeps everything
    bool widthMigrated=false;   // width means the visible picture (picture_width_m), not the whole export (legacy width_m)
    float curve=0;              // 0 flat .. 1 wraps right round you; needs the runtime's cylinder layers
    bool curveAtAxis=false;     // pose convention switch for the cylinder layer, see fillScreenLayer
    bool glow=true;             // ambient glow around the screen
    float glowStrength=0.55f;
    float floatWindow=0;        // floating window: fraction of the picture hidden at one edge of each eye
    bool rumble=true;           // game rumble on the controllers
    float rumbleStrength=1.0f;
    bool rumbleSplit=false;     // left motor on the left controller and right motor on the right, instead of both following the stronger
    std::filesystem::path file;
    bool hasPlacement=false;
    XrPosef placement{{0,0,0,1},{0,0,-2.5f}};
    void load(const std::filesystem::path& path) {
        file=std::filesystem::absolute(path);
        const auto abs=file.wstring();
        auto number=[&](const wchar_t* section,const wchar_t* name,const wchar_t* fallback) {
            wchar_t buffer[64]{};
            GetPrivateProfileStringW(section,name,fallback,buffer,64,abs.c_str());
            size_t end=0; float n=std::stof(buffer,&end);
            require(end==std::wcslen(buffer) && std::isfinite(n),"Invalid configuration number");
            return n;
        };
        auto value=[&](const wchar_t* name,const wchar_t* fallback) { return number(L"screen",name,fallback); };
        wchar_t modern[8]{}; GetPrivateProfileStringW(L"screen",L"picture_width_m",L"",modern,8,abs.c_str());
        widthMigrated=modern[0]!=0;
        distance=value(L"distance_m",L"2.5"); width=widthMigrated ? value(L"picture_width_m",L"3.2") : value(L"width_m",L"3.2"); vertical=value(L"vertical_offset_m",L"0.0");
        cropAspect=value(L"crop_aspect",L"1.777778");
        require(cropAspect==0 || (cropAspect>=1 && cropAspect<=4),"crop_aspect must be 0 (show everything) or between 1 and 4");
        const float eyeSwap=value(L"swap_eyes",L"0");
        require(eyeSwap==0 || eyeSwap==1,"swap_eyes must be 0 or 1"); swap=eyeSwap==1;
        require(distance>=0.5f && distance<=20 && width>=0.2f && width<=20 && std::abs(vertical)<=10,"Screen configuration outside allowed range");
        const float autoHide=number(L"ui",L"auto_hide",L"1");
        require(autoHide==0 || autoHide==1,"[ui] auto_hide must be 0 or 1"); uiAutoHide=autoHide==1;
        pushPullRate=number(L"ui",L"push_pull_rate",L"1.1");
        require(pushPullRate>=0.1f && pushPullRate<=5,"[ui] push_pull_rate must be between 0.1 and 5");
        auto flag=[&](const wchar_t* section,const wchar_t* name,const wchar_t* fallback,const char* what) {
            const float n=number(section,name,fallback);
            require(n==0 || n==1,what); return n==1;
        };
        curve=number(L"screen",L"curvature",L"0"); require(curve>=0 && curve<=1,"curvature must be between 0 (flat) and 1");
        curveAtAxis=flag(L"screen",L"curve_pose_at_axis",L"0","curve_pose_at_axis must be 0 or 1");
        glow=flag(L"screen",L"glow",L"1","glow must be 0 or 1");
        glowStrength=number(L"screen",L"glow_strength",L"0.55"); require(glowStrength>=0 && glowStrength<=1,"glow_strength must be between 0 and 1");
        floatWindow=number(L"screen",L"float_window",L"0"); require(floatWindow>=0 && floatWindow<=0.05f,"float_window must be between 0 and 0.05");
        rumble=flag(L"haptics",L"enabled",L"1","[haptics] enabled must be 0 or 1");
        rumbleStrength=number(L"haptics",L"strength",L"1.0"); require(rumbleStrength>=0 && rumbleStrength<=2,"[haptics] strength must be between 0 and 2");
        rumbleSplit=flag(L"haptics",L"split",L"0","[haptics] split must be 0 or 1");
        hasPlacement=GetPrivateProfileIntW(L"placement",L"enabled",0,abs.c_str())!=0;
        if (hasPlacement) {
            auto saved=[&](const wchar_t* key,const wchar_t* fallback) {
                wchar_t b[64]{}; GetPrivateProfileStringW(L"placement",key,fallback,b,64,abs.c_str());
                size_t end=0; float n=std::stof(b,&end); require(end==std::wcslen(b)&&std::isfinite(n),"Invalid saved placement"); return n;
            };
            placement.position={saved(L"x_m",L"0"),saved(L"y_m",L"0"),saved(L"z_m",L"-2.5")};
            placement.orientation={saved(L"qx",L"0"),saved(L"qy",L"0"),saved(L"qz",L"0"),saved(L"qw",L"1")};
            require(pose::length(placement.position)<=50,"Saved placement too far away");
            placement.orientation=pose::normalize(placement.orientation);
        }
        log("Screen config="+std::filesystem::absolute(path).string()+" distance="+std::to_string(distance)+" width="+std::to_string(width)+" swap_eyes="+std::to_string(swap));
    }
    // Writes one setting back to this game's INI, so a change made in the headset survives a restart.
    void save(const wchar_t* section,const wchar_t* key,float value) const {
        const auto text=std::to_wstring(value);
        if (!WritePrivateProfileStringW(section,key,text.c_str(),file.c_str())) log("Could not save a setting; Win32="+std::to_string(GetLastError()));
    }
    // Older files stored the width of the whole export, black bars included. Convert once, when the real
    // picture is known, so the visible window keeps exactly the size it had.
    void migrateWidth(float visibleFraction) {
        if (widthMigrated) return;
        width=std::clamp(width*visibleFraction,0.2f,20.0f); widthMigrated=true;
        log("Saved width_m covered the whole export; window width is now the visible picture: "+std::to_string(width)+" m");
    }
    static XrQuaternionf yawRotation(XrQuaternionf q) {
        const float yaw=std::atan2(2*(q.x*q.z+q.w*q.y),1-2*(q.x*q.x+q.y*q.y));
        return {0,std::sin(yaw/2),0,std::cos(yaw/2)};
    }
    XrPosef initialPose(const XrPosef& head,bool restore) const {
        const auto yaw=yawRotation(head.orientation);
        const XrPosef relative=restore && hasPlacement ? placement : XrPosef{{0,0,0,1},{0,vertical,-distance}};
        return {pose::multiply(yaw,relative.orientation),pose::add(head.position,pose::rotate(yaw,relative.position))};
    }
    void savePlacement(const XrPosef& screen,const XrPosef& head) {
        const auto inverse=pose::inverse(yawRotation(head.orientation));
        placement={pose::multiply(inverse,screen.orientation),pose::rotate(inverse,pose::sub(screen.position,head.position))};
        if (pose::length(placement.position)>50) { log("Placement beyond 50m; not saving"); return; }
        hasPlacement=true;
        auto write=[&](const wchar_t* section,const wchar_t* key,float value) {
            const auto s=std::to_wstring(value);
            if (!WritePrivateProfileStringW(section,key,s.c_str(),file.c_str())) log("Could not save window setting; Win32="+std::to_string(GetLastError()));
        };
        if (widthMigrated) { write(L"screen",L"picture_width_m",width); WritePrivateProfileStringW(L"screen",L"width_m",nullptr,file.c_str()); }
        else write(L"screen",L"width_m",width);
        write(L"placement",L"enabled",1);
        write(L"placement",L"x_m",placement.position.x); write(L"placement",L"y_m",placement.position.y); write(L"placement",L"z_m",placement.position.z);
        write(L"placement",L"qx",placement.orientation.x); write(L"placement",L"qy",placement.orientation.y);
        write(L"placement",L"qz",placement.orientation.z); write(L"placement",L"qw",placement.orientation.w);
        log("Window placement and width saved to "+file.string());
    }
};
