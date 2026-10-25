// SPDX-License-Identifier: GPL-2.0-only
#pragma once
namespace feedback {
// IDs follow the existing C weapon enum; policies stay at the device boundary.
enum class FirePolicy { Held, Press };
FirePolicy firePolicy(int weapon);
struct FireGate {
    bool held=false,blocked=false;
    int weapon=-1;
    bool sample(bool down,int selected,bool active);
    void reset() { held=true; blocked=true; weapon=-1; }
};
// One actuator's pulse: full drive for `hold` seconds (rumble motors need
// ~45 ms just to spin up, so shorter drive is felt as nothing), then a fast
// decay over `decay` seconds. Sharpness only shapes voice-coil actuators.
// No hold and no decay is a lone click: a voice coil plays a bare
// transient; a rumble motor gets its shortest perceptible spin.
// soft: no onset tap; the drive swells in over the hold and fades out.
struct Pulse { float strength=0,sharpness=0,hold=0,decay=0; bool soft=false; };
// body: the low, heavy actuator (left grip); detail: the light, fast one
// (right grip); trigger: the fire trigger's own motor where the controller
// has one. Two-handed weapons kick both triggers. A stream runs until the
// weapon stops firing.
struct HapticEffect { Pulse body,detail,trigger; bool bothTriggers=false,stream=false; };
enum FeedbackKind { WeaponFire=1, Damage=2, Explosion=3, Pickup=4, Impact=5, Door=6, Lift=7, Switch=8, MenuMove=9, MenuSelect=10, MenuBack=11, MenuToggle=13 };
HapticEffect hapticEffect(int kind,int detail);
struct FeedbackStream {
    int weapon=-1;
    double until=0;
    bool shot(int selected,double now);
    bool update(int selected,bool firing,bool active,double now);
    void reset() { weapon=-1; until=0; }
};
// DualSense adaptive trigger: resistance over ten positions of travel, or a
// vibration whose amplitude follows travel. State follows the gun: Ready to
// fire, Firing (automatic weapons), Cycling (pump, break-action and launchers
// lock until the gun is ready again) and Empty (no ammunition: no wall).
enum class TriggerState { Ready, Firing, Cycling, Empty };
struct TriggerProfile { bool vibration=false; float zones[10]={}; float frequency=0; };
TriggerProfile triggerProfile(int weapon,TriggerState state);
bool cyclesAfterShot(int weapon);
}
