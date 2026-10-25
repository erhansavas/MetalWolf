// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Private software-port adapter; implementations use Apple frameworks. This is
// not SDL and no SDL code, headers, library or runtime is used by the application.
#include <cstdint>
#include <mutex>
#include "WolfKeys.hpp"
using Uint8=uint8_t; using Uint16=uint16_t; using Uint32=uint32_t; using Sint16=int16_t;
using Wolf_Keymod=int;
struct Wolf_Color { Uint8 r,g,b,a; };
struct Wolf_Palette { Wolf_Color colors[256]{}; };
struct Wolf_PixelFormat { int BytesPerPixel=1; Wolf_Palette *palette=nullptr; };
struct Wolf_Surface { int w,h,pitch; void *pixels; Wolf_PixelFormat *format; };
struct Wolf_Window {}; struct Wolf_Renderer {}; struct Wolf_Texture {}; struct Wolf_Joystick {};
struct Wolf_Keysym { int sym=0,scancode=0,mod=0; };
struct Wolf_WindowEvent { Uint32 windowID=1; int event=0; };
struct Wolf_Event {
    int type=0;
    struct { Wolf_Keysym keysym; } key;
    struct { int button=0; } button;
    struct { int x=0,y=0; } wheel;
    Wolf_WindowEvent window;
};
using Wolf_mutex=std::recursive_mutex;
constexpr int KMOD_LALT=1,KMOD_RALT=2,KMOD_LGUI=4,KMOD_RGUI=8,KMOD_SHIFT=16,KMOD_CAPS=32,KMOD_NUM=64;
constexpr int Wolf_FALSE=0,Wolf_TRUE=1,Wolf_ENABLE=1,Wolf_BUTTON_LEFT=1,Wolf_BUTTON_MIDDLE=2,Wolf_BUTTON_RIGHT=3;
constexpr int Wolf_HAT_UP=1,Wolf_HAT_RIGHT=2,Wolf_HAT_DOWN=4,Wolf_HAT_LEFT=8;
constexpr int Wolf_INIT_AUDIO=1,Wolf_INIT_VIDEO=2,Wolf_INIT_JOYSTICK=4;
constexpr int Wolf_WINDOW_FULLSCREEN_DESKTOP=1,Wolf_WINDOW_ALLOW_HIGHDPI=2,Wolf_WINDOW_RESIZABLE=4,Wolf_WINDOWPOS_CENTERED=0;
constexpr int Wolf_RENDERER_PRESENTVSYNC=1,Wolf_RENDERER_SOFTWARE=2,Wolf_TEXTUREACCESS_STREAMING=1;
constexpr int Wolf_AUDIO_ALLOW_FREQUENCY_CHANGE=1,Wolf_SCANCODE_RETURN=WK_RETURN,Wolf_SCANCODE_KP_ENTER=WK_KP_ENTER;
constexpr int Wolf_KEYDOWN=1,Wolf_KEYUP=2,Wolf_MOUSEBUTTONDOWN=3,Wolf_MOUSEBUTTONUP=4,Wolf_MOUSEWHEEL=5,Wolf_QUIT=6,Wolf_WINDOWEVENT=7;
constexpr int Wolf_WINDOWEVENT_FOCUS_GAINED=1,Wolf_WINDOWEVENT_FOCUS_LOST=2,Wolf_WINDOWEVENT_MINIMIZED=3,Wolf_WINDOWEVENT_MAXIMIZED=4,Wolf_WINDOWEVENT_RESIZED=5,Wolf_WINDOWEVENT_RESTORED=6;
constexpr const char *Wolf_HINT_RENDER_SCALE_QUALITY="nearest";
constexpr Uint16 AUDIO_U8=8,AUDIO_S16SYS=16;
constexpr int MIX_CHANNELS=8,MIX_MAX_VOLUME=128;
inline int Wolf_BUTTON(int n) { return 1<<(n-1); }
inline bool Wolf_MUSTLOCK(Wolf_Surface *) { return false; }
int Wolf_Init(int); void Wolf_Quit(); const char *Wolf_GetError();
Uint32 Wolf_GetTicks(); void Wolf_Delay(Uint32 milliseconds);
Wolf_Window *Wolf_CreateWindow(const char *,int,int,int,int,int);
void Wolf_SetWindowMinimumSize(Wolf_Window *,int,int); int Wolf_GetWindowPixelFormat(Wolf_Window *);
void Wolf_GetWindowSize(Wolf_Window *,int *,int *); Uint32 Wolf_GetWindowID(Wolf_Window *);
void Wolf_SetWindowSize(Wolf_Window *,int,int); int Wolf_SetWindowFullscreen(Wolf_Window *,Uint32);
int Wolf_SetRelativeMouseMode(int); void Wolf_WarpMouseInWindow(Wolf_Window *,int,int);
Uint32 Wolf_GetRelativeMouseState(int *,int *);
Wolf_Renderer *Wolf_CreateRenderer(Wolf_Window *,int,int); int Wolf_RenderSetLogicalSize(Wolf_Renderer *,int,int);
Wolf_Texture *Wolf_CreateTexture(Wolf_Renderer *,int,int,int,int);
int Wolf_SetHint(const char *,const char *); int Wolf_UpdateTexture(Wolf_Texture *,const void *,const void *,int);
int Wolf_RenderClear(Wolf_Renderer *); int Wolf_RenderCopy(Wolf_Renderer *,Wolf_Texture *,const void *,const void *); void Wolf_RenderPresent(Wolf_Renderer *);
int Wolf_PixelFormatEnumToMasks(int,int *,Uint32 *,Uint32 *,Uint32 *,Uint32 *);
Wolf_Surface *Wolf_CreateRGBSurface(int,int,int,int,Uint32,Uint32,Uint32,Uint32);
void Wolf_FreeSurface(Wolf_Surface *); int Wolf_LockSurface(Wolf_Surface *); void Wolf_UnlockSurface(Wolf_Surface *);
Wolf_Palette *Wolf_AllocPalette(int); void Wolf_FreePalette(Wolf_Palette *);
int Wolf_SetPaletteColors(Wolf_Palette *,const Wolf_Color *,int,int); int Wolf_SetSurfacePalette(Wolf_Surface *,Wolf_Palette *);
int Wolf_FillRect(Wolf_Surface *,const void *,Uint32); int Wolf_BlitSurface(Wolf_Surface *,const void *,Wolf_Surface *,void *);
Uint32 Wolf_MapRGB(Wolf_PixelFormat *,Uint8,Uint8,Uint8); int Wolf_SaveBMP(Wolf_Surface *,const char *);
int Wolf_PollEvent(Wolf_Event *); int Wolf_WaitEvent(Wolf_Event *); int Wolf_PushEvent(Wolf_Event *); Wolf_Keymod Wolf_GetModState();
int Wolf_NumJoysticks(); Wolf_Joystick *Wolf_JoystickOpen(int); void Wolf_JoystickClose(Wolf_Joystick *);
int Wolf_JoystickEventState(int); void Wolf_JoystickUpdate(); Sint16 Wolf_JoystickGetAxis(Wolf_Joystick *,int);
Uint8 Wolf_JoystickGetButton(Wolf_Joystick *,int); Uint8 Wolf_JoystickGetHat(Wolf_Joystick *,int);
int Wolf_JoystickNumButtons(Wolf_Joystick *); int Wolf_JoystickNumHats(Wolf_Joystick *);
Wolf_mutex *Wolf_CreateMutex(); void Wolf_DestroyMutex(Wolf_mutex *); int Wolf_LockMutex(Wolf_mutex *); int Wolf_UnlockMutex(Wolf_mutex *);
struct Wolf_AudioCVT { Uint8 *buf=nullptr; int len=0,len_mult=0,len_cvt=0; int sourceRate=0,targetRate=0; };
int Wolf_BuildAudioCVT(Wolf_AudioCVT *,Uint16,int,int,Uint16,int,int); int Wolf_ConvertAudio(Wolf_AudioCVT *);
struct WolfMix_Chunk { int allocated=0; Uint8 *abuf=nullptr; Uint32 alen=0; Uint8 volume=MIX_MAX_VOLUME; };
int WolfMix_OpenAudioDevice(int,Uint16,int,int,const char *,int); int WolfMix_QuerySpec(int *,Uint16 *,int *);
int WolfMix_ReserveChannels(int); int WolfMix_GroupChannels(int,int,int); int WolfMix_GroupAvailable(int); int WolfMix_GroupOldest(int);
int WolfMix_SetPanning(int,Uint8,Uint8); int WolfMix_PlayChannel(int,WolfMix_Chunk *,int); int WolfMix_HaltChannel(int);
const char *WolfMix_GetError(); void WolfMix_HookMusic(void (*)(void *,Uint8 *,int),void *);
void WolfMix_SetPostMix(void (*)(void *,Uint8 *,int),void *); void WolfMix_ChannelFinished(void (*)(int));
void Wolf_NativeFeedback(int kind,int detail); void Wolf_NativeReset();
void Wolf_NativeFeedbackSource(void *attacker);

void Wolf_NativeAudioShutdown();
void Wolf_NativeAudioSuspend(bool);

void Wolf_NativeCommands();
void Wolf_NativeAcknowledge();
int Wolf_NativeStrafeCommand();


extern int wolfExitStatus;
// In-game menu equivalents of the former menu bar items.
bool Wolf_NativeFullscreen();
void Wolf_NativeToggleFullscreen();
// Controller settings rows (2026-10-10), saved for Wolfenstein immediately.
enum { WOLF_PAD_DEADZONE, WOLF_PAD_TURN, WOLF_PAD_HAPTICS, WOLF_PAD_HAPTIC_STRENGTH, WOLF_PAD_TRIGGERS, WOLF_PAD_TRIGGER_STRENGTH };
int Wolf_NativePadValue(int setting);
// The connected controller: 0 none, 1 DualSense, 2 any other. Its name and
// the names printed on its buttons (NULL when it has no such button), and
// whether it can vibrate and has trigger effects.
enum { WOLF_BUTTON_NAME, WOLF_BUTTON_SOUTH, WOLF_BUTTON_EAST, WOLF_BUTTON_WEST, WOLF_BUTTON_NORTH, WOLF_BUTTON_LB, WOLF_BUTTON_RB,
       WOLF_BUTTON_LT, WOLF_BUTTON_RT, WOLF_BUTTON_MENU, WOLF_BUTTON_PAUSE, WOLF_BUTTON_TOUCHPAD };
int Wolf_NativePadKind();
const char *Wolf_NativePadButton(int button);
bool Wolf_NativePadVibration();
bool Wolf_NativePadTriggerEffects();
void Wolf_NativePadAdjust(int setting,int direction);
// Native menu panel (2026-10-10): the running list page, if it has one.
int Wolf_NativeMenuPage(char *title,int capacity);
int Wolf_NativeMenuRow(int index,char *label,int capacity);
int Wolf_NativeMenuSelection();
extern int wolfMenuClick;
// The row under a moving pointer, selected without choosing it.
extern int wolfMenuHover;
// While the control panel is open the last game or title frame stays on
// screen behind the native panel, and palette fades complete at once.
extern bool wolfMenuFrozen;
bool Wolf_NativeMenuNoticeConfirm();
// Native menu cues (2026-10-10): the panel's own synthesized tones. The menu
// maps the game sounds it borrows onto them while it is open.
enum { WOLF_CUE_MOVE, WOLF_CUE_ADJUST, WOLF_CUE_SELECT, WOLF_CUE_BACK, WOLF_CUE_DENY, WOLF_CUE_START, WOLF_CUE_QUIT, WOLF_CUE_COUNT };
void Wolf_NativeMenuCue(int kind);
void Wolf_NativeQuitFade();
// Command-Q asks first: true once, after which the game shows its quit confirmation.
bool Wolf_NativeQuitAsked();
bool Wolf_MenuCue(int sound);

// Native drawable pixels; original diagnostic mode can retain its small fixture.
bool Wolf_NativeRenderSize(unsigned *,unsigned *);
void VL_NativeResize();

extern bool wolfPresentationOnly;
bool Wolf_RenderEnabled();
// A game step's own refresh while display passes run: never shown.
bool Wolf_RenderUnseen();
void Wolf_RenderReset();
void Wolf_RenderBeforeStep();
void Wolf_RenderAfterStep();
void Wolf_RenderWaiting(bool);
void Wolf_RenderIdle();
void Wolf_NativeAwaitFrame();
double Wolf_NativeFrameInterval();
// Interval display passes are paced to: the display's, or twice it while the
// adaptive resolution paces a fast display at 60 Hz.
double Wolf_NativePresentInterval();
void Wolf_GoreHit(void *,unsigned);
void Wolf_NativeGoreSound(int kind,int left,int right);
void Wolf_GoreFlinch(const void *actor,int32_t *x,int32_t *y);
void Wolf_GoreBeginFrame();
void Wolf_GoreWallHit(int column,int orientation,int32_t plane,int32_t along,int step);
void Wolf_GoreDrawSurfaces(void *,unsigned);
void Wolf_GoreDraw(void *,unsigned);
// The presented surface's frames stay 8-bit: the blit into it records the
// palette indices and display colors, and Metal resolves them per pixel.
#include <vector>
extern Wolf_Surface *Wolf_PresentSurface;
struct Wolf_IndexFrame { std::vector<uint8_t> indices; uint32_t colors[256]; int width=0,height=0; bool fresh=false,view=false; };
// Native status card (2026-10-25): the original status bar, drawn at its own
// 320x40 size (wl_game.cpp) and shown as a card at the foot of the full-screen
// view. True while ThreeDRefresh presents a gameplay view.
extern bool wolfViewFrame;
const Uint8 *Wolf_StatusImage(int *width,int *height);
// Saved setting, on by default; the touchpad toggles it.
bool Wolf_NativeStatusBar();
void Wolf_NativeSetStatusBar(bool on);
// Render rows the weapon rises by so the card does not cover it.
int Wolf_NativeWeaponLift();
extern Wolf_IndexFrame Wolf_PendingFrame;
void *Wolf_RenderPlayer();
// The presented view angle in degrees with its fraction, while presenting.
bool Wolf_RenderViewAngle(double *degrees);
Uint8 (*Wolf_RenderVisibility())[64];
bool Wolf_RenderStaticVisible(const Uint8 *);
Uint16 Wolf_RenderDoorPosition(unsigned);
Uint16 Wolf_RenderPushPosition();
