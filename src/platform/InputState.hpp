// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "StickCurve.hpp"

// Device state belongs to the native adapter. The original core still receives
// one bounded command per simulation step and owns all weapon/physics timing.
struct WolfInputState {
    std::array<int,128> physicalKeys{};
    std::array<unsigned,512> keyOwners{};
    std::array<bool,17> buttons{},previous{},suppressed{};
    std::array<bool,2> triggerHeld{},stickBlocked{{true,true}};
    std::array<float,4> axes{};
    int context=-1,scan=0,side=0;
    bool hud=false;
    unsigned mouseButtons=0,blockedMouse=0;
    double mouseX=0,mouseY=0,wheel=0;

    static float finiteAxis(float value) {
        return std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;
    }
    static int turnCommand(float value,float sensitivity,int walkingRate,int tics) {
        value=finiteAxis(value);
        sensitivity=std::isfinite(sensitivity)?std::clamp(sensitivity,0.f,2.f):1.f;
        return int(std::lround(value*sensitivity*walkingRate*std::clamp(tics,0,10)));
    }
    // Fire registers just past a DualSense resistance wall (40%), or at a
    // light 15% press on triggers without one (ControllerTrigger.hpp).
    bool trigger(int side,float value,bool fire,bool adaptive=true) {
        value=finiteAxis(value);
        const float press=fire?(adaptive?.40f:.15f):.15f,release=fire?(adaptive?.26f:.08f):.10f;
        triggerHeld[side]=value>=(triggerHeld[side]?release:press);
        return triggerHeld[side];
    }
    void disarm(const bool *raw) {
        for(int i=0;i<17;++i) { suppressed[i]=raw[i];buttons[i]=previous[i]=false; }
        triggerHeld.fill(false);stickBlocked.fill(true);axes.fill(0);scan=side=0;hud=false;
    }
    // Curves live in StickCurve.hpp; the stick stores linear tilt.
    void sample(const bool *raw) {
        for(int i=0;i<17;++i) {
            if(!raw[i])suppressed[i]=false;
            buttons[i]=raw[i] && !suppressed[i];
        }
    }
    void stick(int side,float rawX,float rawY,float deadzone) {
        rawX=finiteAxis(rawX);rawY=finiteAxis(rawY);
        deadzone=std::isfinite(deadzone)?std::clamp(deadzone,0.f,.95f):.18f;
        float magnitude=std::hypot(rawX,rawY);
        if(magnitude<=deadzone)stickBlocked[side]=false;
        float scale=0.f;
        if(!stickBlocked[side] && magnitude>deadzone) {
            float normalized=(std::min(magnitude,1.f)-deadzone)/(1.f-deadzone);
            scale=normalized/magnitude;
        }
        axes[side*2]=rawX*scale;axes[side*2+1]=rawY*scale;
    }
};
