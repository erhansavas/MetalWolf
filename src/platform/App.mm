// SPDX-License-Identifier: GPL-2.0-only
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include "wl_def.h"
#include "Feedback.hpp"
#include "Fullscreen.hpp"
#include "ControllerTrigger.hpp"
#include "Rumble.hpp"
#include "MenuOverlay.hpp"
#include "PresentationMetrics.hpp"
#include "RenderScale.hpp"
#include "InputState.hpp"
#include <atomic>
#include <deque>
#include <chrono>
#include <thread>
#include <csignal>
#include <algorithm>
#include <vector>
#include <new>
int wolfExitStatus=0;
bool wolfMenuFrozen=false;
static NSWindow *nativeWindow;
static NativeFullscreen presentation;
static CAMetalLayer *metalLayer;
static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static id<MTLRenderPipelineState> pipeline,statusPipeline;
static id<MTLTexture> statusTextures[3];
static id<MTLTexture> image;
static id<MTLTexture> frameTextures[3];
static dispatch_semaphore_t availableFrames;
static unsigned frameSlot;
static std::vector<uint8_t> pendingPixels;
static bool pendingFresh;   // pendingPixels differ from the last uploaded frame
static std::deque<Wolf_Event> events;
static bool focused=true,awake=true,relative=false,quitting=false,quitAsked=false;
static double quitAskedAt;
// Command-Q and closing the window ask with the game's own quit confirmation
// where it can be shown (play and the menu pages); anywhere else the request
// is not taken up within a moment, and the game quits cleanly at once.
static void askQuit() { if(!quitAsked) { quitAsked=true; quitAskedAt=NSProcessInfo.processInfo.systemUptime; } }
static volatile sig_atomic_t interrupted;
static double started,pausedAt,pausedTotal;
static int mods,logicalWidth=640,logicalHeight=480;
static WolfInputState input;
static GCController *controller;
// Grip, trigger-motor and voice-coil haptics (Rumble.mm).
static rumble::Device rumbleDevice;
// The pad's fire command this tic, after the fire gate.
static bool commandFiring;
static float damageBalance;
static double damageBalanceUntil;
static feedback::FireGate fireGate;
static double triggerRetryAt;
static int appliedTriggerWeapon=-1;
static int appliedTriggerState=-1;
static float appliedTriggerIntensity=-1;
static bool hapticsEnabled=true,triggersEnabled=true;
static float hapticIntensity=1,triggerIntensity=1,deadzone=.18f,turnSensitivity=1;
// The controller's fixed layout (DualSense names / Xbox names): Cross/A Use,
// Circle/B Run, Triangle/Y and R1 Next Weapon, L1 Previous Weapon, L2 Run,
// R2 Fire, Options/Menu Menu, Create/View Pause, Touchpad Status Bar,
// D-pad Up/Right/Down/Left Chain Gun/Machine Gun/Pistol/Knife, L3 Run, R3 Use.
// Left stick moves and strafes, right stick turns.
enum PadAction { PadNone, PadFire, PadUse, PadRun, PadPrevious, PadNext, PadMenu, PadPause, PadHUD, PadKnife, PadPistol, PadMachineGun, PadChainGun };
static const PadAction padLayout[17]={PadUse,PadRun,PadNone,PadNext,PadPrevious,PadNext,PadRun,PadFire,PadMenu,PadPause,PadHUD,PadChainGun,PadMachineGun,PadPistol,PadKnife,PadRun,PadUse};
static void readPreferences() {
    NSUserDefaults *p=NSUserDefaults.standardUserDefaults;
    auto number=[&](NSString *key,float fallback,float low,float high) {
        float value=[p objectForKey:key]?[p floatForKey:key]:fallback;
        return std::isfinite(value)?std::clamp(value,low,high):fallback;
    };
    deadzone=number(@"Deadzone",.18f,.05f,.5f); turnSensitivity=number(@"TurnSensitivity",1.f,.25f,2.f);
    hapticIntensity=number(@"HapticIntensity",1.f,0.f,1.f); triggerIntensity=number(@"TriggerIntensity",1.f,0.f,1.f);
    if([p objectForKey:@"HapticsEnabled"]) hapticsEnabled=[p boolForKey:@"HapticsEnabled"];
    if([p objectForKey:@"TriggersEnabled"]) triggersEnabled=[p boolForKey:@"TriggersEnabled"];
}
// Keyboard and controller menu navigation own separate bits. A release from
// either device must not clear a key still held by the other.
static void key(int symbol,bool down,unsigned owner=1) {
    if(symbol<=0 || unsigned(symbol)>=input.keyOwners.size())return;
    unsigned before=input.keyOwners[symbol];
    unsigned after=down?(before|owner):(before&~owner);
    input.keyOwners[symbol]=after;
    if(bool(before)==bool(after))return;
    Wolf_Event e;e.type=after?Wolf_KEYDOWN:Wolf_KEYUP;e.key.keysym={symbol,symbol,mods};events.push_back(e);
}
static void releaseMenuKeys() {
    for(unsigned i=1;i<input.keyOwners.size();++i)if(input.keyOwners[i]&2)key(int(i),false,2);
}
static void clearNativeInput() {
    events.erase(std::remove_if(events.begin(),events.end(),[](const Wolf_Event &e){return e.type>=Wolf_KEYDOWN && e.type<=Wolf_MOUSEWHEEL;}),events.end());
    input.physicalKeys.fill(0);input.keyOwners.fill(0);
    input.mouseButtons=0;input.blockedMouse=unsigned(NSEvent.pressedMouseButtons);
    input.mouseX=input.mouseY=input.wheel=0;input.context=-1;input.side=input.scan=0;input.hud=false;
    input.suppressed.fill(true);input.buttons.fill(false);input.previous.fill(false);input.stickBlocked.fill(true);input.axes.fill(0);
    Keyboard.clear();LastScan=sc_None;LastASCII=0;mods=0;
    memset(buttonstate,0,sizeof(buttonstate));memset(buttonheld,0,sizeof(buttonheld));
}
static void windowEvent(int kind) { Wolf_Event e; e.type=Wolf_WINDOWEVENT; e.window.event=kind; events.push_back(e); }
// Original "press a key" waits (level stats, messages) run outside PollControls
// while ingame stays set. The pad must navigate them like menus until play resumes.
static bool acknowledging;
void Wolf_NativeAcknowledge() { acknowledging=true; }
static bool activeGame() { return focused && awake && !presentation.transitioning && ingame && !acknowledging && !menuactive && !Paused && !demoplayback && gamestate.health>0 && !gamestate.victoryflag; }
static GCDualSenseGamepad *dual() { return [controller.physicalInputProfile isKindOfClass:GCDualSenseGamepad.class]?(GCDualSenseGamepad *)controller.physicalInputProfile:nil; }
static void stopWolfFeedback() {
    rumbleDevice.stop();commandFiring=false;
    appliedTriggerWeapon=-1;appliedTriggerState=-1;appliedTriggerIntensity=-1;triggerRetryAt=0;
    @try { GCDualSenseGamepad *p=dual();if(p.leftTrigger.mode!=GCDualSenseAdaptiveTriggerModeOff)[p.leftTrigger setModeOff];if(p.rightTrigger.mode!=GCDualSenseAdaptiveTriggerModeOff)[p.rightTrigger setModeOff]; }
    @catch(NSException *) { triggerRetryAt=NSProcessInfo.processInfo.systemUptime+.5; }
}
void Wolf_NativeReset() { stopWolfFeedback();fireGate.reset(); }
static bool focusReturned=false;
static void setWolfFocus(bool value) {
    value=value && awake;
    if(focused==value)return;
    focused=value; focusReturned|=value;
    clearNativeInput();Wolf_NativeReset();Wolf_SetRelativeMouseMode(false);
    if(value) {
        if(pausedAt){pausedTotal+=NSProcessInfo.processInfo.systemUptime-pausedAt;pausedAt=0;}
        Wolf_NativeAudioSuspend(false);windowEvent(Wolf_WINDOWEVENT_FOCUS_GAINED);
    } else {
        pausedAt=NSProcessInfo.processInfo.systemUptime;Wolf_NativeAudioSuspend(true);windowEvent(Wolf_WINDOWEVENT_FOCUS_LOST);
    }
}
// The status card (WolfNative.hpp): saved, on by default.
static int statusBar=-1;
bool Wolf_NativeStatusBar() {
    if(statusBar<0) { NSUserDefaults *d=NSUserDefaults.standardUserDefaults; statusBar=[d objectForKey:@"StatusBar"]?[d boolForKey:@"StatusBar"]:1; }
    return statusBar;
}
void Wolf_NativeSetStatusBar(bool on) { statusBar=on; [NSUserDefaults.standardUserDefaults setBool:on forKey:@"StatusBar"]; }
// The card is shown with the setting on, or while Tab is held, as the
// original shows the status bar over a full-screen view.
static bool statusShown() { return ingame && !gamestate.victoryflag && (Wolf_NativeStatusBar() || Keyboard[sc_Tab]); }
static void toggleHUD() { if(activeGame()) Wolf_NativeSetStatusBar(!Wolf_NativeStatusBar()); }
@interface WolfDelegate : NSObject <NSWindowDelegate,NSApplicationDelegate>
@end
@implementation WolfDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender { askQuit(); return NO; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender { askQuit(); return NSTerminateCancel; }
- (void)windowDidBecomeKey:(NSNotification *)n { if(n.object==nativeWindow)setWolfFocus(true); }
- (void)windowDidResignKey:(NSNotification *)n { if(n.object==nativeWindow && !nativeWindow.keyWindow)setWolfFocus(false); }
- (void)windowDidResize:(NSNotification *)n { windowEvent(Wolf_WINDOWEVENT_RESIZED); }
- (void)windowDidChangeBackingProperties:(NSNotification *)n { windowEvent(Wolf_WINDOWEVENT_RESIZED); }
- (void)windowDidMiniaturize:(NSNotification *)n { setWolfFocus(false);windowEvent(Wolf_WINDOWEVENT_MINIMIZED); }
- (void)windowDidDeminiaturize:(NSNotification *)n { windowEvent(Wolf_WINDOWEVENT_RESTORED);setWolfFocus(NSApp.active && nativeWindow.keyWindow); }
- (void)applicationDidResignActive:(NSNotification *)n { setWolfFocus(false); }
- (void)applicationDidBecomeActive:(NSNotification *)n { setWolfFocus(nativeWindow.keyWindow); }
- (void)fullscreen:(id)sender { Wolf_NativeReset();clearNativeInput();Wolf_SetRelativeMouseMode(false);nativeWindow=presentation.toggle(nativeWindow,self);fullscreen=presentation.active(nativeWindow);setWolfFocus(nativeWindow.keyWindow); }
- (NSSize)window:(NSWindow *)window willUseFullScreenContentSize:(NSSize)proposed { return window.screen.frame.size; }
- (NSApplicationPresentationOptions)window:(NSWindow *)window willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)options { return NativeFullscreen::options(options); }
- (void)windowWillEnterFullScreen:(NSNotification *)notice { presentation.transitioning=true;Wolf_NativeReset();clearNativeInput();Wolf_SetRelativeMouseMode(false); }
- (void)windowWillExitFullScreen:(NSNotification *)notice { presentation.transitioning=true;Wolf_NativeReset();clearNativeInput();Wolf_SetRelativeMouseMode(false); }
- (void)windowDidEnterFullScreen:(NSNotification *)notice { presentation.transitioning=false;fullscreen=true;[nativeWindow makeFirstResponder:nativeWindow.contentView]; }
- (void)windowDidExitFullScreen:(NSNotification *)notice { presentation.transitioning=false;fullscreen=false;[nativeWindow makeFirstResponder:nativeWindow.contentView]; }
- (void)windowDidFailToEnterFullScreen:(NSWindow *)window { presentation.transitioning=false;fullscreen=false; }
- (void)windowDidFailToExitFullScreen:(NSWindow *)window { presentation.transitioning=false;fullscreen=presentation.active(nativeWindow); }
@end
static WolfDelegate *delegate;
// In-game menu equivalents of the former menu bar items.
bool Wolf_NativeFullscreen() { return fullscreen; }
void Wolf_NativeToggleFullscreen() { [delegate fullscreen:nil]; }
static void storePad() {
    NSUserDefaults *p=NSUserDefaults.standardUserDefaults;
    [p setFloat:deadzone forKey:@"Deadzone"]; [p setFloat:turnSensitivity forKey:@"TurnSensitivity"];
    [p setFloat:hapticIntensity forKey:@"HapticIntensity"]; [p setFloat:triggerIntensity forKey:@"TriggerIntensity"];
    [p setBool:hapticsEnabled forKey:@"HapticsEnabled"]; [p setBool:triggersEnabled forKey:@"TriggersEnabled"];
    Wolf_NativeReset();
}
// The connected controller's button names as printed on it: PlayStation
// and Xbox by name, any other by the name the system gives each button.
static const char *buttonName(GCController *c,int button) {
    GCExtendedGamepad *p=c.extendedGamepad;
    if(!p || button<0 || button>WOLF_BUTTON_TOUCHPAD) return nullptr;
    GCPhysicalInputProfile *profile=c.physicalInputProfile;
    bool sense=[profile isKindOfClass:GCDualSenseGamepad.class],shock=[profile isKindOfClass:GCDualShockGamepad.class],xbox=[profile isKindOfClass:GCXboxGamepad.class];
    GCControllerElement *elements[]={nil,p.buttonA,p.buttonB,p.buttonX,p.buttonY,p.leftShoulder,p.rightShoulder,p.leftTrigger,p.rightTrigger,p.buttonMenu,p.buttonOptions,nil};
    static const char *playstation[]={"","Cross","Circle","Square","Triangle","L1","R1","L2","R2","Options","Create",""};
    static const char *box[]={"","A","B","X","Y","LB","RB","LT","RT","Menu","View",""};
    static const char *plain[]={"","A","B","X","Y","L1","R1","L2","R2","Menu","Options",""};
    NSString *name=nil;
    if(button==WOLF_BUTTON_NAME) name=c.vendorName.length?c.vendorName:@"Controller";
    else if(button==WOLF_BUTTON_TOUCHPAD) { if(!sense) return nullptr; name=@"touchpad"; }
    else if(!elements[button]) return nullptr;
    else if(sense || shock) name=shock && button==WOLF_BUTTON_PAUSE?@"Share":@(playstation[button]);
    else if(xbox) name=@(box[button]);
    else {
        name=elements[button].localizedName;
        if([name hasSuffix:@" Button"]) name=[name substringToIndex:name.length-7];
        if(!name.length) name=@(plain[button]);
    }
    static char text[WOLF_BUTTON_TOUCHPAD+1][64];
    snprintf(text[button],sizeof(text[button]),"%s",name.UTF8String);
    return text[button];
}
// 0 none, 1 DualSense (haptics, adaptive triggers), 2 any other controller.
static int padKind(GCController *c) {
    if(!c.extendedGamepad) return 0;
    return [c.physicalInputProfile isKindOfClass:GCDualSenseGamepad.class]?1:2;
}
static bool padTriggerEffects(GCController *c) {
    return padKind(c)==1 || [c.haptics.supportedLocalities containsObject:GCHapticsLocalityRightTrigger];
}
int Wolf_NativePadKind() { return padKind(controller); }
const char *Wolf_NativePadButton(int button) { return buttonName(controller,button); }
bool Wolf_NativePadVibration() { return controller.haptics!=nil; }
bool Wolf_NativePadTriggerEffects() { return padTriggerEffects(controller); }
int Wolf_NativePadValue(int setting) {
    switch(setting) {
    case WOLF_PAD_DEADZONE: return int(std::lround(deadzone*100));
    case WOLF_PAD_TURN: return int(std::lround(turnSensitivity*100));
    case WOLF_PAD_HAPTICS: return hapticsEnabled;
    case WOLF_PAD_HAPTIC_STRENGTH: return int(std::lround(hapticIntensity*100));
    case WOLF_PAD_TRIGGERS: return triggersEnabled;
    case WOLF_PAD_TRIGGER_STRENGTH: return int(std::lround(triggerIntensity*100));
    }
    return 0;
}
void Wolf_NativePadAdjust(int setting,int direction) {
    long step=direction>0?5:-5;
    switch(setting) {
    case WOLF_PAD_DEADZONE: deadzone=std::clamp(std::lround(deadzone*100)+step,5L,50L)/100.f; break;
    case WOLF_PAD_TURN: turnSensitivity=std::clamp(std::lround(turnSensitivity*100)+step,25L,200L)/100.f; break;
    case WOLF_PAD_HAPTICS: hapticsEnabled=!hapticsEnabled; break;
    case WOLF_PAD_HAPTIC_STRENGTH: hapticIntensity=std::clamp(std::lround(hapticIntensity*100)+step,0L,100L)/100.f; break;
    case WOLF_PAD_TRIGGERS: triggersEnabled=!triggersEnabled; break;
    case WOLF_PAD_TRIGGER_STRENGTH: triggerIntensity=std::clamp(std::lround(triggerIntensity*100)+step,0L,100L)/100.f; break;
    }
    storePad();
}
static int symbol(NSEvent *e) {
    if(ingame && !menuactive && !Paused) switch(e.keyCode) {
        case 0:return WK_a;case 1:return WK_s;case 2:return WK_d;case 13:return WK_w;case 12:return WK_q;case 14:return WK_e;
        case 18:return WK_1;case 19:return WK_2;case 20:return WK_3;case 21:return WK_4;
    }
    switch(e.keyCode) {
        case 53:return WK_ESCAPE; case 36:case 76:return WK_RETURN; case 48:return WK_TAB; case 51:case 117:return WK_BACKSPACE;
        case 123:return WK_LEFT; case 124:return WK_RIGHT; case 125:return WK_DOWN; case 126:return WK_UP;
        case 122:return WK_F1; case 120:return WK_F2; case 99:return WK_F3; case 118:return WK_F4;
        case 96:return WK_F5; case 97:return WK_F6; case 98:return WK_F7; case 100:return WK_F8;
        case 101:return WK_F9; case 109:return WK_F10; case 103:return WK_F11; case 111:return WK_F12;
    }
    NSString *text=e.charactersIgnoringModifiers.lowercaseString;
    return text.length && [text characterAtIndex:0]<128?[text characterAtIndex:0]:0;
}
static void padButtons(bool *b) {
    GCExtendedGamepad *p=controller.extendedGamepad;
    b[0]=p.buttonA.pressed;b[1]=p.buttonB.pressed;b[2]=p.buttonX.pressed;b[3]=p.buttonY.pressed;
    b[4]=p.leftShoulder.pressed;b[5]=p.rightShoulder.pressed;
    bool adaptive=dual() && triggersEnabled && triggerIntensity>0;
    b[6]=input.trigger(0,p.leftTrigger.value,false,adaptive);
    b[7]=input.trigger(1,p.rightTrigger.value,true,adaptive);
    b[8]=p.buttonMenu.pressed;b[9]=p.buttonOptions.pressed;b[10]=dual().touchpadButton.pressed;
    b[11]=p.dpad.yAxis.value>.5f;b[12]=p.dpad.xAxis.value>.5f;b[13]=p.dpad.yAxis.value<-.5f;b[14]=p.dpad.xAxis.value<-.5f;
    b[15]=p.leftThumbstickButton.pressed;b[16]=p.rightThumbstickButton.pressed;
}
// The trigger follows the gun: a wall to break for the pistol, a building
// pull that buzzes while an automatic is held firing, slack when empty.
static int profileWeapon() { return gamestate.weapon==wp_knife?0:gamestate.weapon==wp_pistol?1:3; }
static void updateWolfTriggers() {
    GCDualSenseGamepad *pad=dual();if(!pad || !activeGame())return;
    double now=NSProcessInfo.processInfo.systemUptime;
    using feedback::TriggerState;
    int weapon=profileWeapon();
    TriggerState state=weapon && gamestate.ammo<=0?TriggerState::Empty:weapon==3 && commandFiring?TriggerState::Firing:TriggerState::Ready;
    bool changed=appliedTriggerWeapon!=weapon || appliedTriggerState!=int(state) || appliedTriggerIntensity!=triggerIntensity;
    if(!changed && now<triggerRetryAt)return;
    auto profile=feedback::triggerProfile(weapon,state);
    @try {
        for(int side=0;side<2;++side) {
            GCDualSenseAdaptiveTrigger *trigger=side?pad.rightTrigger:pad.leftTrigger;
            bool fire=side==1 && triggersEnabled && triggerIntensity>0;
            auto desired=fire?Native_TriggerMode(profile):GCDualSenseAdaptiveTriggerModeOff;
            if(!changed && trigger.mode==desired)continue;
            if(!fire)[trigger setModeOff];
            else Native_ApplyTrigger(trigger,profile,triggerIntensity);
        }
        appliedTriggerWeapon=weapon;appliedTriggerState=int(state);appliedTriggerIntensity=triggerIntensity;triggerRetryAt=now+.05;
    } @catch(NSException *exception) {
        appliedTriggerWeapon=-1;triggerRetryAt=now+.5;
        fprintf(stderr,"Wolf trigger interrupted: %s\n",exception.reason.UTF8String);
    }
}
static void retireWolfHaptics() {
    Wolf_NativeReset();rumbleDevice.attach(nil);
}
// A controller in use: a button or trigger pressed, or a stick or the d-pad
// pushed past halfway. Stick drift at rest does not count.
static bool engaged(GCController *c) {
    GCExtendedGamepad *p=c.extendedGamepad; if(!p) return false;
    if(p.buttonA.pressed || p.buttonB.pressed || p.buttonX.pressed || p.buttonY.pressed || p.leftShoulder.pressed || p.rightShoulder.pressed ||
       p.buttonMenu.pressed || p.buttonOptions.pressed || p.leftThumbstickButton.pressed || p.rightThumbstickButton.pressed) return true;
    if([c.physicalInputProfile isKindOfClass:GCDualSenseGamepad.class] && ((GCDualSenseGamepad *)c.physicalInputProfile).touchpadButton.pressed) return true;
    if(p.leftTrigger.value>.3f || p.rightTrigger.value>.3f) return true;
    auto pushed=[](GCControllerDirectionPad *d) { return std::hypot(d.xAxis.value,d.yAxis.value)>.5f; };
    return pushed(p.leftThumbstick) || pushed(p.rightThumbstick) || pushed(p.dpad);
}
// With more than one controller connected, the one last used plays: another
// takes over when it is used while the playing one is idle, so two in use at
// once never trade control back and forth.
static GCController *takeover(GCController *playing) {
    if(engaged(playing)) return nil;
    for(GCController *c in GCController.controllers) if(c!=playing && engaged(c)) return c;
    return nil;
}
static void padEvents() {
    NSArray<GCController *> *connected=GCController.controllers;
    GCController *next=[connected containsObject:controller]?controller:nil;
    GCController *taken=focused && awake?takeover(next):nil;
    if(taken)next=taken;
    if(!next)for(GCController *c in connected)if(c.extendedGamepad){next=c;break;}
    if(next!=controller) {
        int context=input.context;
        releaseMenuKeys();retireWolfHaptics();controller=next;input.context=-1;rumbleDevice.attach(controller);
        input.triggerHeld.fill(false);input.suppressed.fill(true);input.buttons.fill(false);input.previous.fill(false);input.stickBlocked.fill(true);input.axes.fill(0);
        if(controller){controller.handlerQueue=dispatch_get_main_queue();controller.playerIndex=GCControllerPlayerIndexUnset;}
        // A controller taking over acts on the press that brought it in.
        if(taken){input.context=context;input.suppressed.fill(false);input.stickBlocked.fill(false);}
    }
    GCExtendedGamepad *p=controller.extendedGamepad;if(!p)return;
    bool raw[17];padButtons(raw);
    int context=!focused || !awake || presentation.transitioning?0:activeGame()?2:1;
    if(context!=input.context) {
        releaseMenuKeys();input.disarm(raw);input.context=context;Wolf_NativeReset();
    }
    input.sample(raw);
    input.stick(0,p.leftThumbstick.xAxis.value,p.leftThumbstick.yAxis.value,deadzone);
    input.stick(1,p.rightThumbstick.xAxis.value,p.rightThumbstick.yAxis.value,deadzone);
    const auto &now=input.buttons;
    if(context==1) {
        // Each new menu press ticks the hand: steps faint, confirm firmer, back duller.
        auto menuKey=[&](int symbol,bool down,int kind) {
            if(down && !(input.keyOwners[symbol]&2) && hapticsEnabled)
                @try { rumbleDevice.play(feedback::hapticEffect(kind,0),hapticIntensity,0,0); } @catch(NSException *) { rumbleDevice.attach(controller); }
            key(symbol,down,2);
        };
        menuKey(WK_RETURN,now[0],feedback::MenuSelect);menuKey(WK_ESCAPE,now[1] || now[8],feedback::MenuBack);key(WK_PAUSE,now[9],2);
        float x=input.axes[0],y=input.axes[1];
        menuKey(WK_UP,now[11] || (y>.55f && fabs(y)>=fabs(x)),feedback::MenuMove);
        menuKey(WK_RIGHT,now[12] || (x>.55f && fabs(x)>fabs(y)),feedback::MenuMove);
        menuKey(WK_DOWN,now[13] || (y<-.55f && fabs(y)>=fabs(x)),feedback::MenuMove);
        menuKey(WK_LEFT,now[14] || (x<-.55f && fabs(x)>fabs(y)),feedback::MenuMove);
    } else if(context==2) {
        for(int i=0;i<17;++i)if(now[i] && !input.previous[i] && padLayout[i]==PadHUD)input.hud=!input.hud;
        updateWolfTriggers();
    }
    input.previous=now;
}
static void mouseButton(NSEvent *event) {
    unsigned physical=unsigned(event.buttonNumber),mask=physical<3?1u<<physical:0;
    if(!mask)return;
    bool down=event.type==NSEventTypeLeftMouseDown || event.type==NSEventTypeRightMouseDown || event.type==NSEventTypeOtherMouseDown;
    if(!down)input.blockedMouse&=~mask;
    if(!focused || !awake || presentation.transitioning || (input.blockedMouse&mask))return;
    bool before=(input.mouseButtons&mask)!=0;
    if(down)input.mouseButtons|=mask;else input.mouseButtons&=~mask;
    if(before==down)return;
    Wolf_Event e;e.type=down?Wolf_MOUSEBUTTONDOWN:Wolf_MOUSEBUTTONUP;
    e.button.button=physical==0?Wolf_BUTTON_LEFT:physical==1?Wolf_BUTTON_RIGHT:Wolf_BUTTON_MIDDLE;events.push_back(e);
}
// Shared native menu panel in Wolfenstein colors (2026-10-10), drawn into the
// Metal frame. Original screens without a list page show through after a
// short grace that hides the gaps between consecutive pages. While the panel
// is up, frames follow the display and a changed page goes out at once.
static NativeMenuOverlay menuOverlay;
bool Wolf_NativeQuitAsked() { bool asked=quitAsked; quitAsked=false; return asked; }
void Wolf_NativeQuitFade() { NativeFadeOutWindow(nativeWindow,.6); }
static double menuSeenAt=-1,menuUpdatedAt,menuFrameDue;
static bool menuRedraw=false;
static void updateMenuOverlay(bool always=false) {
    double now=NSProcessInfo.processInfo.systemUptime;
    if(!always && now-menuUpdatedAt<1/120.0)return;
    menuUpdatedAt=now;
    char title[512];int count=focused?Wolf_NativeMenuPage(title,sizeof(title)):-1;
    if(count>=0) {
        NativeMenuPage page;page.identity=title;page.selected=Wolf_NativeMenuSelection();
        page.heading=title[0]?[NSString stringWithUTF8String:title]:@"Wolfenstein 3D";
        for(int i=0;i<count;++i) {
            char label[128];int status=Wolf_NativeMenuRow(i,label,sizeof(label));if(status<0)continue;
            page.rows.push_back({[NSString stringWithUTF8String:label],status,i});
        }
        bool confirm=Wolf_NativeMenuNoticeConfirm();
        const char *more=strchr(title,'\n');
        if(count==0 || confirm) { page.body=page.heading; page.heading=@"Wolfenstein 3D"; }
        else if(more) {
            // A page with notes: its name, then the notes above the rows.
            page.heading=[[NSString alloc] initWithBytes:title length:size_t(more-title) encoding:NSUTF8StringEncoding];
            page.body=[NSString stringWithUTF8String:more+1];
        }
        // The connected controller's button names, or the keys alone.
        const char *yes=Wolf_NativePadButton(WOLF_BUTTON_SOUTH),*no=Wolf_NativePadButton(WOLF_BUTTON_EAST);
        NSString *ok=yes?[NSString stringWithFormat:@"ENTER / %s",yes]:@"ENTER",*back=no?[NSString stringWithFormat:@"ESC / %s",no]:@"ESC",*move=yes?@"↑↓ / STICK":@"↑↓";
        page.hint=(confirm?[NSString stringWithFormat:@"%@  ·  %@  ·  %@",move,ok,back]:count?[NSString stringWithFormat:@"%@  ·  ←→ ADJUST  ·  %@  ·  %@",move,ok,back]:ok).uppercaseString;
        if(menuOverlay.present(nativeWindow.contentView.bounds,nativeWindow.screen.safeAreaInsets.top,nativeWindow.backingScaleFactor,page))menuRedraw=true;
        menuSeenAt=now;
        // The pointer selects the row it moves onto; a still pointer leaves
        // keyboard and controller selection alone.
        static NSPoint lastPointer={-1,-1};
        NSPoint pointer=[nativeWindow.contentView convertPoint:nativeWindow.mouseLocationOutsideOfEventStream fromView:nil];
        if(!NSEqualPoints(pointer,lastPointer)) {
            bool moved=lastPointer.x>=0; lastPointer=pointer;
            int row=menuOverlay.hit(CGPointMake(pointer.x,pointer.y));
            if(moved && row>=0) wolfMenuHover=row;
        }
    } else if(menuOverlay.shown() && (!focused || now-menuSeenAt>.25)) { menuOverlay.close();menuRedraw=true; }
}
static void pump() {
    @autoreleasepool {
        for(NSEvent *e; (e=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:NSDate.distantPast inMode:NSDefaultRunLoopMode dequeue:YES]);) {
            mods=0;if(e.modifierFlags&NSEventModifierFlagShift)mods|=KMOD_SHIFT;if(e.modifierFlags&NSEventModifierFlagCapsLock)mods|=KMOD_CAPS;
            if(e.modifierFlags&NSEventModifierFlagOption)mods|=KMOD_LALT;if(e.modifierFlags&NSEventModifierFlagCommand)mods|=KMOD_LGUI;
            bool canInput=focused && awake && !presentation.transitioning;
            if(e.type==NSEventTypeKeyDown || e.type==NSEventTypeKeyUp) {
                unsigned code=e.keyCode;
                if(e.type==NSEventTypeKeyUp) {
                    if(code<input.physicalKeys.size()) {
                        int s=input.physicalKeys[code];input.physicalKeys[code]=0;
                        if(s && std::find(input.physicalKeys.begin(),input.physicalKeys.end(),s)==input.physicalKeys.end())key(s,false);
                    }
                } else if(e.modifierFlags&NSEventModifierFlagCommand) {
                    if([e.charactersIgnoringModifiers.lowercaseString isEqual:@"q"]) { if(!e.isARepeat) askQuit(); }
                    else [NSApp sendEvent:e];
                }
                else if(canInput && !e.isARepeat && code<input.physicalKeys.size()) {
                    int s=symbol(e);
                    if(s && !input.physicalKeys[code]) {input.physicalKeys[code]=s;key(s,true);}
                }
            } else if(e.type==NSEventTypeFlagsChanged) {
                if(canInput) {
                    key(WK_LSHIFT,(e.modifierFlags&NSEventModifierFlagShift)!=0);
                    key(WK_LCTRL,(e.modifierFlags&NSEventModifierFlagControl)!=0);
                    key(WK_LALT,(e.modifierFlags&NSEventModifierFlagOption)!=0);
                }
            } else if(e.type==NSEventTypeMouseMoved || e.type==NSEventTypeLeftMouseDragged || e.type==NSEventTypeRightMouseDragged || e.type==NSEventTypeOtherMouseDragged) {
                // Look and aim are consumed only by gameplay tics. Motion during
                // menus, pause or intermissions must not queue up for later.
                if(canInput && activeGame() && (relative || e.window==nativeWindow)) {
                    double dx=std::isfinite(e.deltaX)?e.deltaX:0,dy=std::isfinite(e.deltaY)?e.deltaY:0;
                    input.mouseX=std::clamp(input.mouseX+dx,-32768.0,32768.0);input.mouseY=std::clamp(input.mouseY+dy,-32768.0,32768.0);
                }
            } else if(e.type==NSEventTypeLeftMouseDown && menuOverlay.shown()) {
                NSPoint p=[nativeWindow.contentView convertPoint:e.locationInWindow fromView:nil];
                int row=menuOverlay.hit(CGPointMake(p.x,p.y));if(row>=0)wolfMenuClick=row;
            } else if(e.type==NSEventTypeLeftMouseDown || e.type==NSEventTypeLeftMouseUp || e.type==NSEventTypeRightMouseDown || e.type==NSEventTypeRightMouseUp || e.type==NSEventTypeOtherMouseDown || e.type==NSEventTypeOtherMouseUp) mouseButton(e);
            else if(e.type==NSEventTypeScrollWheel && canInput && activeGame()) {
                double delta=e.scrollingDeltaY*(e.hasPreciseScrollingDeltas?.08:1.0);
                if(std::isfinite(delta))input.wheel=std::clamp(input.wheel+delta,-8.0,8.0);
            } else [NSApp sendEvent:e];
        }
        [NSApp updateWindows];[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:NSDate.distantPast];
        padEvents();
        if(quitAsked && NSProcessInfo.processInfo.systemUptime-quitAskedAt>.25) { quitAsked=false; quitting=true; }
        if(quitting || interrupted) {events.clear();Wolf_Event e;e.type=Wolf_QUIT;events.push_back(e);quitting=false;interrupted=0;}
        if(metalLayer)updateMenuOverlay();
    }
}
static void presentMenuFrame() {
    double now=NSProcessInfo.processInfo.systemUptime,interval=Wolf_NativeFrameInterval()/1000;
    if(!menuRedraw && !(menuOverlay.shown() && now>=menuFrameDue)) { Wolf_RenderIdle();return; }
    menuFrameDue=now-menuFrameDue>interval?now+interval:menuFrameDue+interval;
    menuRedraw=false;Wolf_RenderPresent(nullptr);
}
int Wolf_Init(int) { started=NSProcessInfo.processInfo.systemUptime; return 0; }
void Wolf_Quit() { retireWolfHaptics();clearNativeInput(); Wolf_NativeAudioShutdown(); Wolf_SetRelativeMouseMode(false);presentation.shutdown(nativeWindow); }
const char *Wolf_GetError() { return "Native macOS backend initialization failed"; }
Uint32 Wolf_GetTicks() { double now=pausedAt?pausedAt:NSProcessInfo.processInfo.systemUptime; return Uint32((now-started-pausedTotal)*1000); }
double Wolf_NativeFrameInterval() { long rate=nativeWindow.screen.maximumFramesPerSecond;return 1000.0/(rate>0?rate:60); }
// Main-thread time spent waiting (sleeps, frame pacing) since the last display
// pass; the rest of the time between passes is work.
static double waited=0;
static void waitFor(std::chrono::microseconds span) { double start=NSProcessInfo.processInfo.systemUptime; std::this_thread::sleep_for(span); waited+=NSProcessInfo.processInfo.systemUptime-start; }
void Wolf_Delay(Uint32 ms) { double end=NSProcessInfo.processInfo.systemUptime+ms/1000.0; do { pump();if(focused)presentMenuFrame(); waitFor(std::chrono::milliseconds(focused?1:20)); } while((NSProcessInfo.processInfo.systemUptime<end || !focused) && std::none_of(events.begin(),events.end(),[](const Wolf_Event &e){return e.type==Wolf_QUIT;})); }
Wolf_Window *Wolf_CreateWindow(const char *,int,int,int w,int h,int flags) {
    device=MTLCreateSystemDefaultDevice(); queue=[device newCommandQueue]; if(!device || !queue)return nullptr;
    NSString *path=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"wolf.metallib"];
    NSError *error=nil; id<MTLLibrary> lib=[device newLibraryWithURL:[NSURL fileURLWithPath:path] error:&error];
    MTLRenderPipelineDescriptor *desc=[MTLRenderPipelineDescriptor new]; desc.vertexFunction=[lib newFunctionWithName:@"wolf_vertex"]; desc.fragmentFunction=[lib newFunctionWithName:@"wolf_fragment"];
    desc.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm_sRGB; pipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
    desc.fragmentFunction=[lib newFunctionWithName:@"wolf_status_fragment"];
    desc.colorAttachments[0].blendingEnabled=YES;
    desc.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    statusPipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
    if(!pipeline || !statusPipeline) { fprintf(stderr,"Wolf Metal: %s\n",error.localizedDescription.UTF8String); return nullptr; }
    nativeWindow=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,w,h*1.2) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    delegate=[WolfDelegate new]; nativeWindow.delegate=delegate; NSApp.delegate=delegate;
    nativeWindow.collectionBehavior=NSWindowCollectionBehaviorFullScreenPrimary;
    nativeWindow.title=@"Wolfenstein 3D"; nativeWindow.contentMinSize=NSMakeSize(320,240); nativeWindow.backgroundColor=NSColor.blackColor;
    metalLayer=[CAMetalLayer layer]; metalLayer.device=device; metalLayer.pixelFormat=MTLPixelFormatBGRA8Unorm_sRGB; metalLayer.framebufferOnly=YES;
    metalLayer.displaySyncEnabled=YES;metalLayer.maximumDrawableCount=3;metalLayer.allowsNextDrawableTimeout=YES;
    CGColorSpaceRef colorSpace=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);metalLayer.colorspace=colorSpace;CGColorSpaceRelease(colorSpace);
    nativeWindow.contentView.wantsLayer=YES; nativeWindow.contentView.layer=metalLayer;
    menuOverlay.build(NativeMenuTheme::wolf());
    if(!menuOverlay.setupMetal(device,lib,MTLPixelFormatBGRA8Unorm_sRGB,false))return nullptr;
    // Every option lives in the in-game menu; the menu bar stays empty.
    NSApp.mainMenu=[NSMenu new]; [nativeWindow center]; [nativeWindow makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    NSNotificationCenter *workspace=NSWorkspace.sharedWorkspace.notificationCenter;
    [workspace addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { awake=false;setWolfFocus(false); }];
    [workspace addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { awake=true;setWolfFocus(NSApp.active && nativeWindow.keyWindow); }];
    // The window delegate is not the application delegate, so app activation
    // arrives here. Coming back after Command-Tab, the game window becomes key
    // again and play resumes; without this the session stayed paused.
    NSNotificationCenter *center=NSNotificationCenter.defaultCenter;
    [center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
        if(!nativeWindow.keyWindow)[nativeWindow makeKeyAndOrderFront:nil];
        setWolfFocus(true);
    }];
    [center addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) { setWolfFocus(false); }];
    windowEvent(Wolf_WINDOWEVENT_FOCUS_GAINED); windowEvent(Wolf_WINDOWEVENT_RESTORED);
    if(flags&Wolf_WINDOW_FULLSCREEN_DESKTOP)[delegate fullscreen:nil];
    return new Wolf_Window;
}
void Wolf_SetWindowMinimumSize(Wolf_Window *,int w,int h) { nativeWindow.contentMinSize=NSMakeSize(w,h); }
int Wolf_GetWindowPixelFormat(Wolf_Window *) { return 32; }
void Wolf_GetWindowSize(Wolf_Window *,int *w,int *h) { *w=nativeWindow.contentView.bounds.size.width; *h=nativeWindow.contentView.bounds.size.height; }
Uint32 Wolf_GetWindowID(Wolf_Window *) { return 1; }
void Wolf_SetWindowSize(Wolf_Window *,int w,int h) { [nativeWindow setContentSize:NSMakeSize(w,h)]; }
int Wolf_SetWindowFullscreen(Wolf_Window *,Uint32 flags) { if(presentation.active(nativeWindow)!=(flags!=0))[delegate fullscreen:nil]; return 0; }
int Wolf_SetRelativeMouseMode(int mode) { bool value=mode && focused; if(value!=relative) { CGAssociateMouseAndMouseCursorPosition(!value); if(value)[NSCursor hide];else[NSCursor unhide]; relative=value; } return 0; }
void Wolf_WarpMouseInWindow(Wolf_Window *,int,int) {}
Uint32 Wolf_GetRelativeMouseState(int *x,int *y) {
    int dx=int(input.mouseX),dy=int(input.mouseY);
    if(x){*x=dx;input.mouseX-=dx;}else input.mouseX=0;
    if(y){*y=dy;input.mouseY-=dy;}else input.mouseY=0;
    return (input.mouseButtons&1?Wolf_BUTTON(Wolf_BUTTON_LEFT):0)|(input.mouseButtons&2?Wolf_BUTTON(Wolf_BUTTON_RIGHT):0)|(input.mouseButtons&4?Wolf_BUTTON(Wolf_BUTTON_MIDDLE):0);
}
Wolf_Renderer *Wolf_CreateRenderer(Wolf_Window *,int,int) { return new Wolf_Renderer; }
int Wolf_RenderSetLogicalSize(Wolf_Renderer *,int w,int h) { logicalWidth=w;logicalHeight=h;return 0; }
// Adaptive render resolution (RenderScale.hpp).
static native_render::RenderScale renderScale;
// Each display pass: the main thread's work since the previous one and the
// software rendering inside it steer the render size.
static void governRenderScale(double workMs,double renderMs) {
    static int map=-1,episode=-1,thermal=0,poll=0; static bool lowPower=false;
    // A level's first frames carry its loading work.
    if(gamestate.mapon!=map || gamestate.episode!=episode) { renderScale.settle(120); map=gamestate.mapon; episode=gamestate.episode; }
    // Frames after refocusing carry window and fullscreen work.
    if(focusReturned) { renderScale.settle(120); focusReturned=false; }
    if(poll--<=0) { poll=30; thermal=int(NSProcessInfo.processInfo.thermalState); lowPower=NSProcessInfo.processInfo.lowPowerModeEnabled; }
    renderScale.frame(workMs,renderMs,Wolf_NativeFrameInterval(),thermal,lowPower);
}
double Wolf_NativePresentInterval() { return renderScale.intervalMs()>0?renderScale.intervalMs():Wolf_NativeFrameInterval(); }
bool Wolf_NativeRenderSize(unsigned *w,unsigned *h) {
    NSSize pixels=[nativeWindow.contentView convertSizeToBacking:nativeWindow.contentView.bounds.size];
    if(!std::isfinite(pixels.width) || !std::isfinite(pixels.height) || pixels.width<=0 || pixels.height<=0)return false;
    double scale=std::min({1.0,4096.0/pixels.width,2560.0/pixels.height});
    scale*=renderScale.scale();
    *w=std::max(320u,unsigned(pixels.width*scale)&~1u);*h=std::max(200u,unsigned(pixels.height*scale)&~1u);
    return true;
}
Wolf_Texture *Wolf_CreateTexture(Wolf_Renderer *,int,int,int w,int h) {
    if(w<1 || h<1 || w>4096 || h>2560)return nullptr;
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint width:w height:h mipmapped:NO];d.storageMode=MTLStorageModeShared; d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
    // Streamed every frame: a plain copy, not the CPU-side compression the
    // driver otherwise performs on each upload.
    d.allowGPUOptimizedContents=NO;
    id<MTLTexture> replacement[3];for(int i=0;i<3;++i){replacement[i]=[device newTextureWithDescriptor:d];if(!replacement[i])return nullptr;}
    std::vector<uint8_t> pixels;
    try {pixels.resize(size_t(w)*h);}catch(const std::bad_alloc &){return nullptr;}
    Wolf_Texture *texture=new(std::nothrow)Wolf_Texture;if(!texture)return nullptr;
    // Submitted command buffers retain old textures; their completion handlers
    // signal their own semaphore. Resizing does not wait on the main thread.
    for(int i=0;i<3;++i)frameTextures[i]=replacement[i];image=frameTextures[0];
    availableFrames=dispatch_semaphore_create(3);frameSlot=0;pendingPixels.swap(pixels);pendingFresh=true;return texture;
}
// The frame's palette indices and display colors were recorded by the blit
// into the presented surface (Software.cpp); the GPU resolves the colors.
static uint32_t pendingColors[256];
// The status card of the pending frame, captured with it.
static std::vector<uint8_t> pendingStatus;
static int statusWidth=0,statusHeight=0;
static bool pendingCard=false,shownCard=false;
int Wolf_UpdateTexture(Wolf_Texture *,const void *,const void *,int) {
    if(wolfMenuFrozen)return 0;
    Wolf_IndexFrame &f=Wolf_PendingFrame;
    if(!image || !f.fresh || pendingPixels.size()!=size_t(image.width)*image.height || f.indices.size()!=pendingPixels.size())return -1;
    pendingPixels.swap(f.indices); memcpy(pendingColors,f.colors,sizeof(pendingColors));
    pendingCard=f.view && statusShown();
    if(pendingCard) {
        const Uint8 *status=Wolf_StatusImage(&statusWidth,&statusHeight);
        if(status) pendingStatus.assign(status,status+size_t(statusWidth)*statusHeight); else pendingCard=false;
    }
    f.fresh=false; pendingFresh=true;return 0;
}
// The card at the foot of the view, in drawable pixels: the status bar at a
// whole-number scale (every original pixel an equal square), as in MetalDoom.
static CGRect statusCard(CGSize drawable) {
    double scale=nativeWindow?nativeWindow.backingScaleFactor:1;
    // The largest whole multiple of the original bar within 55% of the
    // width and an eighth of the height: readable, never in the way.
    double k=std::max(1.0,std::floor(std::min(drawable.width*.55/320,drawable.height*.125/40)));
    double width=320*k,height=40*k,margin=std::round(8*scale);
    return CGRectMake(std::floor((drawable.width-width)/2),drawable.height-height-margin,width,height);
}
int Wolf_NativeWeaponLift() {
    if(!statusShown() || !nativeWindow || !screenHeight) return 0;
    NSSize size=[nativeWindow.contentView convertSizeToBacking:nativeWindow.contentView.bounds.size];
    if(size.height<1) return 0;
    CGRect card=statusCard(NSSizeToCGSize(size));
    return int(std::lround((size.height-card.origin.y)*screenHeight/size.height));
}
int Wolf_RenderClear(Wolf_Renderer *) { return 0; }
int Wolf_RenderCopy(Wolf_Renderer *,Wolf_Texture *,const void *,const void *) { return 0; }
// Frames submitted but not yet on screen.
static std::atomic<int> framesQueued{0};
// Before a display pass: at most one finished frame may still wait for the
// display, so each picture is drawn from the newest game state instead of
// queueing behind older ones (one refresh less latency). Bounded, so a frame
// the system never shows cannot stall the game.
void Wolf_NativeAwaitFrame() {
    double deadline=NSProcessInfo.processInfo.systemUptime+.1;
    while(framesQueued>1 && NSProcessInfo.processInfo.systemUptime<deadline) waitFor(std::chrono::microseconds(250));
}
extern bool wolfPresentationOnly;
void Wolf_RenderPresent(Wolf_Renderer *) { @autoreleasepool {
    // A display pass's software rendering ends here, before the drawable wait.
    if(wolfPresentationOnly) {
        Native_MetricsRenderEnd();
        static double lastPass=0; double now=NSProcessInfo.processInfo.systemUptime;
        double renderMs=Native_MetricsTakeRenderMs();
        if(lastPass>0) governRenderScale((now-lastPass-waited)*1000,renderMs);
        lastPass=now; waited=0;
    }
    pump();if(!focused || !awake || presentation.transitioning || nativeWindow.miniaturized || !image)return;
    NSSize size=[nativeWindow.contentView convertSizeToBacking:nativeWindow.contentView.bounds.size];metalLayer.contentsScale=nativeWindow.backingScaleFactor; metalLayer.drawableSize=CGSizeMake(size.width,size.height);
    double blocked=NSProcessInfo.processInfo.systemUptime;
    id<CAMetalDrawable> drawable=[metalLayer nextDrawable]; if(!drawable)return;
    if(dispatch_semaphore_wait(availableFrames,dispatch_time(DISPATCH_TIME_NOW,250*NSEC_PER_MSEC)))return;
    waited+=NSProcessInfo.processInfo.systemUptime-blocked;
    // Menu frames repeat a frozen game image; it is uploaded only when it changes.
    static id<MTLTexture> statusImage;
    if(pendingFresh) {
        unsigned slot=frameSlot++%3;
        image=frameTextures[slot];pendingFresh=false;
        [image replaceRegion:MTLRegionMake2D(0,0,image.width,image.height) mipmapLevel:0 withBytes:pendingPixels.data() bytesPerRow:image.width];
        shownCard=pendingCard;
        if(shownCard) {
            id<MTLTexture> __strong &texture=statusTextures[slot];
            if(texture.width!=NSUInteger(statusWidth) || texture.height!=NSUInteger(statusHeight)) {
                MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint width:statusWidth height:statusHeight mipmapped:NO];
                d.storageMode=MTLStorageModeShared; d.usage=MTLTextureUsageShaderRead;
                texture=[device newTextureWithDescriptor:d];
            }
            [texture replaceRegion:MTLRegionMake2D(0,0,statusWidth,statusHeight) mipmapLevel:0 withBytes:pendingStatus.data() bytesPerRow:statusWidth];
            statusImage=texture;
        }
    }
    updateMenuOverlay(true);
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor]; pass.colorAttachments[0].texture=drawable.texture; pass.colorAttachments[0].loadAction=MTLLoadActionClear; pass.colorAttachments[0].storeAction=MTLStoreActionStore; pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
    id<MTLCommandBuffer> cmd=[queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder=[cmd renderCommandEncoderWithDescriptor:pass];
    if(!cmd || !encoder){dispatch_semaphore_signal(availableFrames);return;}
    [encoder setViewport:MTLViewport{0,0,size.width,size.height,0,1}]; [encoder setRenderPipelineState:pipeline]; [encoder setFragmentTexture:image atIndex:0];
    // A frame rendered below native size by the adaptive resolution is resampled smoothly.
    uint32_t filtered=renderScale.sixteenths()<16 && image.width+1<size.width; [encoder setFragmentBytes:&filtered length:sizeof(filtered) atIndex:0]; [encoder setFragmentBytes:pendingColors length:sizeof(pendingColors) atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    if(shownCard && statusImage) {
        CGRect card=statusCard(NSSizeToCGSize(size));
        float shape[]={float(card.size.width),float(card.size.height),float(6*nativeWindow.backingScaleFactor),0};
        [encoder setRenderPipelineState:statusPipeline];
        [encoder setViewport:MTLViewport{card.origin.x,card.origin.y,card.size.width,card.size.height,0,1}];
        [encoder setFragmentTexture:statusImage atIndex:0]; [encoder setFragmentBytes:pendingColors length:sizeof(pendingColors) atIndex:1];
        [encoder setFragmentBytes:shape length:sizeof(shape) atIndex:2];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    menuOverlay.encode(encoder,size.width,size.height); [encoder endEncoding]; ++framesQueued; [drawable addPresentedHandler:^(id<MTLDrawable>){ --framesQueued; }];
    // A fast display that cannot be kept up with is paced at 60 Hz.
    if(renderScale.intervalMs()>Wolf_NativeFrameInterval()*1.5) [cmd presentDrawable:drawable afterMinimumDuration:renderScale.intervalMs()/1000]; else [cmd presentDrawable:drawable];menuRedraw=false; dispatch_semaphore_t submittedFrames=availableFrames;[cmd addCompletedHandler:^(id<MTLCommandBuffer> completed){dispatch_semaphore_signal(submittedFrames);if(completed.status==MTLCommandBufferStatusError)fprintf(stderr,"Wolf Metal command failed\n");}]; [cmd commit];
} }
int Wolf_PollEvent(Wolf_Event *e) { pump(); if(events.empty())return 0;*e=events.front();events.pop_front();return 1; }
int Wolf_WaitEvent(Wolf_Event *e) { while(!Wolf_PollEvent(e))Wolf_Delay(5);return 1; }
int Wolf_PushEvent(Wolf_Event *e) { events.push_back(*e);return 1; }
Wolf_Keymod Wolf_GetModState() { return mods; }
// Gameplay pad commands are sampled once by PollControls, after original input
// and before original command bounds/demo recording. No timer injects shots.
void Wolf_NativeCommands() {
    input.side=0;acknowledging=false;
    if(!activeGame()) {input.scan=0;input.hud=false;input.wheel=0;commandFiring=false;return;}
    if(input.hud) {input.hud=false;toggleHUD();}
    if(input.scan) {LastScan=input.scan;input.scan=0;}
    // A scroll notch produces one original weapon-cycle command, with a release
    // step between notches so the core's normal edge semantics stay intact.
    if(!buttonheld[bt_prevweapon] && !buttonheld[bt_nextweapon] && fabs(input.wheel)>=1) {
        buttonstate[input.wheel>0?bt_prevweapon:bt_nextweapon]=true;
        input.wheel-=input.wheel>0?1:-1;
    }
    GCExtendedGamepad *p=controller.extendedGamepad;if(!p) {commandFiring=false;return;}
    const auto &buttons=input.buttons;
    bool down=false;for(int i=0;i<17;++i)down|=buttons[i] && padLayout[i]==PadFire;
    bool firing=fireGate.sample(down,gamestate.weapon==wp_pistol?1:gamestate.weapon==wp_knife?0:3,true);
    buttonstate[bt_attack]|=firing;commandFiring=firing;
    for(int i=0;i<17;++i)if(buttons[i]) switch(padLayout[i]) {
        case PadUse: buttonstate[bt_use]=true; break;
        case PadRun: buttonstate[bt_run]=true; break;
        case PadPrevious: buttonstate[bt_prevweapon]=true; break;
        case PadNext: buttonstate[bt_nextweapon]=true; break;
        case PadMenu: buttonstate[bt_esc]=true; break;
        case PadPause: buttonstate[bt_pause]=true; break;
        case PadKnife: buttonstate[bt_readyknife]=true; break;
        case PadPistol: buttonstate[bt_readypistol]=true; break;
        case PadMachineGun: buttonstate[bt_readymachinegun]=true; break;
        case PadChainGun: buttonstate[bt_readychaingun]=true; break;
        default: break;
    }
    const bool running=buttonstate[bt_run]^always_run;
    // Analog movement: speed follows tilt linearly; the last 10% of travel
    // eases into running speed, so a full push runs without a button.
    auto moveRate=[&](float value) { return stick::moveRate(value,BASEMOVE,RUNMOVE,running)*tics; };
    controly-=int(std::lround(moveRate(input.axes[1])));
    float side=input.axes[0],turn=input.axes[2];
    // Turning: full tilt is 4 degrees per tic (280 deg/s) at sensitivity 1,
    // at once, with no ramp-in.
    turn=std::clamp(turn,-1.f,1.f);
    float shaped=std::copysign(stick::turnCurve(std::fabs(turn)),turn);
    controlx+=WolfInputState::turnCommand(shaped,turnSensitivity,80,tics);
    float strafe=std::clamp(side,-1.f,1.f);
    input.side=int(std::lround(moveRate(strafe)));
    updateWolfTriggers();
}
int Wolf_NativeStrafeCommand() { return activeGame()?input.side:0; }
// Native GameController supplies commands above; the old joystick paths remain
// inert so the same physical input cannot be applied twice.
int Wolf_NumJoysticks() { return 0; } Wolf_Joystick *Wolf_JoystickOpen(int) { return nullptr; } void Wolf_JoystickClose(Wolf_Joystick *) {}
int Wolf_JoystickEventState(int) { return 0; } void Wolf_JoystickUpdate() {} Sint16 Wolf_JoystickGetAxis(Wolf_Joystick *,int) { return 0; }
Uint8 Wolf_JoystickGetButton(Wolf_Joystick *,int) { return 0; } Uint8 Wolf_JoystickGetHat(Wolf_Joystick *,int) { return 0; }
int Wolf_JoystickNumButtons(Wolf_Joystick *) { return 0; } int Wolf_JoystickNumHats(Wolf_Joystick *) { return 0; }
// Where the damage came from, relative to where the player faces (Wolf's
// map y grows southward).
void Wolf_NativeFeedbackSource(void *attacker) {
    const auto *from=static_cast<const objtype *>(attacker);
    if(!from || !player)return;
    double toward=std::atan2(-double(from->y-player->y),double(from->x-player->x))-player->angle*M_PI/180;
    damageBalance=float(-std::sin(toward));damageBalanceUntil=NSProcessInfo.processInfo.systemUptime+.1;
}
void Wolf_NativeFeedback(int kind,int detail) {
    if(!activeGame() || !controller)return;
    // Each original Wolf weapon maps to a recoil profile, played only
    // after the engine executes its attack: the knife as fists, the pistol
    // as the pistol, both automatics one kick per round (the MP40 lighter).
    int weapon=detail==wp_knife?0:detail==wp_pistol?1:3;
    auto effect=feedback::hapticEffect(kind,kind==1?weapon:detail);
    float scale=kind==1 && detail==wp_machinegun?.8f:1.f;
    double now=NSProcessInfo.processInfo.systemUptime;
    const int fire=2; // R2 fires
    @try {
        rumbleDevice.play(effect,hapticsEnabled?hapticIntensity*scale:0,triggersEnabled?triggerIntensity*scale:0,fire,kind==2 && now<damageBalanceUntil?damageBalance:0);
    } @catch(NSException *exception) {
        fprintf(stderr,"Wolf haptic device interrupted: %s\n",exception.reason.UTF8String);
        rumbleDevice.attach(controller);
    }
}
extern int Wolf_GameMain(int,char **);
static void signalHandler(int) { interrupted=1; }
static std::vector<char *> launchArguments;
static int launchResult;
@interface WolfLaunch : NSObject <NSApplicationDelegate>
- (void)startGame;
@end
@implementation WolfLaunch
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    // Enter outside a dispatch-main callback so
    // nested event pumping can service GameController/CoreHaptics callbacks.
    [self performSelector:@selector(startGame) withObject:nil afterDelay:0];
}
- (void)startGame {
    launchResult=Wolf_GameMain(int(launchArguments.size()-1),launchArguments.data());
    [NSApp stop:nil];
}
@end
int main(int argc,char **argv) { @autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];readPreferences();
    std::signal(SIGINT,signalHandler);std::signal(SIGTERM,signalHandler);
    for(int i=0;i<argc;++i)launchArguments.push_back(argv[i]);
    fullscreen=true;
    launchArguments.push_back(nullptr);
    WolfLaunch *launch=[WolfLaunch new];NSApp.delegate=launch;[NSApp run];
    return launchResult;
} }
