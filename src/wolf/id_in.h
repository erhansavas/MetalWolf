// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
//
//	ID Engine
//	ID_IN.h - Header file for Input Manager
//	v1.0d1
//	By Jason Blochowiak
//

#ifndef	__ID_IN__
#define	__ID_IN__

#ifdef	__DEBUG__
#define	__DEBUG_InputMgr__
#endif

typedef	int		ScanCode;
#define	sc_None			0
#define	sc_Bad			0xff
#define	sc_Return		WK_RETURN
#define	sc_Enter		sc_Return
#define	sc_Escape		WK_ESCAPE
#define	sc_Space		WK_SPACE
#define	sc_BackSpace	WK_BACKSPACE
#define	sc_Tab			WK_TAB
#define	sc_Alt			WK_LALT
#define	sc_Control		WK_LCTRL
#define	sc_CapsLock		WK_CAPSLOCK
#define	sc_LShift		WK_LSHIFT
#define	sc_RShift		WK_RSHIFT
#define	sc_UpArrow		WK_UP
#define	sc_DownArrow	WK_DOWN
#define	sc_LeftArrow	WK_LEFT
#define	sc_RightArrow	WK_RIGHT
#define	sc_Insert		WK_INSERT
#define	sc_Delete		WK_DELETE
#define	sc_Home			WK_HOME
#define	sc_End			WK_END
#define	sc_PgUp			WK_PAGEUP
#define	sc_PgDn			WK_PAGEDOWN
#define	sc_F1			WK_F1
#define	sc_F2			WK_F2
#define	sc_F3			WK_F3
#define	sc_F4			WK_F4
#define	sc_F5			WK_F5
#define	sc_F6			WK_F6
#define	sc_F7			WK_F7
#define	sc_F8			WK_F8
#define	sc_F9			WK_F9
#define	sc_F10			WK_F10
#define	sc_F11			WK_F11
#define	sc_F12			WK_F12

#define sc_ScrollLock		WK_SCROLLOCK
#define sc_PrintScreen		WK_PRINT

#define	sc_1			WK_1
#define	sc_2			WK_2
#define	sc_3			WK_3
#define	sc_4			WK_4
#define	sc_5			WK_5
#define	sc_6			WK_6
#define	sc_7			WK_7
#define	sc_8			WK_8
#define	sc_9			WK_9
#define	sc_0			WK_0

#define	sc_A			WK_a
#define	sc_B			WK_b
#define	sc_C			WK_c
#define	sc_D			WK_d
#define	sc_E			WK_e
#define	sc_F			WK_f
#define	sc_G			WK_g
#define	sc_H			WK_h
#define	sc_I			WK_i
#define	sc_J			WK_j
#define	sc_K			WK_k
#define	sc_L			WK_l
#define	sc_M			WK_m
#define	sc_N			WK_n
#define	sc_O			WK_o
#define	sc_P			WK_p
#define	sc_Q			WK_q
#define	sc_R			WK_r
#define	sc_S			WK_s
#define	sc_T			WK_t
#define	sc_U			WK_u
#define	sc_V			WK_v
#define	sc_W			WK_w
#define	sc_X			WK_x
#define	sc_Y			WK_y
#define	sc_Z			WK_z

#define	key_None		0

// [FG] map mouse wheel to key presses
const int KEYD_MWHEELUP = (0x80 + 0x6b), KEYD_MWHEELDOWN = (0x80 + 0x6c);

typedef	enum		{
						demo_Off,demo_Record,demo_Playback,demo_PlayDone
					} Demo;
typedef	enum		{
						ctrl_Keyboard,
						ctrl_Keyboard1 = ctrl_Keyboard,ctrl_Keyboard2,
						ctrl_Joystick,
						ctrl_Joystick1 = ctrl_Joystick,ctrl_Joystick2,
						ctrl_Mouse
					} ControlType;
typedef	enum		{
						motion_Left = -1,motion_Up = -1,
						motion_None = 0,
						motion_Right = 1,motion_Down = 1
					} Motion;
typedef	enum		{
						dir_North,dir_NorthEast,
						dir_East,dir_SouthEast,
						dir_South,dir_SouthWest,
						dir_West,dir_NorthWest,
						dir_None
					} Direction;
typedef	struct		{
						boolean		button0,button1,button2,button3;
						short		x,y;
						Motion		xaxis,yaxis;
						Direction	dir;
					} CursorInfo;
typedef	CursorInfo	ControlInfo;
typedef	struct		{
						ScanCode	button0,button1,
									upleft,		up,		upright,
									left,				right,
									downleft,	down,	downright;
					} KeyboardDef;
typedef	struct		{
						word		joyMinX,joyMinY,
									threshMinX,threshMinY,
									threshMaxX,threshMaxY,
									joyMaxX,joyMaxY,
									joyMultXL,joyMultYL,
									joyMultXH,joyMultYH;
					} JoystickDef;
// Global variables
extern std::unordered_map<ScanCode, boolean> Keyboard;
extern           boolean    MousePresent;
extern  volatile boolean    Paused;
extern  volatile char       LastASCII;
extern           ScanCode   LastScan;
extern           int        JoyNumButtons;
extern           boolean    grabmouse;


// Function prototypes
#define	IN_KeyDown(code)	(Keyboard[(code)])
#define	IN_ClearKey(code)	{Keyboard[code] = false;\
							if (code == LastScan) LastScan = sc_None;}

// DEBUG - put names in prototypes
extern	void		IN_Startup(void),IN_Shutdown(void);
extern	void		IN_ClearKeysDown(void);
extern	void		IN_ReadControl(int,ControlInfo *);
extern	void		IN_GetJoyAbs(word joy,word *xp,word *yp);
extern	void		IN_SetupJoy(word joy,word minx,word maxx,
								word miny,word maxy);
extern	void		IN_StopDemo(void),IN_FreeDemoBuffer(void),
					IN_Ack(void);
extern	boolean		IN_UserInput(longword delay);
extern	char		IN_WaitForASCII(void);
extern	ScanCode	IN_WaitForKey(void);
extern	word		IN_GetJoyButtonsDB(word joy);
extern	const char *IN_GetScanName(ScanCode);

void    IN_WaitAndProcessEvents();
void    IN_ProcessEvents();

int     IN_MouseButtons (void);

boolean IN_JoyPresent();
void    IN_SetJoyCurrent(int joyIndex);
int     IN_JoyButtons (void);
void    IN_GetJoyDelta(int *dx,int *dy);
void    IN_GetJoyFineDelta(int *dx, int *dy);

void    IN_StartAck(void);
boolean IN_CheckAck (void);
void    IN_UpdateGrab(void);

#endif
