// SPDX-License-Identifier: GPL-2.0-only
#include "Feedback.hpp"
#include <algorithm>
#include <initializer_list>
namespace feedback {
FirePolicy firePolicy(int weapon) {
    // Original rocket/BFG release semantics; explicit user pistol press policy.
    return weapon==1 || weapon==4 || weapon==6?FirePolicy::Press:FirePolicy::Held;
}
bool FireGate::sample(bool down,int selected,bool active) {
    if(!active) { blocked=down; held=down; weapon=selected; return false; }
    if(selected!=weapon) { if(held && down && firePolicy(selected)==FirePolicy::Press) blocked=true; weapon=selected; }
    if(!down) { blocked=false; held=false; return false; }
    bool edge=!held; held=true;
    return !blocked && (firePolicy(selected)==FirePolicy::Held || edge);
}
// Recoil (2026-10-10): the grip motors carry the gun's kick, the detail
// motor the crack of the shot and the trigger motor the jolt under the
// finger. Heavier charges hold longer and ring out longer; two-handed
// weapons kick both triggers. Automatic fire is one kick per round.
HapticEffect hapticEffect(int kind,int detail) {
    HapticEffect e;
    if(kind==WeaponFire) switch(detail) {
    case 0: e.body={.70f,.35f,.050f,.08f}; e.detail={.55f,.60f,.030f,.05f}; e.trigger={.45f,.50f,.040f,.04f}; break; // fist
    case 1: e.body={.75f,.50f,.045f,.09f}; e.detail={1.f,.95f,.030f,.05f}; e.trigger={1.f,.90f,.050f,.04f}; break; // pistol
    case 2: e.body={1.f,.15f,.070f,.24f}; e.detail={.95f,.70f,.040f,.08f}; e.trigger={1.f,.60f,.070f,.08f}; e.bothTriggers=true; break; // shotgun
    case 3: e.body={.80f,.40f,.045f,.05f}; e.detail={.90f,.85f,.030f,.04f}; e.trigger={.90f,.85f,.045f,.03f}; break; // chaingun, per round
    case 4: e.body={1.f,.05f,.100f,.40f}; e.detail={.75f,.30f,.060f,.15f}; e.trigger={1.f,.30f,.090f,.12f}; e.bothTriggers=true; break; // rocket
    case 5: e.body={.45f,.80f}; e.detail={.60f,1.f}; e.trigger={.55f,1.f}; e.stream=true; break; // plasma hum
    case 6: e.body={1.f,.02f,.200f,.60f}; e.detail={1.f,.50f,.120f,.30f}; e.trigger={1.f,.20f,.200f,.25f}; e.bothTriggers=true; break; // BFG
    case 7: e.body={.75f,.55f}; e.detail={.65f,.80f}; e.trigger={.60f,.70f}; e.bothTriggers=true; e.stream=true; break; // chainsaw motor
    case 8: e.body={1.f,.10f,.090f,.32f}; e.detail={1.f,.60f,.050f,.10f}; e.trigger={1.f,.50f,.090f,.10f}; e.bothTriggers=true; break; // double shotgun
    default: break;
    }
    else if(kind==Damage) { // actual post-armour damage
        float s=std::clamp(detail/40.f,.15f,1.f);
        e.body={.45f+.55f*s,.15f,.04f+.04f*s,.10f+.25f*s}; e.detail={.35f+.50f*s,.60f,.03f,.06f+.08f*s};
        if(detail>=20) { e.trigger={.30f+.70f*s,.30f,.05f,.08f}; e.bothTriggers=true; }
    }
    else if(kind==Explosion) { // 0..100 by distance
        float s=std::clamp(detail/100.f,0.f,1.f);
        e.body={.25f+.75f*s,.05f,.05f+.07f*s,.15f+.35f*s}; e.detail={.60f*s,.30f,.03f,.04f+.08f*s};
        if(s>.6f) { e.trigger={s,.20f,.06f,.10f}; e.bothTriggers=true; }
    }
    else if(kind==Pickup) { // a small crisp click: the fast actuator ticks, the heavy one barely touches
        e.detail={.80f,1.f,.020f,.030f}; e.body={.20f,.50f,.015f,.020f};
    }
    else if(kind==Impact) { // landings, slams, heavy footsteps: 0..100 by distance
        float s=std::clamp(detail/100.f,0.f,1.f);
        e.body={.20f+.60f*s,.10f,.045f,.08f+.15f*s}; e.detail={.30f*s,.40f,.03f,.05f};
    }
    // Mechanisms, 0..100 by distance: small, grounded and on the sound's
    // onset. A door is a short low shove, a lift a deep thunk and its moving
    // floor a faint grind, a switch a tight clack with a little weight.
    else if(kind==Door) {
        float s=std::clamp(detail/100.f,0.f,1.f);
        e.body={.20f+.30f*s,.15f,.050f,.10f+.08f*s}; e.detail={.08f+.12f*s,.40f,.020f,.05f};
    }
    else if(kind==Lift) {
        float s=std::clamp(detail/100.f,0.f,1.f);
        e.body={.20f+.35f*s,.10f,.040f,.10f+.08f*s}; e.detail={.12f*s,.30f,.020f,.04f};
    }
    else if(kind==Switch) {
        float s=std::clamp(detail/100.f,0.f,1.f);
        e.detail={.40f+.25f*s,.95f,.010f,.020f}; e.body={.15f+.15f*s,.45f,.010f,.020f};
    }
    // Menus: moving between rows is a faint soft swell on the heavy
    // actuator, apart from the interactions, which are one weak click on the
    // light one: a crisp bright one to switch a value, a rounder press to
    // enter and a dull soft one to go back.
    else if(kind==MenuMove) e.body={.07f,.10f,.030f,.070f,true};
    else if(kind==MenuToggle) e.detail={.24f,1.f};
    else if(kind==MenuSelect) e.detail={.30f,.45f};
    else if(kind==MenuBack) e.detail={.20f,.20f};
    return e;
}
bool FeedbackStream::shot(int selected,double now) {
    bool began=weapon!=selected || now>=until;
    weapon=selected; until=now+.20; return began;
}
bool FeedbackStream::update(int selected,bool firing,bool active,double now) {
    if(weapon>=0 && (!active || !firing || selected!=weapon || now>=until)) { reset(); return true; }
    return false;
}
// Trigger travel in tenths. Fire registers at 40% of travel, so each
// semi-automatic wall rises through the take-up and peaks just before that
// point: pressing through the peak is the break, then the overtravel is
// light. Automatic weapons build up without a break.
bool cyclesAfterShot(int weapon) { return weapon==2 || weapon==4 || weapon==6 || weapon==8; }
TriggerProfile triggerProfile(int weapon,TriggerState state) {
    TriggerProfile p;
    auto set=[&](std::initializer_list<float> values) { std::copy(values.begin(),values.end(),p.zones); };
    if(weapon==0) return p; // fists: a free trigger
    if(weapon==7) { // chainsaw: the engine idles under the finger, revs when cutting
        p.vibration=true;
        if(state==TriggerState::Firing) { set({0,.50f,.80f,1,1,1,1,1,1,1}); p.frequency=.50f; }
        else { set({0,.25f,.30f,.35f,.35f,.35f,.35f,.35f,.35f,.35f}); p.frequency=.35f; }
        return p;
    }
    if(state==TriggerState::Empty) { set({0,.05f,.05f,.05f,.05f,.05f,.05f,.05f,.05f,.05f}); return p; }
    if(state==TriggerState::Cycling && cyclesAfterShot(weapon)) { set({1,1,1,1,1,1,1,1,1,1}); return p; }
    if(state==TriggerState::Firing && (weapon==3 || weapon==5)) {
        p.vibration=true;
        if(weapon==3) { set({0,0,.50f,.80f,1,1,1,1,1,1}); p.frequency=.30f; }
        else { set({0,0,.40f,.60f,.75f,.75f,.75f,.75f,.75f,.75f}); p.frequency=.65f; }
        return p;
    }
    switch(weapon) {
    case 1: set({0,.20f,.55f,1,.25f,.20f,.20f,.20f,.20f,.20f}); break;      // pistol
    case 2: set({0,.30f,.75f,1,.35f,.30f,.30f,.30f,.30f,.30f}); break;      // shotgun
    case 3: set({0,.15f,.35f,.55f,.50f,.50f,.50f,.50f,.50f,.50f}); break;  // chaingun
    case 4: set({.10f,.40f,.80f,1,.45f,.40f,.40f,.40f,.40f,.40f}); break;  // rocket launcher
    case 5: set({0,.15f,.30f,.45f,.45f,.45f,.45f,.45f,.45f,.45f}); break;  // plasma rifle
    case 6: set({.20f,.50f,.90f,1,.60f,.55f,.55f,.55f,.55f,.55f}); break;  // BFG
    case 8: set({.10f,.40f,.85f,1,.40f,.35f,.35f,.35f,.35f,.35f}); break;  // double shotgun
    default: break;
    }
    return p;
}
}
