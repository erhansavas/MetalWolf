// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
//
//	ID Engine
//	ID_IN.c - Input Manager
//	v1.0d1
//	By Jason Blochowiak
//

//
//	This module handles dealing with the various input devices
//
//	Depends on: Memory Mgr (for demo recording), Sound Mgr (for timing stuff),
//				User Mgr (for command line parms)
//
//	Globals:
//		LastScan - The keyboard scan code of the last key pressed
//		LastASCII - The ASCII value of the last key pressed
//	DEBUG - there are more globals
//

#include "wl_def.h"


/*
=============================================================================

					GLOBAL VARIABLES

=============================================================================
*/


//
// configuration variables
//
boolean MousePresent;
boolean grabmouse = true;

// There's no WK_LAST anymore. If your program had a lookup table of
// WK_LAST elements, to map between SDL keys and whatever your
// application wanted internally, that's no longer feasible. Use a hash
// table instead. A std::map will do.
// <https://wiki.libsdl.org/MigrationGuide>

// 	Global variables
std::unordered_map<ScanCode, boolean> Keyboard;
volatile boolean	Paused;
volatile char		LastASCII;
ScanCode	LastScan;

//KeyboardDef	KbdDefs = {0x1d,0x38,0x47,0x48,0x49,0x4b,0x4d,0x4f,0x50,0x51};
static KeyboardDef KbdDefs = {
    sc_Control,             // button0
    sc_Alt,                 // button1
    sc_Home,                // upleft
    sc_UpArrow,             // up
    sc_PgUp,                // upright
    sc_LeftArrow,           // left
    sc_RightArrow,          // right
    sc_End,                 // downleft
    sc_DownArrow,           // down
    sc_PgDn                 // downright
};

static Wolf_Joystick *Joystick;
int JoyNumButtons;
static int JoyNumHats;

/*
=============================================================================

					LOCAL VARIABLES

=============================================================================
*/
byte        ASCIINames[] =		// Unshifted ASCII for scan codes       // TODO: keypad
{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,8  ,9  ,0  ,0  ,0  ,13 ,0  ,0  ,	// 0
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,27 ,0  ,0  ,0  ,	// 1
	' ',0  ,0  ,0  ,0  ,0  ,0  ,39 ,0  ,0  ,'*','+',',','-','.','/',	// 2
	'0','1','2','3','4','5','6','7','8','9',0  ,';',0  ,'=',0  ,0  ,	// 3
	'`','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',	// 4
	'p','q','r','s','t','u','v','w','x','y','z','[',92 ,']',0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0		// 7
};
byte ShiftNames[] =		// Shifted ASCII for scan codes
{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,8  ,9  ,0  ,0  ,0  ,13 ,0  ,0  ,	// 0
    0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,27 ,0  ,0  ,0  ,	// 1
	' ',0  ,0  ,0  ,0  ,0  ,0  ,34 ,0  ,0  ,'*','+','<','_','>','?',	// 2
	')','!','@','#','$','%','^','&','*','(',0  ,':',0  ,'+',0  ,0  ,	// 3
	'~','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',	// 4
	'P','Q','R','S','T','U','V','W','X','Y','Z','{','|','}',0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0		// 7
};
byte SpecialNames[] =	// ASCII for 0xe0 prefixed codes
{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 0
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,13 ,0  ,0  ,0  ,	// 1
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 2
	0  ,0  ,0  ,0  ,0  ,'/',0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 4
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0   	// 7
};


static	boolean		IN_Started;

static	Direction	DirTable[] =		// Quick lookup for total direction
{
    dir_NorthWest,	dir_North,	dir_NorthEast,
    dir_West,		dir_None,	dir_East,
    dir_SouthWest,	dir_South,	dir_SouthEast
};


///////////////////////////////////////////////////////////////////////////
//
//	INL_GetMouseButtons() - Gets the status of the mouse buttons from the
//		mouse driver
//
///////////////////////////////////////////////////////////////////////////

static boolean screenvisible;
static boolean window_focused;
static int window_w, window_h;

static boolean MouseShouldBeGrabbed(void)
{
   // if the window doesnt have focus, never grab it

   if (!window_focused)
      return false;

   // if we specify not to grab the mouse, never grab

   if (!grabmouse)
      return false;

   // when menu is active or game is paused, release the mouse

   if (Paused || menuactive)
      return false;

   // only grab mouse when playing levels (but not demos)

   return ingame && !demoplayback;
}

static void SetShowCursor(boolean show)
{
   // When the cursor is hidden, grab the input.
   // Relative mode implicitly hides the cursor.

   Wolf_SetRelativeMouseMode(show ? Wolf_FALSE : Wolf_TRUE);
   Wolf_GetRelativeMouseState(NULL, NULL);
}

void IN_UpdateGrab(void)
{
   static boolean currently_grabbed = false;
   boolean grab;

   grab = MouseShouldBeGrabbed();

   if (grab && !currently_grabbed)
   {
      SetShowCursor(false);
   }

   if (!grab && currently_grabbed)
   {
      int screen_w, screen_h;

      SetShowCursor(true);

      // When releasing the mouse from grab, warp the mouse cursor to
      // the bottom-right of the screen. This is a minimally distracting
      // place for it to appear - we may only have released the grab
      // because we're at an end of level intermission screen, for
      // example.

      Wolf_GetWindowSize(window, &screen_w, &screen_h);
      Wolf_WarpMouseInWindow(window, screen_w - 16, screen_h - 16);
      Wolf_GetRelativeMouseState(NULL, NULL);
   }

   // Native fullscreen/focus transitions can release the capture independently.
   // Reconcile the backend every time; it debounces cursor hide/show itself.
   Wolf_SetRelativeMouseMode(grab ? Wolf_TRUE : Wolf_FALSE);
   currently_grabbed = grab;
}

static unsigned int mouse_button_state = 0;

static void UpdateMouseButtonState(unsigned int button, boolean on)
{
    // Note: button "0" is left, button "1" is right,
    // button "2" is middle for Doom.  This is different
    // to how SDL sees things.

    switch (button)
    {
        case Wolf_BUTTON_LEFT:
            button = 0;
            break;

        case Wolf_BUTTON_RIGHT:
            button = 1;
            break;

        case Wolf_BUTTON_MIDDLE:
            button = 2;
            break;

        default:
            return;
    }

    // Turn bit representing this button on or off.

    if (on)
    {
        mouse_button_state |= (1 << button);
    }
    else
    {
        mouse_button_state &= ~(1 << button);
    }
}

static void I_HandleMouseEvent(Wolf_Event *sdlevent)
{
    switch (sdlevent->type)
    {
        case Wolf_MOUSEBUTTONDOWN:
            UpdateMouseButtonState(sdlevent->button.button, true);
            break;

        case Wolf_MOUSEBUTTONUP:
            UpdateMouseButtonState(sdlevent->button.button, false);
            break;

        default:
            break;
    }
}

static int
INL_GetMouseButtons(void)
{
    return mouse_button_state;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_GetJoyDelta() - Returns the relative movement of the specified
//		joystick (from +/-127)
//
///////////////////////////////////////////////////////////////////////////
void IN_GetJoyDelta(int *dx,int *dy)
{
    if(!Joystick)
    {
        *dx = *dy = 0;
        return;
    }

    Wolf_JoystickUpdate();
    int x = Wolf_JoystickGetAxis(Joystick, 0) >> 8;
    int y = Wolf_JoystickGetAxis(Joystick, 1) >> 8;

    if(param_joystickhat != -1)
    {
        uint8_t hatState = Wolf_JoystickGetHat(Joystick, param_joystickhat);
        if(hatState & Wolf_HAT_RIGHT)
            x += 127;
        else if(hatState & Wolf_HAT_LEFT)
            x -= 127;
        if(hatState & Wolf_HAT_DOWN)
            y += 127;
        else if(hatState & Wolf_HAT_UP)
            y -= 127;

        if(x < -128) x = -128;
        else if(x > 127) x = 127;

        if(y < -128) y = -128;
        else if(y > 127) y = 127;
    }

    *dx = x;
    *dy = y;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_GetJoyFineDelta() - Returns the relative movement of the specified
//		joystick without dividing the results by 256 (from +/-127)
//
///////////////////////////////////////////////////////////////////////////
void IN_GetJoyFineDelta(int *dx, int *dy)
{
    if(!Joystick)
    {
        *dx = 0;
        *dy = 0;
        return;
    }

    Wolf_JoystickUpdate();
    int x = Wolf_JoystickGetAxis(Joystick, 0);
    int y = Wolf_JoystickGetAxis(Joystick, 1);

    if(x < -128) x = -128;
    else if(x > 127) x = 127;

    if(y < -128) y = -128;
    else if(y > 127) y = 127;

    *dx = x;
    *dy = y;
}

/*
===================
=
= IN_JoyButtons
=
===================
*/

int IN_JoyButtons()
{
    if(!Joystick) return 0;

    Wolf_JoystickUpdate();

    int res = 0;
    for(int i = 0; i < JoyNumButtons && i < 32; i++)
        res |= Wolf_JoystickGetButton(Joystick, i) << i;
    return res;
}

boolean IN_JoyPresent()
{
    return Joystick != NULL;
}

static void HandleWindowEvent(Wolf_WindowEvent *event)
{
    switch (event->event)
    {
        case Wolf_WINDOWEVENT_RESIZED:
            if (!fullscreen)
            {
                Wolf_GetWindowSize(window, &window_w, &window_h);
            }
            break;

        // Don't render the screen when the window is minimized:

        case Wolf_WINDOWEVENT_MINIMIZED:
            screenvisible = false;
            break;

        case Wolf_WINDOWEVENT_MAXIMIZED:
        case Wolf_WINDOWEVENT_RESTORED:
            screenvisible = true;
            break;

        // Update the value of window_focused when we get a focus event
        //
        // We try to make ourselves be well-behaved: the grab on the mouse
        // is removed if we lose focus (such as a popup window appearing),
        // and we dont move the mouse around if we aren't focused either.

        case Wolf_WINDOWEVENT_FOCUS_GAINED:
            window_focused = true;
            break;

        case Wolf_WINDOWEVENT_FOCUS_LOST:
            mouse_button_state=0;IN_ClearKeysDown();
            memset(buttonstate,0,sizeof(buttonstate));memset(buttonheld,0,sizeof(buttonheld));
            window_focused = false;
            break;

        default:
            break;
    }

    IN_UpdateGrab();
}

static boolean ToggleFullScreenKeyShortcut(Wolf_Keysym *sym)
{
    Uint16 flags = (KMOD_LALT | KMOD_RALT);
#if defined(__MACOSX__)
    flags |= (KMOD_LGUI | KMOD_RGUI);
#endif
    return (sym->scancode == Wolf_SCANCODE_RETURN ||
            sym->scancode == Wolf_SCANCODE_KP_ENTER) && (sym->mod & flags) != 0;
}

static void I_ToggleFullScreen(void)
{
    unsigned int flags = 0;

    fullscreen = !fullscreen;

    if (fullscreen)
    {
        Wolf_GetWindowSize(window, &window_w, &window_h);
        flags |= Wolf_WINDOW_FULLSCREEN_DESKTOP;
    }

    Wolf_SetWindowFullscreen(window, flags);

    if (!fullscreen)
    {
        if (!window_w || !window_h)
        {
            window_w = screenWidth;
            window_h = screenHeight;
        }
        Wolf_SetWindowSize(window, window_w, window_h);
    }
}

// [FG] map mouse wheel to key presses
static unsigned int mwheelsym = 0;
static inline void mwheelkey (int type)
{
    Wolf_Event event;

    event.type = type;
    event.key.keysym.sym = mwheelsym;
    Wolf_PushEvent(&event);

    mwheelsym = 0;
}

static void processEvent(Wolf_Event *event)
{
    switch (event->type)
    {
        // exit if the window is closed
        case Wolf_QUIT:
            Quit(NULL);

        // [FG] map mouse wheel to key presses
        case Wolf_MOUSEWHEEL:
        {
            if (MousePresent && window_focused)
            {
                if (event->wheel.y > 0)
                {
                    mwheelsym = KEYD_MWHEELUP;
                }
                else
                {
                    mwheelsym = KEYD_MWHEELDOWN;
                }
                // [FG] fake key press event
                event->key.keysym.sym = mwheelsym;
            }
        } // [FG] fall through

        // check for keypresses
        case Wolf_KEYDOWN:
        {
            if (ToggleFullScreenKeyShortcut(&event->key.keysym))
            {
                I_ToggleFullScreen();
                return;
            }

            LastScan = event->key.keysym.sym;
            Wolf_Keymod mod = Wolf_GetModState();
            if(Keyboard[sc_Alt])
            {
                if(LastScan==WK_F4)
                    Quit(NULL);
            }

            if(LastScan == WK_KP_ENTER) LastScan = WK_RETURN;
            else if(LastScan == WK_RSHIFT) LastScan = WK_LSHIFT;
            else if(LastScan == WK_RALT) LastScan = WK_LALT;
            else if(LastScan == WK_RCTRL) LastScan = WK_LCTRL;
            else
            {
                if((mod & KMOD_NUM) == 0)
                {
                    switch(LastScan)
                    {
                        case WK_KP_2: LastScan = WK_DOWN; break;
                        case WK_KP_4: LastScan = WK_LEFT; break;
                        case WK_KP_6: LastScan = WK_RIGHT; break;
                        case WK_KP_8: LastScan = WK_UP; break;
                    }
                }
            }

            unsigned int sym = LastScan;
            if(sym >= 'a' && sym <= 'z')
                sym -= 32;  // convert to uppercase

            if(mod & (KMOD_SHIFT | KMOD_CAPS))
            {
                if(sym < lengthof(ShiftNames) && ShiftNames[sym])
                    LastASCII = ShiftNames[sym];
            }
            else
            {
                if(sym < lengthof(ASCIINames) && ASCIINames[sym])
                    LastASCII = ASCIINames[sym];
            }
            Keyboard[LastScan] = 1;
            if(LastScan == WK_PAUSE)
                Paused = true;
            break;
		}

        case Wolf_KEYUP:
        {
            int key = event->key.keysym.sym;
            if(key == WK_KP_ENTER) key = WK_RETURN;
            else if(key == WK_RSHIFT) key = WK_LSHIFT;
            else if(key == WK_RALT) key = WK_LALT;
            else if(key == WK_RCTRL) key = WK_LCTRL;
            else
            {
                if((Wolf_GetModState() & KMOD_NUM) == 0)
                {
                    switch(key)
                    {
                        case WK_KP_2: key = WK_DOWN; break;
                        case WK_KP_4: key = WK_LEFT; break;
                        case WK_KP_6: key = WK_RIGHT; break;
                        case WK_KP_8: key = WK_UP; break;
                    }
                }
            }

            Keyboard[key] = 0;
            break;
        }

        case Wolf_MOUSEBUTTONDOWN:
        case Wolf_MOUSEBUTTONUP:
            if (MousePresent && window_focused)
            {
                I_HandleMouseEvent(event);
            }
            break;

        case Wolf_WINDOWEVENT:
            if (event->window.windowID == Wolf_GetWindowID(window))
            {
                HandleWindowEvent(&event->window);
            }
            break;
    }
}

void IN_WaitAndProcessEvents()
{
    Wolf_Event event;
    if(!Wolf_WaitEvent(&event)) return;
    do
    {
        processEvent(&event);
    }
    while(Wolf_PollEvent(&event));

    // [FG] fake key release event
    if (mwheelsym)
        mwheelkey(Wolf_KEYUP);
}

void IN_ProcessEvents()
{
    Wolf_Event event;

    while (Wolf_PollEvent(&event))
    {
        processEvent(&event);
    }

    // [FG] fake key release event
    if (mwheelsym)
        mwheelkey(Wolf_KEYUP);
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_Startup() - Starts up the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void
IN_Startup(void)
{
	if (IN_Started)
		return;

    IN_ClearKeysDown();

    if(param_joystickindex >= 0 && param_joystickindex < Wolf_NumJoysticks())
    {
        Joystick = Wolf_JoystickOpen(param_joystickindex);
        if(Joystick)
        {
            JoyNumButtons = Wolf_JoystickNumButtons(Joystick);
            if(JoyNumButtons > 32) JoyNumButtons = 32;      // only up to 32 buttons are supported
            JoyNumHats = Wolf_JoystickNumHats(Joystick);
            if(param_joystickhat < -1 || param_joystickhat >= JoyNumHats)
                Quit("The joystickhat param must be between 0 and %i!", JoyNumHats - 1);
        }
        Wolf_JoystickEventState(Wolf_ENABLE);
    }

    IN_UpdateGrab();

    // I didn't find a way to ask libSDL whether a mouse is present, yet...
    MousePresent = true;

    IN_Started = true;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Shutdown() - Shuts down the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void
IN_Shutdown(void)
{
	if (!IN_Started)
		return;

    if(Joystick)
    {
        Wolf_JoystickClose(Joystick);
    }

	IN_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_ClearKeysDown() - Clears the keyboard array
//
///////////////////////////////////////////////////////////////////////////
void
IN_ClearKeysDown(void)
{
	LastScan = sc_None;
	LastASCII = key_None;
	Keyboard.clear();
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_ReadControl() - Reads the device associated with the specified
//		player and fills in the control info struct
//
///////////////////////////////////////////////////////////////////////////
void
IN_ReadControl(int player,ControlInfo *info)
{
	word		buttons;
	int			dx,dy;
	Motion		mx,my;

	dx = dy = 0;
	mx = my = motion_None;
	buttons = 0;

	IN_ProcessEvents();

    if (Keyboard[KbdDefs.upleft])
        mx = motion_Left,my = motion_Up;
    else if (Keyboard[KbdDefs.upright])
        mx = motion_Right,my = motion_Up;
    else if (Keyboard[KbdDefs.downleft])
        mx = motion_Left,my = motion_Down;
    else if (Keyboard[KbdDefs.downright])
        mx = motion_Right,my = motion_Down;

    if (Keyboard[KbdDefs.up] || Keyboard[WK_w])
        my = motion_Up;
    else if (Keyboard[KbdDefs.down] || Keyboard[WK_s])
        my = motion_Down;

    if (Keyboard[KbdDefs.left] || Keyboard[WK_a])
        mx = motion_Left;
    else if (Keyboard[KbdDefs.right] || Keyboard[WK_d])
        mx = motion_Right;

    if (Keyboard[KbdDefs.button0])
        buttons += 1 << 0;
    if (Keyboard[KbdDefs.button1])
    {
        buttons += 1 << 1;
    }

	dx = mx * 127;
	dy = my * 127;

	info->x = dx;
	info->xaxis = mx;
	info->y = dy;
	info->yaxis = my;
	info->button0 = (buttons & (1 << 0)) != 0;
	info->button1 = (buttons & (1 << 1)) != 0;
	info->button2 = (buttons & (1 << 2)) != 0;
	info->button3 = (buttons & (1 << 3)) != 0;
	info->dir = DirTable[((my + 1) * 3) + (mx + 1)];
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_WaitForKey() - Waits for a scan code, then clears LastScan and
//		returns the scan code
//
///////////////////////////////////////////////////////////////////////////
ScanCode
IN_WaitForKey(void)
{
	ScanCode	result;

	while ((result = LastScan)==0)
		IN_WaitAndProcessEvents();
	LastScan = 0;
	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_WaitForASCII() - Waits for an ASCII char, then clears LastASCII and
//		returns the ASCII value
//
///////////////////////////////////////////////////////////////////////////
char
IN_WaitForASCII(void)
{
	char		result;

	while ((result = LastASCII)==0)
		IN_WaitAndProcessEvents();
	LastASCII = '\0';
	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Ack() - waits for a button or key press.  If a button is down, upon
// calling, it must be released for it to be recognized
//
///////////////////////////////////////////////////////////////////////////

boolean	btnstate[NUMBUTTONS];

void IN_StartAck(void)
{
    Wolf_NativeAcknowledge();
    IN_ProcessEvents();
//
// get initial state of everything
//
	IN_ClearKeysDown();
	memset(btnstate, 0, sizeof(btnstate));

	int buttons = IN_JoyButtons() << 4;

	if(MousePresent)
		buttons |= IN_MouseButtons();

	for(int i = 0; i < NUMBUTTONS; i++, buttons >>= 1)
		if(buttons & 1)
			btnstate[i] = true;
}


boolean IN_CheckAck (void)
{
    IN_ProcessEvents();
//
// see if something has been pressed
//
	if(LastScan)
		return true;

	int buttons = IN_JoyButtons() << 4;

	if(MousePresent)
		buttons |= IN_MouseButtons();

	for(int i = 0; i < NUMBUTTONS; i++, buttons >>= 1)
	{
		if(buttons & 1)
		{
			if(!btnstate[i])
            {
                // Wait until button has been released
                do
                {
                    IN_WaitAndProcessEvents();
                    buttons = IN_JoyButtons() << 4;

                    if(MousePresent)
                        buttons |= IN_MouseButtons();
                }
                while(buttons & (1 << i));

				return true;
            }
		}
		else
			btnstate[i] = false;
	}

	return false;
}


void IN_Ack (void)
{
	IN_StartAck ();

    do
    {
        IN_WaitAndProcessEvents();
    }
	while(!IN_CheckAck ());
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_UserInput() - Waits for the specified delay time (in ticks) or the
//		user pressing a key or a mouse button. If the clear flag is set, it
//		then either clears the key or waits for the user to let the mouse
//		button up.
//
///////////////////////////////////////////////////////////////////////////
boolean IN_UserInput(longword delay)
{
	longword	lasttime;

	lasttime = GetTimeCount();
	IN_StartAck ();
	do
	{
        IN_ProcessEvents();
		if (IN_CheckAck())
			return true;
        Wolf_Delay(5);
	} while (GetTimeCount() - lasttime < delay);
	return(false);
}

//===========================================================================

/*
===================
=
= IN_MouseButtons
=
===================
*/
int IN_MouseButtons (void)
{
	if (MousePresent)
		return INL_GetMouseButtons();
	else
		return 0;
}
