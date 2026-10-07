#pragma once
#include "controller_frame.hpp"
#include "window_control.hpp"
#include "process_watch.hpp"
#include "virtual_pad.hpp"
#include "input_paths.hpp"
#include <map>

class ControllerInput {
    XrInstance instance_=XR_NULL_HANDLE;
    XrSession session_=XR_NULL_HANDLE;
    XrActionSet set_=XR_NULL_HANDLE;
    XrPath hands_[2]{};
    XrSpace gripSpaces_[2]{},aimSpaces_[2]{};
    std::map<std::string,XrAction> actions_;
    bool seenControllers_=false,lastFocused_=false,frameProfile_=false,aimWarned_=false;
    bool trigger_[2]{};   // pressed state with hysteresis
    bool hapticBound_=false,hapticWarned_=false,buzzing_[2]{};
    std::chrono::steady_clock::time_point tickUntil_[2]{};   // a UI tick owns this hand's motor until then
    GamepadPublisher publisher_;
    // The same pad as a virtual Xbox controller in Windows, for games that do not use a shim (gamepad=virtual in the catalog).
    VirtualPad virtualPad_;
    bool virtualWanted_=false,virtualWarned_=false;
    std::string virtualProblem_;      // why the pad is not there, while a game wants it
    std::chrono::steady_clock::time_point virtualRetry_{};
    // Plugs the virtual pad in if a game wants it and it is not there; if the driver is missing, tries again every few seconds
    // (it may be installed while FlatToDepth runs) and says why only once.
    void maintainVirtualPad() {
        if (!virtualWanted_ || virtualPad_.plugged()) return;
        const auto now=std::chrono::steady_clock::now();
        if (now<virtualRetry_) return;
        virtualRetry_=now+std::chrono::seconds(5);
        std::string why;
        if (virtualPad_.plugIn(&why)) { log("Virtual Xbox controller plugged in through ViGEmBus: the VR controllers are a normal gamepad to the game"); virtualWarned_=false; virtualProblem_.clear(); }
        else { virtualProblem_=why; if (!virtualWarned_) { log("No virtual controller for this game: "+why); virtualWarned_=true; } }
    }
    XrPath path(const std::string& text) { XrPath p{}; XR(xrStringToPath(instance_,text.c_str(),&p)); return p; }
    XrAction action(const char* name,XrActionType type,bool twoHands=false) {
        XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
        strcpy_s(ci.actionName,name); strcpy_s(ci.localizedActionName,name);
        ci.actionType=type; if (twoHands) { ci.countSubactionPaths=2; ci.subactionPaths=hands_; }
        XrAction a{}; XR(xrCreateAction(set_,&ci,&a)); actions_[name]=a; return a;
    }
    XrActionStateGetInfo get(const char* name,int hand=-1) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO}; gi.action=actions_.at(name);
        if (hand>=0) gi.subactionPath=hands_[hand]; return gi;
    }
    bool button(const char* name,int hand=-1) {
        auto gi=get(name,hand); XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN}; XR(xrGetActionStateBoolean(session_,&gi,&s));
        return s.isActive && s.currentState;
    }
    float axis(const char* name,int hand) {
        auto gi=get(name,hand); XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT}; XR(xrGetActionStateFloat(session_,&gi,&s));
        return s.isActive ? s.currentState : 0;
    }
    // Locates a hand pose action; false if the action is inactive or the pose is not actively tracked.
    bool locate(const char* name,XrSpace space,int hand,XrSpace local,XrTime time,XrPosef& pose) {
        auto gi=get(name,hand); XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE}; XR(xrGetActionStatePose(session_,&gi,&state));
        if (!state.isActive) return false;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION}; XR(xrLocateSpace(space,local,time,&location));
        const auto valid=XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        pose=location.pose;
        return (location.locationFlags & valid)==valid;
    }
public:
    void initialize(XrInstance instance,XrSession session,bool frameProfile) {
        instance_=instance; session_=session; frameProfile_=frameProfile;
        hands_[0]=path("/user/hand/left"); hands_[1]=path("/user/hand/right");
        XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO}; strcpy_s(si.actionSetName,"flattodepth_controls");
        strcpy_s(si.localizedActionSetName,"FlatToDepth gamepad and window"); XR(xrCreateActionSet(instance_,&si,&set_));
        action("stick",XR_ACTION_TYPE_VECTOR2F_INPUT,true); action("trigger",XR_ACTION_TYPE_FLOAT_INPUT,true);
        action("grip",XR_ACTION_TYPE_BOOLEAN_INPUT,true); action("bumper",XR_ACTION_TYPE_BOOLEAN_INPUT,true);
        action("stick_click",XR_ACTION_TYPE_BOOLEAN_INPUT,true); action("hand_pose",XR_ACTION_TYPE_POSE_INPUT,true);
        action("aim_pose",XR_ACTION_TYPE_POSE_INPUT,true); action("haptic",XR_ACTION_TYPE_VIBRATION_OUTPUT,true);
        for (const char* name:{"a","b","x","y","menu","view","up","down","left","right"}) action(name,XR_ACTION_TYPE_BOOLEAN_INPUT);
        std::vector<XrActionSuggestedBinding> bindings,aimBindings,hapticBindings;
        auto bind=[&](const char* name,const std::string& source) { bindings.push_back({actions_.at(name),path(source)}); };
        for (const char* hand:{"left","right"}) {
            const std::string base=std::string("/user/hand/")+hand+"/input/";
            bind("stick",base+"thumbstick"); bind("trigger",base+"trigger/value");
            bind("stick_click",base+"thumbstick/click"); bind("hand_pose",base+"grip/pose");
            aimBindings.push_back({actions_.at("aim_pose"),path(base+"aim/pose")});
            // Not base+"output/haptic": base ends in /input/, and /user/hand/left/input/output/haptic is no path at all, which is why the runtime refused it and rumble never worked.
            hapticBindings.push_back({actions_.at("haptic"),path(paths::haptic(hand))});
            if (frameProfile) { bind("grip",base+"squeeze/click"); bind("bumper",base+"bumper/click"); }
        }
        if (frameProfile) {
            for (const char* name:{"a","b","x","y"}) bind(name,std::string("/user/hand/right/input/")+name+"/click");
            bind("menu","/user/hand/right/input/menu/click"); bind("view","/user/hand/left/input/view/click");
            for (const char* name:{"up","down","left","right"}) bind(name,std::string("/user/hand/left/input/dpad_")+name+"/click");
        } else {
            // Touch fallback lacks Frame's extra face buttons, bumpers and D-pad.
            bind("a","/user/hand/right/input/a/click"); bind("b","/user/hand/right/input/b/click");
            bind("x","/user/hand/left/input/x/click"); bind("y","/user/hand/left/input/y/click");
            bind("menu","/user/hand/left/input/menu/click");
            // Touch squeeze is a float, so only the trigger can grab the window UI in this fallback.
            log("Frame profile unavailable: limited Touch fallback; trigger-only window grabbing");
        }
        XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestion.interactionProfile=path(frameProfile ? "/interaction_profiles/valve/frame_controller_valve" : "/interaction_profiles/oculus/touch_controller");
        auto suggest=[&](bool withAim,bool withHaptic) {
            auto all=bindings;
            if (withAim) all.insert(all.end(),aimBindings.begin(),aimBindings.end());
            if (withHaptic) all.insert(all.end(),hapticBindings.begin(),hapticBindings.end());
            suggestion.countSuggestedBindings=static_cast<uint32_t>(all.size()); suggestion.suggestedBindings=all.data();
            return xrSuggestInteractionProfileBindings(instance_,&suggestion);
        };
        // Pointing needs aim/pose and rumble needs the haptic output. A runtime that rejects either path rejects the
        // whole suggestion, so drop the optional bindings one at a time, aim first because pointing matters more:
        // the gamepad must keep working whatever happens, with the grip pose pointing and no rumble if need be.
        const struct { bool aim,haptic; } attempts[]={{true,true},{true,false},{false,true},{false,false}};
        XrResult suggested=XR_SUCCESS; bool aimBound=false;
        for (const auto& attempt:attempts) {
            suggested=suggest(attempt.aim,attempt.haptic);
            { char text[XR_MAX_RESULT_STRING_SIZE]{}; xrResultToString(instance_,suggested,text); log(std::string("Controller bindings with aim=")+(attempt.aim ? "1" : "0")+" haptic="+(attempt.haptic ? "1" : "0")+": "+text); }
            if (XR_SUCCEEDED(suggested)) { aimBound=attempt.aim; hapticBound_=attempt.haptic; break; }
        }
        XR(suggested);
        if (!aimBound) log("aim/pose binding rejected; pointing with the grip pose instead");
        if (!hapticBound_) log("haptic output binding rejected; no rumble on the controllers");
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO}; attach.countActionSets=1; attach.actionSets=&set_;
        XR(xrAttachSessionActionSets(session_,&attach));
        for (int i=0;i<2;++i) for (const char* name:{"hand_pose","aim_pose"}) {
            XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO}; ci.action=actions_.at(name); ci.subactionPath=hands_[i];
            ci.poseInActionSpace.orientation.w=1; XR(xrCreateActionSpace(session_,&ci,std::strcmp(name,"hand_pose")==0 ? &gripSpaces_[i] : &aimSpaces_[i]));
        }
        log("Controller actions attached; profile="+std::string(frameProfile ? "Valve Frame" : "Oculus Touch"));
    }
    void shutdown() {
        for (int i=0;i<2;++i) if (buzzing_[i]) { stopVibration(i); buzzing_[i]=false; }
        publisher_.clearMotors();
        publisher_.publish({},false);
        useVirtualPad(false);
        for (auto* spaces:{gripSpaces_,aimSpaces_}) for (int i=0;i<2;++i) { if (spaces[i]) xrDestroySpace(spaces[i]); spaces[i]=XR_NULL_HANDLE; }
        if (set_) xrDestroyActionSet(set_); set_=XR_NULL_HANDLE;
    }
    void profiles() {
        if (!session_) return;
        for (int i=0;i<2;++i) {
            XrInteractionProfileState s{XR_TYPE_INTERACTION_PROFILE_STATE}; XR(xrGetCurrentInteractionProfile(session_,hands_[i],&s));
            char buffer[XR_MAX_PATH_LENGTH]{}; uint32_t n=0;
            if (s.interactionProfile) XR(xrPathToString(instance_,s.interactionProfile,sizeof(buffer),&n,buffer));
            log("Controller hand="+std::to_string(i)+" active_profile="+buffer);
        }
    }
    ControllerFrame sync(XrSpace local,XrTime time) {
        ControllerFrame out;
        XrActiveActionSet active{set_,XR_NULL_PATH}; XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO}; si.countActiveActionSets=1; si.activeActionSets=&active;
        XrResult result=xrSyncActions(session_,&si); xrCheck(result,"xrSyncActions");
        out.focused=result==XR_SUCCESS;
        if (out.focused!=lastFocused_) { log((out.focused ? "Controller focus acquired" : "Controller focus lost: neutral gamepad")+std::string(" (the window with the keyboard: ")+foregroundDescription()+")"); lastFocused_=out.focused; }
        if (!out.focused) { trigger_[0]=trigger_[1]=false; return out; }
        auto convert=[](float x) { return static_cast<SHORT>(std::lround(std::clamp(x,-1.0f,1.0f)*32767)); };
        for (int i=0;i<2;++i) {
            auto& pad=out.hands[i];
            auto gi=get("stick",i); XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F}; XR(xrGetActionStateVector2f(session_,&gi,&stick));
            out.available |= stick.isActive==XR_TRUE;
            if (stick.isActive) out.stickY[i]=stick.currentState.y;
            if (i==0) { pad.sThumbLX=convert(stick.currentState.x); pad.sThumbLY=convert(stick.currentState.y); }
            else { pad.sThumbRX=convert(stick.currentState.x); pad.sThumbRY=convert(stick.currentState.y); }
            const float trigger=std::clamp(axis("trigger",i),0.0f,1.0f);
            const auto triggerByte=static_cast<BYTE>(std::lround(trigger*255));
            if (i==0) pad.bLeftTrigger=triggerByte; else pad.bRightTrigger=triggerByte;
            if (button("bumper",i)) pad.wButtons |= i==0 ? XINPUT_GAMEPAD_LEFT_SHOULDER : XINPUT_GAMEPAD_RIGHT_SHOULDER;
            if (button("stick_click",i)) pad.wButtons |= i==0 ? XINPUT_GAMEPAD_LEFT_THUMB : XINPUT_GAMEPAD_RIGHT_THUMB;
            out.grip[i]=button("grip",i);
            // Pointer click: trigger (with hysteresis so a half-pull does not flicker) or grip.
            if (!trigger_[i] && trigger>0.65f) trigger_[i]=true; else if (trigger_[i] && trigger<0.4f) trigger_[i]=false;
            out.click[i]=trigger_[i] || out.grip[i];
            XrPosef p{};
            if (locate("aim_pose",aimSpaces_[i],i,local,time,p)) { out.pointerValid[i]=true; out.pointer[i]=p; }
            else {
                auto state=get("aim_pose",i); XrActionStatePose s{XR_TYPE_ACTION_STATE_POSE}; XR(xrGetActionStatePose(session_,&state,&s));
                if (!s.isActive && !aimWarned_) { log("aim/pose inactive; pointing with the grip pose"); aimWarned_=true; }
                if (!s.isActive && locate("hand_pose",gripSpaces_[i],i,local,time,p)) { out.pointerValid[i]=true; out.pointer[i]=p; }
            }
        }
        // Face buttons are not per-hand actions, so say which controller each one lives on.
        struct Bit { const char* name; int mask; int frameHand,touchHand; };
        const Bit buttons[]={{"a",XINPUT_GAMEPAD_A,1,1},{"b",XINPUT_GAMEPAD_B,1,1},{"x",XINPUT_GAMEPAD_X,1,0},{"y",XINPUT_GAMEPAD_Y,1,0},
            {"menu",XINPUT_GAMEPAD_START,1,0},{"view",XINPUT_GAMEPAD_BACK,0,0},{"up",XINPUT_GAMEPAD_DPAD_UP,0,0},
            {"down",XINPUT_GAMEPAD_DPAD_DOWN,0,0},{"left",XINPUT_GAMEPAD_DPAD_LEFT,0,0},{"right",XINPUT_GAMEPAD_DPAD_RIGHT,0,0}};
        for (const auto& b:buttons) if (button(b.name)) out.hands[frameProfile_ ? b.frameHand : b.touchHand].wButtons |= static_cast<WORD>(b.mask);
        // Both grips held turns the face buttons into shortcuts for the bridge itself, and keeps them from the game.
        const bool chord=out.grip[0] && out.grip[1];
        out.recenter=chord && button("a");
        out.toolsChord=chord && button("b");
        out.swapChord=chord && button("x");
        seenControllers_ |= out.available;
        return out;
    }

    // --- The virtual controller -----------------------------------------------------------------------------------------
    // Called when a game starts or ends: on for a game whose catalog entry says gamepad=virtual, off otherwise. Plugging the pad in is
    // retried from publish() while the driver is missing, so a driver installed meanwhile is picked up.
    void useVirtualPad(bool on) {
        if (on==virtualWanted_) return;
        virtualWanted_=on; virtualRetry_={}; virtualWarned_=false; virtualProblem_.clear();
        if (on) maintainVirtualPad();
        else if (virtualPad_.plugged()) { virtualPad_.unplug(); log("Virtual Xbox controller removed"); }
    }
    // What is wrong with the virtual controller right now (empty when it works, or none is wanted), written for the player.
    const std::string& virtualPadProblem() const { return virtualProblem_; }

    // --- Rumble ----------------------------------------------------------------------------------------------
    // What the game last asked of its two motors (see packMotors), and how often it has asked.
    // For a game that has the virtual pad, what it asked of the pad's motors (the driver reports 0 to 255; packMotors takes 0 to 65535);
    // otherwise what it asked of the shim. Called every frame, which is also what keeps the driver's request for the next change open.
    // A game whose old shim is still in its folder (an install from before the virtual pad) rumbles through the shim, so the stronger of the two wins.
    LONG gameMotors() {
        const LONG fromShim=publisher_.motors();
        if (!(virtualWanted_ && virtualPad_.plugged())) return fromShim;
        const auto r=virtualPad_.rumble();
        return packMotors(std::max(static_cast<WORD>(r.heavy*257),leftMotor(fromShim)),std::max(static_cast<WORD>(r.light*257),rightMotor(fromShim)));
    }
    LONG gameMotorWrites() const { return publisher_.motorWrites()+(virtualWanted_ && virtualPad_.plugged() ? static_cast<LONG>(virtualPad_.rumbleEvents()) : 0); }    void clearGameMotors() { publisher_.clearMotors(); }
    bool hapticsAvailable() const { return hapticBound_; }
    // Call once per frame. The game's two motors become controller vibration scaled by `strength`: with split=false
    // both controllers follow the stronger motor, with split=true the left motor drives the left controller and the
    // right motor the right one. active=false (menu, focus lost, rumble off) lets go of the motors. A UI tick on a
    // hand outranks the game's rumble there until it is over.
    void rumble(LONG motors,float strength,bool split,bool active) {
        const WORD left=leftMotor(motors),right=rightMotor(motors);
        const float level[2]{split ? left/65535.0f : std::max(left,right)/65535.0f,split ? right/65535.0f : std::max(left,right)/65535.0f};
        const auto now=std::chrono::steady_clock::now();
        for (int i=0;i<2;++i) {
            if (now<tickUntil_[i]) continue;
            const float amplitude=active ? std::clamp(level[i]*strength,0.0f,1.0f) : 0.0f;
            if (amplitude>0.01f) buzzing_[i]=vibrate(i,amplitude,60'000'000);   // re-applied every frame, so it dies by itself if we stop
            else if (buzzing_[i]) { stopVibration(i); buzzing_[i]=false; }
        }
    }
    // A short click for the user interface: hovering a button, pressing it.
    void tick(int hand,float amplitude,int milliseconds) {
        if (hand<0 || hand>1) return;
        if (vibrate(hand,std::clamp(amplitude,0.0f,1.0f),static_cast<XrDuration>(milliseconds)*1'000'000))
            tickUntil_[hand]=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    }
private:
    bool vibrate(int hand,float amplitude,XrDuration duration) {
        if (!hapticBound_ || !session_) return false;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO}; info.action=actions_.at("haptic"); info.subactionPath=hands_[hand];
        XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
        vibration.duration=duration; vibration.frequency=XR_FREQUENCY_UNSPECIFIED; vibration.amplitude=amplitude;
        const XrResult result=xrApplyHapticFeedback(session_,&info,reinterpret_cast<XrHapticBaseHeader*>(&vibration));
        if (XR_FAILED(result) && !hapticWarned_) {
            log("Controller vibration failed (OpenXR="+std::to_string(result)+"); rumble may be unavailable on this controller");
            hapticWarned_=true;
        }
        return XR_SUCCEEDED(result);
    }
    void stopVibration(int hand) {
        if (!hapticBound_ || !session_) return;
        XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO}; info.action=actions_.at("haptic"); info.subactionPath=hands_[hand];
        xrStopHapticFeedback(session_,&info);
    }
public:
    // captured[i]: hand i is busy with the window UI. neutral: the whole gamepad is withheld (recenter chord).
    void publish(const ControllerFrame& frame,const bool captured[2],bool neutral,uint64_t number) {
        const XINPUT_GAMEPAD pad=frame.gamepad(captured);
        const XINPUT_GAMEPAD sent=frame.focused && !neutral ? pad : XINPUT_GAMEPAD{};
        publisher_.publish(sent,seenControllers_);
        if (virtualWanted_) { maintainVirtualPad(); virtualPad_.submit(sent); }
        if (number==1 || number%300==0) log("Controller available="+std::to_string(frame.available)+" focused="+std::to_string(frame.focused)+
            " window_ui_captured="+std::to_string(captured[0])+std::to_string(captured[1])+" buttons="+hex(pad.wButtons)+" LX="+std::to_string(pad.sThumbLX)+
            " LY="+std::to_string(pad.sThumbLY)+" game_XInput_reads="+std::to_string(publisher_.reads())+" reader_PID="+std::to_string(publisher_.readerPid())+
            " game_rumble_writes="+std::to_string(gameMotorWrites())+
            (virtualWanted_ ? std::string(" virtual_pad=")+(virtualPad_.plugged() ? "on" : "off")+" virtual_pad_errors="+std::to_string(virtualPad_.submitErrors())+" last_error="+std::to_string(virtualPad_.lastSubmitError()) : std::string()));
    }
    void neutral() { publisher_.publish({},seenControllers_); if (virtualWanted_) { maintainVirtualPad(); virtualPad_.submit({}); } }
};
