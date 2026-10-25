// SPDX-License-Identifier: GPL-2.0-only
// Controller haptics: grip motors, Xbox-style
// trigger motors and DualSense voice coils, driven from one effect model
// (Feedback.hpp). Main thread only.
#pragma once
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include "Feedback.hpp"

namespace rumble {
class Device {
public:
    // nil detaches. Engines are created once per controller and restarted on
    // demand after the system stops or resets them.
    void attach(GCController *controller);
    bool dualSense() const { return dual; }
    bool hasTriggerMotors() const { return engines[LeftTrigger] || engines[RightTrigger]; }
    // fire: bit 0 the left trigger fires, bit 1 the right one. balance: -1
    // when the hit came from the left .. +1 from the right.
    void play(const feedback::HapticEffect &effect,float haptics,float triggers,int fire,float balance=0);
    void stream(const feedback::HapticEffect &effect,float haptics,float triggers,int fire);
    void stopStream();
    void stop();
    unsigned started=0;
private:
    enum { Body, Detail, LeftTrigger, RightTrigger, Count };
    GCController *device=nil;
    CHHapticEngine *engines[Count]={};
    bool running[Count]={};
    id<CHHapticPatternPlayer> current[Count]={},streams[Count]={};
    double holdUntil[Count]={};
    float level[Count]={};
    unsigned generation=0;
    bool dual=false,motors=false;
    CHHapticEngine *engine(int actuator);
    void pulse(int actuator,feedback::Pulse pulse,float scale);
    void hum(int actuator,feedback::Pulse pulse,float scale);
    void triggerSides(const feedback::HapticEffect &effect,int fire,bool sides[2]) const;
};
}
