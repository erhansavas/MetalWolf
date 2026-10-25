// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <GameController/GameController.h>
#include <algorithm>
#include "Feedback.hpp"
// Where a Fire trigger registers. DualSense: at 40% of travel, just past the
// resistance wall each weapon profile builds (Feedback.cpp), so the shot
// breaks where the wall gives. Triggers without resistance fire at 15%:
// a light, immediate press with the release below it.
static constexpr float Native_FireActivationThreshold=.40f;
static constexpr float Native_FireActivationLight=.15f;
static inline float Native_FireThreshold(bool adaptive) { return adaptive?Native_FireActivationThreshold:Native_FireActivationLight; }
static inline float Native_FireRelease(bool adaptive) { return adaptive?.26f:.08f; }
static inline GCDualSenseAdaptiveTriggerMode Native_TriggerMode(const feedback::TriggerProfile &profile) {
    bool any=std::any_of(profile.zones,profile.zones+10,[](float v) { return v>0; });
    return !any?GCDualSenseAdaptiveTriggerModeOff:profile.vibration?GCDualSenseAdaptiveTriggerModeVibration:GCDualSenseAdaptiveTriggerModeFeedback;
}
// Resistance (or vibration amplitude) per tenth of travel, scaled by the
// user's trigger intensity.
static inline void Native_ApplyTrigger(GCDualSenseAdaptiveTrigger *trigger,const feedback::TriggerProfile &profile,float intensity) {
    intensity=std::clamp(intensity,0.f,1.f);
    if(intensity<=0 || Native_TriggerMode(profile)==GCDualSenseAdaptiveTriggerModeOff) { [trigger setModeOff]; return; }
    if(profile.vibration) {
        GCDualSenseAdaptiveTriggerPositionalAmplitudes amplitudes;
        for(int i=0;i<10;++i) amplitudes.values[i]=std::clamp(profile.zones[i]*intensity,0.f,1.f);
        [trigger setModeVibrationWithAmplitudes:amplitudes frequency:profile.frequency];
    } else {
        GCDualSenseAdaptiveTriggerPositionalResistiveStrengths strengths;
        for(int i=0;i<10;++i) strengths.values[i]=std::clamp(profile.zones[i]*intensity,0.f,1.f);
        [trigger setModeFeedbackWithResistiveStrengths:strengths];
    }
}
