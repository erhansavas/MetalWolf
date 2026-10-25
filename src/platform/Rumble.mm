// SPDX-License-Identifier: GPL-2.0-only
#include "Rumble.hpp"
#include <algorithm>
#include <cstdio>

namespace rumble {
namespace {
double now() { return NSProcessInfo.processInfo.systemUptime; }
CHHapticEventParameter *parameter(CHHapticEventParameterID identifier,float value) {
    return [[CHHapticEventParameter alloc] initWithParameterID:identifier value:value];
}
// A player whose engine already stopped (controller unplugged) throws instead
// of failing; it has nothing left to stop.
void halt(id<CHHapticPatternPlayer> player) {
    @try { [player stopAtTime:CHHapticTimeImmediate error:nil]; } @catch(NSException *) {}
}
}

void Device::attach(GCController *controller) {
    stop();
    ++generation;
    for(int a=0;a<Count;++a) {
        CHHapticEngine *e=engines[a];
        e.stoppedHandler=^(CHHapticEngineStoppedReason){}; e.resetHandler=^{};
        bool shared=false; for(int b=0;b<a;++b) shared|=engines[b]==e;
        if(e && !shared) [e stopWithCompletionHandler:nil];
        engines[a]=nil; running[a]=false;
    }
    device=controller; dual=[controller.physicalInputProfile isKindOfClass:GCDualSenseGamepad.class]; motors=false;
    if(!controller.haptics) return;
    NSSet *localities=controller.haptics.supportedLocalities;
    auto create=[&](GCHapticsLocality locality) -> CHHapticEngine * {
        if(![localities containsObject:locality]) return nil;
        CHHapticEngine *e=[controller.haptics createEngineWithLocality:locality];
        e.playsHapticsOnly=YES; e.autoShutdownEnabled=NO; e.isMutedForHaptics=NO;
        return e;
    };
    // Split grips when the controller has them: the left motor is the
    // heavy, low one on every gamepad that reports separate handles.
    engines[Body]=create(GCHapticsLocalityLeftHandle);
    engines[Detail]=create(GCHapticsLocalityRightHandle);
    if(!engines[Body] || !engines[Detail]) {
        CHHapticEngine *whole=create(GCHapticsLocalityAll)?:create(GCHapticsLocalityDefault);
        engines[Body]=engines[Detail]=whole;
    }
    if(!dual) { engines[LeftTrigger]=create(GCHapticsLocalityLeftTrigger); engines[RightTrigger]=create(GCHapticsLocalityRightTrigger); }
    // Every rumble motor that is not a DualSense voice coil is an eccentric
    // mass: it needs a full-power spin-up and cannot render transients.
    motors=!dual;
    unsigned owner=generation;
    for(int a=0;a<Count;++a) if(CHHapticEngine *e=engines[a]) {
        void (^lost)(void)=^{ dispatch_async(dispatch_get_main_queue(),^{
            if(owner!=generation) return;
            for(int b=0;b<Count;++b) if(engines[b]==e) { running[b]=false; current[b]=nil; streams[b]=nil; level[b]=0; }
        }); };
        e.stoppedHandler=^(CHHapticEngineStoppedReason){ lost(); };
        e.resetHandler=lost;
    }
    fprintf(stderr,"Haptics: %s; DualSense=%d; grips split=%d; trigger motors left=%d right=%d\n",controller.vendorName.UTF8String,dual,
            engines[Body]!=engines[Detail],engines[LeftTrigger]!=nil,engines[RightTrigger]!=nil);
}

CHHapticEngine *Device::engine(int a) {
    CHHapticEngine *e=engines[a];
    if(!e) return nil;
    if(!running[a]) {
        NSError *error=nil;
        if(![e startAndReturnError:&error]) { fprintf(stderr,"Haptic engine %d start failed: %s\n",a,error.localizedDescription.UTF8String); return nil; }
        for(int b=0;b<Count;++b) if(engines[b]==e) running[b]=true;
    }
    return e;
}

// Full drive for the hold, then a fast exponential-like decay. A stronger
// kick still at full drive is never cut by a weaker one. A soft pulse has no
// onset tap and swells in over its hold instead.
void Device::pulse(int a,feedback::Pulse p,float scale) {
    float strength=std::clamp(p.strength*scale,0.f,1.f);
    if(strength<.02f) return;
    float hold=p.hold,decay=p.decay;
    if(motors) { hold=std::max(hold,.045f); decay=std::max(decay,.06f); }
    double t=now();
    if(t<holdUntil[a] && level[a]>strength) return;
    CHHapticEngine *e=engine(a);
    if(!e) return;
    NSArray *parameters=@[parameter(CHHapticEventParameterIDHapticIntensity,strength),parameter(CHHapticEventParameterIDHapticSharpness,p.sharpness)];
    NSMutableArray *events=[NSMutableArray new];
    bool click=!motors && hold==0 && decay==0;
    if(!motors && !p.soft) [events addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient parameters:parameters relativeTime:0]];
    if(!click) [events addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:parameters relativeTime:0 duration:hold+decay]];
    CHHapticParameterCurve *envelope=[[CHHapticParameterCurve alloc] initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl controlPoints:@[
        [[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:0 value:p.soft?0:1],
        [[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:hold value:1],
        [[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:hold+decay*(p.soft?.5f:.3f) value:p.soft?.35f:.4f],
        [[CHHapticParameterCurveControlPoint alloc] initWithRelativeTime:hold+decay value:0]] relativeTime:0];
    NSError *error=nil;
    CHHapticPattern *pattern=[[CHHapticPattern alloc] initWithEvents:events parameterCurves:click?@[]:@[envelope] error:&error];
    id<CHHapticPatternPlayer> player=pattern?[e createPlayerWithPattern:pattern error:&error]:nil;
    if(!player || ![player startAtTime:CHHapticTimeImmediate error:&error]) {
        for(int b=0;b<Count;++b) if(engines[b]==e) running[b]=false;
        fprintf(stderr,"Haptic actuator %d playback failed: %s\n",a,error?error.localizedDescription.UTF8String:"no player");
        return;
    }
    halt(current[a]);
    current[a]=player; holdUntil[a]=t+hold; level[a]=strength; ++started;
}
void Device::hum(int a,feedback::Pulse p,float scale) {
    float strength=std::clamp(p.strength*scale,0.f,1.f);
    if(strength<.02f || streams[a]) return;
    CHHapticEngine *e=engine(a);
    if(!e) return;
    NSError *error=nil;
    CHHapticEvent *event=[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous parameters:@[
        parameter(CHHapticEventParameterIDHapticIntensity,strength),parameter(CHHapticEventParameterIDHapticSharpness,p.sharpness)] relativeTime:0 duration:GCHapticDurationInfinite];
    CHHapticPattern *pattern=[[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
    id<CHHapticPatternPlayer> player=pattern?[e createPlayerWithPattern:pattern error:&error]:nil;
    if(!player || ![player startAtTime:CHHapticTimeImmediate error:&error]) {
        for(int b=0;b<Count;++b) if(engines[b]==e) running[b]=false;
        fprintf(stderr,"Haptic stream %d failed: %s\n",a,error?error.localizedDescription.UTF8String:"no player");
        return;
    }
    streams[a]=player; ++started;
}
void Device::triggerSides(const feedback::HapticEffect &effect,int fire,bool sides[2]) const {
    // The fire trigger kicks; two-handed weapons and big hits shake both.
    for(int side=0;side<2;++side) sides[side]=effect.bothTriggers || (fire&(1<<side));
}
void Device::play(const feedback::HapticEffect &effect,float haptics,float triggers,int fire,float balance) {
    balance=std::clamp(balance,-1.f,1.f);
    // A hit from one side drives that grip harder and the other softer.
    float left=1+.35f*std::max(-balance,0.f)-.45f*std::max(balance,0.f),right=1+.35f*std::max(balance,0.f)-.45f*std::max(-balance,0.f);
    if(haptics>0) {
        pulse(Body,effect.body,haptics*left);
        // One shared engine still gets both layers: the crack rides the kick.
        pulse(engines[Detail]==engines[Body]?Body:Detail,effect.detail,haptics*right);
    }
    if(triggers>0 && effect.trigger.strength>0) {
        bool sides[2]; triggerSides(effect,fire,sides);
        for(int side=0;side<2;++side) if(sides[side]) pulse(LeftTrigger+side,effect.trigger,triggers);
    }
}
void Device::stream(const feedback::HapticEffect &effect,float haptics,float triggers,int fire) {
    if(haptics>0) { hum(Body,effect.body,haptics); if(engines[Detail]!=engines[Body]) hum(Detail,effect.detail,haptics); }
    if(triggers>0 && effect.trigger.strength>0) {
        bool sides[2]; triggerSides(effect,fire,sides);
        for(int side=0;side<2;++side) if(sides[side]) hum(LeftTrigger+side,effect.trigger,triggers);
    }
}
void Device::stopStream() {
    for(auto &player:streams) { halt(player); player=nil; }
}
void Device::stop() {
    stopStream();
    for(int a=0;a<Count;++a) { halt(current[a]); current[a]=nil; holdUntil[a]=0; level[a]=0; }
}
}
