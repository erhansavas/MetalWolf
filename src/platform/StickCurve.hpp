// SPDX-License-Identifier: GPL-2.0-only
// Analog stick response for the controller.
#pragma once
#include <algorithm>
#include <cmath>

namespace stick {
// Turning: an aiming sweet spot over the inner 80% of travel (gentle power
// curve, fine control) and a steep outer band for fast turns. 0..1 -> 0..1.
inline float turnCurve(float n) {
    n=std::clamp(n,0.f,1.f);
    constexpr float knee=.8f,kneeOut=.5f;
    if(n<=knee) return kneeOut*std::pow(n/knee,1.5f);
    return kneeOut+(1-kneeOut)*(n-knee)/(1-knee);
}
// Movement: speed follows tilt linearly; when not already running, the last
// 10% of travel eases from walking into running speed. Returns a signed rate.
inline float moveRate(float value,float walk,float run,bool running) {
    float tilt=std::min(std::fabs(value),1.f),rate=tilt*(running?run:walk);
    if(!running && tilt>.9f) rate=walk+(run-walk)*(tilt-.9f)/.1f;
    return std::copysign(rate,value);
}
}
