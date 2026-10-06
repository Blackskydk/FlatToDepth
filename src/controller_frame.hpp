#pragma once
#include "gamepad_ipc.hpp"
#include <openxr/openxr.h>

// One sampled frame of Steam Frame controller state.
struct ControllerFrame {
    XINPUT_GAMEPAD hands[2]{};      // gamepad controls physically located on each controller
    bool focused=false,available=false,grip[2]{},click[2]{},pointerValid[2]{};
    // Pointing ray per hand (aim pose, or grip pose if the runtime has no aim binding), LOCAL space.
    XrPosef pointer[2]{{{0,0,0,1},{0,0,0}},{{0,0,0,1},{0,0,0}}};
    float stickY[2]{};
    // Both grips held plus a face button: shortcuts for the bridge itself (A recenters, B opens the tools, X swaps eyes).
    bool recenter=false,toolsChord=false,swapChord=false;

    // Gamepad as the game should see it. A hand whose controller is busy with the window UI
    // (pointing at the bar or a handle, or dragging) contributes nothing, so UI clicks never reach the game.
    XINPUT_GAMEPAD gamepad(const bool captured[2]) const {
        XINPUT_GAMEPAD out{};
        if (!captured[0]) {
            out.sThumbLX=hands[0].sThumbLX; out.sThumbLY=hands[0].sThumbLY;
            out.bLeftTrigger=hands[0].bLeftTrigger; out.wButtons|=hands[0].wButtons;
        }
        if (!captured[1]) {
            out.sThumbRX=hands[1].sThumbRX; out.sThumbRY=hands[1].sThumbRY;
            out.bRightTrigger=hands[1].bRightTrigger; out.wButtons|=hands[1].wButtons;
        }
        return out;
    }
};
