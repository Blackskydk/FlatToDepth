#pragma once
#include "process_watch.hpp"
#include <functional>

// Presses a keyboard key on the game's behalf, for the Geo-11 shortcuts (convergence, HUD depth, ...) that otherwise
// need a keyboard. Keys can only go to whichever window has the keyboard, so the caller says whether that is the
// game; if it is not, nothing is sent rather than typing F-keys into some other program. A press is held for a
// moment because Geo-11 looks at the keyboard once per frame, and released again later by tick().
class KeyPresser {
public:
    using Clock=std::chrono::steady_clock;
    using Send=std::function<void(WORD vk,bool down)>;
    enum class Result { Sent, NotInFront, Busy, Refused };
    static constexpr int HoldMs=90;

    static void sendKey(WORD vk,bool down) {
        INPUT input{}; input.type=INPUT_KEYBOARD; input.ki.wVk=vk;
        input.ki.wScan=static_cast<WORD>(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC));
        input.ki.dwFlags=down ? 0 : KEYEVENTF_KEYUP;
        SendInput(1,&input,sizeof(input));
    }
    // Only the function keys: nothing else is ever pressed on the user's behalf.
    static bool allowed(WORD vk) { return vk>=VK_F1 && vk<=VK_F12; }

    explicit KeyPresser(Send send=&KeyPresser::sendKey) : send_(std::move(send)) {}
    ~KeyPresser() { releaseNow(); }
    KeyPresser(const KeyPresser&)=delete;
    KeyPresser& operator=(const KeyPresser&)=delete;

    Result press(WORD vk,bool gameInFront,Clock::time_point now=Clock::now()) {
        if (!allowed(vk)) return Result::Refused;
        if (held_) return Result::Busy;
        if (!gameInFront) return Result::NotInFront;
        send_(vk,true); held_=vk; releaseAt_=now+std::chrono::milliseconds(HoldMs);
        return Result::Sent;
    }
    // Call every frame: lets go of the key once it has been held long enough.
    void tick(Clock::time_point now=Clock::now()) { if (held_ && now>=releaseAt_) releaseNow(); }
    bool holding() const { return held_!=0; }
    void releaseNow() { if (held_) { send_(held_,false); held_=0; } }
private:
    Send send_;
    WORD held_=0;
    Clock::time_point releaseAt_{};
};
