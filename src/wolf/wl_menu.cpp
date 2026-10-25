// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
////////////////////////////////////////////////////////////////////
//
// WL_MENU.C
// by John Romero (C) 1992 Id Software, Inc.
//
////////////////////////////////////////////////////////////////////

#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
#else
    #include <unistd.h>
#endif

#include "wl_def.h"

extern int lastgamemusicoffset;
extern int numEpisodesMissing;

//
// PRIVATE PROTOTYPES
//

#define STARTITEM       newgame

// ENDSTRx constants are defined in foreign.h
char endStrings[9][80] = {
    ENDSTR1,
    ENDSTR2,
    ENDSTR3,
    ENDSTR4,
    ENDSTR5,
    ENDSTR6,
    ENDSTR7,
    ENDSTR8,
    ENDSTR9
};

// Native menu publication (2026-10-10): the list page HandleMenu is running,
// read by the platform layer to draw the shared native panel.
static struct { CP_iteminfo *info; CP_itemtype *items; int which; bool active; } nativeMenu;
int wolfMenuClick = -1;
int wolfMenuHover = -1;
static void (*nativeMenuAdjust) (int which, int direction);
static int nativeLoadSave;   // 0 load, 1 save
// Pages shown in the native panel draw nothing in the original screen and
// skip its cursor animation; a move reaches the panel at once.
static bool nativeOnly;
// A message or yes/no question shown as a native panel notice.
static char nativeNotice[256];
static bool nativeNoticeConfirm;
// Answer highlighted on the native panel: 0 yes, 1 no.
static int nativeConfirmChoice;
static bool nativeConfirmQuit;
static bool NativeOnlyPage (CP_itemtype *items);

CP_itemtype MainMenu[] = {

    // Pause menu priority (2026-10-10): back to play, saving, then the rest;
    // the same order as DOOM's.
    {0, STR_BD, 0},
    {0, "Quick Save", 0},
    {0, STR_SG, CP_SaveGame},
    {1, STR_LG, CP_LoadGame},
    {1, STR_NG, CP_NewGame},
    {1, "Settings", NativeOptions},
    {1, STR_VS, CP_ViewScores},
    {1, STR_QT, 0}
};

#ifndef SPEAR
CP_itemtype NewEmenu[] = {
    {1, "Episode 1\n" "Escape from Wolfenstein", 0},
    {0, "", 0},
    {3, "Episode 2\n" "Operation: Eisenfaust", 0},
    {0, "", 0},
    {3, "Episode 3\n" "Die, Fuhrer, Die!", 0},
    {0, "", 0},
    {3, "Episode 4\n" "A Dark Secret", 0},
    {0, "", 0},
    {3, "Episode 5\n" "Trail of the Madman", 0},
    {0, "", 0},
    {3, "Episode 6\n" "Confrontation", 0}
};
#endif


CP_itemtype NewMenu[] = {
    {1, STR_DADDY, 0},
    {1, STR_HURTME, 0},
    {1, STR_BRINGEM, 0},
    {1, STR_DEATH, 0}
};

CP_itemtype LSMenu[] = {
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0},
    {1, "", 0}
};

CP_itemtype CusMenu[] = {
    {1, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0},
    {1, "", 0}
};

// [FG] extended "Customize" menus
CP_itemtype Cus2Menu[] = {
    {1, "", 0},
    {0, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0},
    {1, "", 0},
    {0, "", 0}
};

// CP_iteminfo struct format: short x, y, amount, curpos, indent;
CP_iteminfo MainItems = { MENU_X, MENU_Y, lengthof(MainMenu), STARTITEM, 24 },
            LSItems   = { LSM_X, LSM_Y, lengthof(LSMenu), 0, 24 },
            CusItems  = { 8, CST_Y + 13 * 2, lengthof(CusMenu), -1, 0},
            Cus2Items = { 8, CST_Y + 13 * 2, lengthof(Cus2Menu), -1, 0}, // [FG] extended "Customize" menus
#ifndef SPEAR
            NewEitems = { NE_X, NE_Y, lengthof(NewEmenu), 0, 88 },
#endif
            NewItems  = { NM_X, NM_Y, lengthof(NewMenu), 2, 24 };

int color_hlite[] = {
    DEACTIVE,
    HIGHLIGHT,
    READHCOLOR,
    0x67
};

int color_norml[] = {
    DEACTIVE,
    TEXTCOLOR,
    READCOLOR,
    0x6b
};

int EpisodeSelect[6] = { 1 };


static int SaveGamesAvail[10];
static int StartGame;
static int SoundStatus = 1;
static int pickquick;
static int nativeQuickSlot=-1;
static char SaveName[13] = "savegam?.";
static int latestWolfSlot() {
    int best=-1,hint=-1; struct timespec newest={0,0};
    char marker[300];snprintf(marker,sizeof(marker),"%s/latest-wolf-%s",configdir,extension);
    FILE *journal=fopen(marker,"rb");if(journal){if(fscanf(journal,"%d",&hint)!=1)hint=-1;fclose(journal);}
    for(int slot=0;slot<10;++slot) {
        char path[300],name[13]; strcpy(name,SaveName);name[7]='0'+slot;
        snprintf(path,sizeof(path),"%s/%s",configdir,name);
        struct stat st; FILE *f=fopen(path,"rb"); if(!f)continue;
        uint32_t header[4]={}; bool valid=fseek(f,32,SEEK_SET)==0 && fread(header,sizeof(header),1,f)==1 && header[0]==0x464c4f57 && header[1]==0x3143414d && header[2]==sizeof(objtype) && header[3]==sizeof(gamestate);fclose(f);
        if(valid && stat(path,&st)==0 && (best<0 || st.st_mtimespec.tv_sec>newest.tv_sec || (st.st_mtimespec.tv_sec==newest.tv_sec && (st.st_mtimespec.tv_nsec>newest.tv_nsec || (st.st_mtimespec.tv_nsec==newest.tv_nsec && slot==hint))))) { best=slot;newest=st.st_mtimespec; }
    }
    return best;
}
static FILE *beginNativeSave(const char *path,char *temporary,size_t capacity) {
    snprintf(temporary,capacity,"%s.tmp.XXXXXX",path);int fd=mkstemp(temporary); if(fd<0)return NULL;
    FILE *f=fdopen(fd,"wb"); if(!f){close(fd);unlink(temporary);}return f;
}
static bool finishNativeSave(FILE *file,const char *temporary,const char *path,bool written,int slot) {
    bool ok=written && !ferror(file); if(fflush(file)!=0 || fsync(fileno(file))!=0)ok=false; if(fclose(file)!=0)ok=false;
    if(ok && rename(temporary,path)!=0)ok=false;
    if(!ok)unlink(temporary);else {
        int directory=open(configdir,O_RDONLY);if(directory>=0){fsync(directory);close(directory);}
        char marker[300],pending[340];snprintf(marker,sizeof(marker),"%s/latest-wolf-%s",configdir,extension);
        FILE *journal=beginNativeSave(marker,pending,sizeof(pending));
        if(journal){bool valid=fprintf(journal,"%d\n",slot)>0;valid=fflush(journal)==0 && valid;valid=fsync(fileno(journal))==0 && valid;valid=fclose(journal)==0 && valid;if(valid){if(rename(pending,marker)!=0)unlink(pending);}else unlink(pending);}
        directory=open(configdir,O_RDONLY);if(directory>=0){fsync(directory);close(directory);}
        nativeQuickSlot=slot;SetupSaveGames();
    }
    return ok;
}
static bool NativeSaveSlot(int slot,const char *description) {
    if(slot<0 || slot>9)return false;
    char path[300],temporary[340],name[13],label[32]={};strcpy(name,SaveName);name[7]='0'+slot;
    snprintf(path,sizeof(path),"%s/%s",configdir,name);snprintf(label,sizeof(label),"%s",description);
    FILE *f=beginNativeSave(path,temporary,sizeof(temporary));if(!f)return false;
    bool ok=fwrite(label,1,32,f)==32 && SaveTheGame(f,0,0);return finishNativeSave(f,temporary,path,ok,slot);
}
void WriteConfig(void);
static char SaveGameNames[10][32];
// Quick Save from the pause menu: the slot quick saved last (or the newest
// save); without one, the first empty slot.
static bool NativeQuickSave() {
    SetupSaveGames();
    int slot=nativeQuickSlot>=0?nativeQuickSlot:latestWolfSlot();
    if(slot<0) {
        slot=0;
        for(int i=0;i<10;++i) if(!SaveGamesAvail[i]) { slot=i; break; }
    }
    return NativeSaveSlot(slot,SaveGamesAvail[slot]?SaveGameNames[slot]:"Quick Save");
}



////////////////////////////////////////////////////////////////////
//
// INPUT MANAGER SCANCODE TABLES
//
////////////////////////////////////////////////////////////////////

#if 0
static const char *ScanNames[] =      // Scan code names with single chars
{
    "?", "?", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+", "?", "?",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "|", "?", "A", "S",
    "D", "F", "G", "H", "J", "K", "L", ";", "\"", "?", "?", "?", "Z", "X", "C", "V",
    "B", "N", "M", ",", ".", "/", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?",
    "?", "?", "?", "?", "?", "?", "?", "?", "\xf", "?", "-", "\x15", "5", "\x11", "+", "?",
    "\x13", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?",
    "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?",
    "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?", "?"
};                              // DEBUG - consolidate these
static ScanCode ExtScanCodes[] =        // Scan codes with >1 char names
{
    1, 0xe, 0xf, 0x1d, 0x2a, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
    0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x59, 0x46, 0x1c, 0x36,
    0x37, 0x38, 0x47, 0x49, 0x4f, 0x51, 0x52, 0x53, 0x45, 0x48,
    0x50, 0x4b, 0x4d, 0x00
};
static const char *ExtScanNames[] =   // Names corresponding to ExtScanCodes
{
    "Esc", "BkSp", "Tab", "Ctrl", "LShft", "Space", "CapsLk", "F1", "F2", "F3", "F4",
    "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "ScrlLk", "Enter", "RShft",
    "PrtSc", "Alt", "Home", "PgUp", "End", "PgDn", "Ins", "Del", "NumLk", "Up",
    "Down", "Left", "Right", ""
};

/*#pragma warning 737 9
static byte
                                        *ScanNames[] =          // Scan code names with single chars
                                        {
        "?","?","1","2","3","4","5","6","7","8","9","0","-","+","?","?",
        "Q","W","E","R","T","Y","U","I","O","P","[","]","|","?","A","S",
        "D","F","G","H","J","K","L",";","\"","?","?","?","Z","X","C","V",
        "B","N","M",",",".","/","?","?","?","?","?","?","?","?","?","?",
        "?","?","?","?","?","?","?","?","\xf","?","-","\x15","5","\x11","+","?",
        "\x13","?","?","?","?","?","?","?","?","?","?","?","?","?","?","?",
        "?","?","?","?","?","?","?","?","?","?","?","?","?","?","?","?",
        "?","?","?","?","?","?","?","?","?","?","?","?","?","?","?","?"
                                        };      // DEBUG - consolidate these
static byte ExtScanCodes[] =    // Scan codes with >1 char names
                                        {
        1,0xe,0xf,0x1d,0x2a,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,
        0x3f,0x40,0x41,0x42,0x43,0x44,0x57,0x59,0x46,0x1c,0x36,
        0x37,0x38,0x47,0x49,0x4f,0x51,0x52,0x53,0x45,0x48,
        0x50,0x4b,0x4d,0x00
                                        };
static byte *ExtScanNames[] =   // Names corresponding to ExtScanCodes
                                        {
        "Esc","BkSp","Tab","Ctrl","LShft","Space","CapsLk","F1","F2","F3","F4",
        "F5","F6","F7","F8","F9","F10","F11","F12","ScrlLk","Enter","RShft",
        "PrtSc","Alt","Home","PgUp","End","PgDn","Ins","Del","NumLk","Up",
        "Down","Left","Right",""
                                        };*/

#else
static std::unordered_map<ScanCode, const char *> ScanNames;
void US_SetScanNames()
{
    ScanNames[0] = "None";
    ScanNames[sc_Enter] = "Enter";
    ScanNames[WK_UP] = "Up";
    ScanNames[WK_DOWN] = "Down";
    ScanNames[WK_RIGHT] = "Right";
    ScanNames[WK_LEFT] = "Left";
    ScanNames[WK_INSERT] = "Ins";
    ScanNames[WK_HOME] = "Home";
    ScanNames[WK_END] = "End";
    ScanNames[WK_PAGEUP] = "PgUp";
    ScanNames[WK_PAGEDOWN] = "PgDn";
    ScanNames[WK_F1] = "F1";
    ScanNames[WK_F2] = "F2";
    ScanNames[WK_F3] = "F3";
    ScanNames[WK_F4] = "F4";
    ScanNames[WK_F5] = "F5";
    ScanNames[WK_F6] = "F6";
    ScanNames[WK_F7] = "F7";
    ScanNames[WK_F8] = "F8";
    ScanNames[WK_F9] = "F9";
    ScanNames[WK_F10] = "F10";
    ScanNames[WK_F11] = "F11";
    ScanNames[WK_F12] = "F12";
    ScanNames[WK_F13] = "F13";
    ScanNames[WK_F14] = "F14";
    ScanNames[WK_F15] = "F15";
    ScanNames[WK_F16] = "F16";
    ScanNames[WK_F17] = "F17";
    ScanNames[WK_F18] = "F18";
    ScanNames[WK_F19] = "F19";
    ScanNames[WK_NUMLOCKCLEAR] = "NumLk";
    ScanNames[WK_CAPSLOCK] = "CapsLk";
    ScanNames[WK_SCROLLLOCK] = "ScrlLk";
    ScanNames[WK_RSHIFT] = "RShft";
    ScanNames[WK_LSHIFT] = "Shift";
    ScanNames[WK_RCTRL] = "RCtrl";
    ScanNames[WK_LCTRL] = "LCtrl";
    ScanNames[WK_RALT] = "RAlt";
    ScanNames[WK_LALT] = "Alt";
    ScanNames[WK_RGUI] = "RMeta";
    ScanNames[WK_LGUI] = "LMeta";
    ScanNames[WK_PRINTSCREEN] = "PrtSc";
    ScanNames[sc_BackSpace] = "BkSp";
    ScanNames[sc_Tab] = "Tab";
    ScanNames[sc_Return] = "Return";
    ScanNames[WK_PAUSE] = "Pause";
    ScanNames[sc_Escape] = "Esc";
    ScanNames[sc_Space] = "Space";
    ScanNames[WK_EXCLAIM] = "!";
    ScanNames[WK_QUOTEDBL] = "\"";
    ScanNames[WK_HASH] = "#";
    ScanNames[WK_DOLLAR] = "$";
    ScanNames[WK_QUESTION] = "?";
    ScanNames[WK_AMPERSAND] = "&";
    ScanNames[WK_QUOTE] = "'";
    ScanNames[WK_LEFTPAREN] = "(";
    ScanNames[WK_RIGHTPAREN] = ")";
    ScanNames[WK_ASTERISK] = "*";
    ScanNames[WK_PLUS] = "+";
    ScanNames[WK_COMMA] = ",";
    ScanNames[WK_MINUS] = "-";
    ScanNames[WK_PERIOD] = ".";
    ScanNames[WK_SLASH] = "/";
    ScanNames[WK_0] = "0";
    ScanNames[WK_1] = "1";
    ScanNames[WK_2] = "2";
    ScanNames[WK_3] = "3";
    ScanNames[WK_4] = "4";
    ScanNames[WK_5] = "5";
    ScanNames[WK_6] = "6";
    ScanNames[WK_7] = "7";
    ScanNames[WK_8] = "8";
    ScanNames[WK_9] = "9";
    ScanNames[WK_COLON] = ":";
    ScanNames[WK_SEMICOLON] = ";";
    ScanNames[WK_LESS] = "<";
    ScanNames[WK_EQUALS] = "=";
    ScanNames[WK_GREATER] = ">";
    ScanNames[WK_AT] = "@";
    ScanNames[WK_a] = "A";
    ScanNames[WK_b] = "B";
    ScanNames[WK_c] = "C";
    ScanNames[WK_d] = "D";
    ScanNames[WK_e] = "E";
    ScanNames[WK_f] = "F";
    ScanNames[WK_g] = "G";
    ScanNames[WK_h] = "H";
    ScanNames[WK_i] = "I";
    ScanNames[WK_j] = "J";
    ScanNames[WK_k] = "K";
    ScanNames[WK_l] = "L";
    ScanNames[WK_m] = "M";
    ScanNames[WK_n] = "N";
    ScanNames[WK_o] = "O";
    ScanNames[WK_p] = "P";
    ScanNames[WK_q] = "Q";
    ScanNames[WK_r] = "R";
    ScanNames[WK_s] = "S";
    ScanNames[WK_t] = "T";
    ScanNames[WK_u] = "U";
    ScanNames[WK_v] = "V";
    ScanNames[WK_w] = "W";
    ScanNames[WK_x] = "X";
    ScanNames[WK_y] = "Y";
    ScanNames[WK_z] = "Z";
    ScanNames[WK_LEFTBRACKET] = "[";
    ScanNames[WK_BACKSLASH] = "\\";
    ScanNames[WK_RIGHTBRACKET] = "]";
    ScanNames[KEYD_MWHEELUP] = "MWUP";
    ScanNames[KEYD_MWHEELDOWN] = "MWDN";
}

#endif

bool menuactive; // [FG] true if menu is active

////////////////////////////////////////////////////////////////////
//
// Wolfenstein Control Panel!  Ta Da!
//
////////////////////////////////////////////////////////////////////
void
US_ControlPanel (ScanCode scancode)
{
    int which;
    struct Freeze { bool was = wolfMenuFrozen; Freeze () { wolfMenuFrozen = true; } ~Freeze () { wolfMenuFrozen = was; } } freeze;

    if (ingame)
    {
        if (CP_CheckQuick (scancode))
            return;
        lastgamemusicoffset = StartCPMusic (MENUSONG);
    }
    else
        StartCPMusic (MENUSONG);
    SetupControlPanel ();

    //
    // F-KEYS FROM WITHIN GAME
    //
    switch (scancode)
    {
        case sc_F1:
            BossKey ();
            goto finishup;

        case sc_F2:
            CP_SaveGame (0);
            goto finishup;

        case sc_F3:
            CP_LoadGame (0);
            goto finishup;

        case sc_F4:
            NativeAudio (0);
            goto finishup;

        case sc_F5:
            NativeOptions (0);
            goto finishup;

        case sc_F6:
            NativeControls (0);
            goto finishup;

        finishup:
            CleanupControlPanel ();
#ifdef SPEAR
            UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif
            return;
    }

#ifdef SPEAR
    CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif

    // In a game the menu is the pause menu and opens on Resume.
    if (ingame)
        MainItems.curpos = backtodemo;
    DrawMainMenu ();
    MenuFadeIn ();
    StartGame = 0;

    //
    // MAIN MENU LOOP
    //
    do
    {
        which = HandleMenu (&MainItems, &MainMenu[0], NULL);

#ifdef SPEAR
#ifndef SPEARDEMO
        IN_ProcessEvents();

        //
        // EASTER EGG FOR SPEAR OF DESTINY!
        //
        if (Keyboard[sc_I] && Keyboard[sc_D])
        {
            VW_FadeOut ();
            StartCPMusic (XJAZNAZI_MUS);
            UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
            UnCacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
            ClearMemory ();


            CA_CacheGrChunk (IDGUYS1PIC);
            VWB_DrawPic (0, 0, IDGUYS1PIC);
            UNCACHEGRCHUNK (IDGUYS1PIC);

            CA_CacheGrChunk (IDGUYS2PIC);
            VWB_DrawPic (0, 80, IDGUYS2PIC);
            UNCACHEGRCHUNK (IDGUYS2PIC);

            VW_UpdateScreen ();

            Wolf_Color pal[256];
            CA_CacheGrChunk (IDGUYSPALETTE);
            VL_ConvertPalette(grsegs[IDGUYSPALETTE], pal, 256);
            VL_FadeIn (0, 255, pal, 30);
            UNCACHEGRCHUNK (IDGUYSPALETTE);

            while (Keyboard[sc_I] || Keyboard[sc_D])
                IN_WaitAndProcessEvents();
            IN_ClearKeysDown ();
            IN_Ack ();

            VW_FadeOut ();

            CacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
            CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
            DrawMainMenu ();
            StartCPMusic (MENUSONG);
            MenuFadeIn ();
        }
#endif
#endif

        switch (which)
        {
            case viewscores:
                if (MainMenu[viewscores].routine == NULL)
                {
                    if (CP_EndGame (0))
                        StartGame = 1;
                }
                else
                {
                    DrawMainMenu();
                    MenuFadeIn ();
                }
                break;

            case backtodemo:
                Wolf_NativeMenuCue (WOLF_CUE_START);
                // fall through
            case -1:            // Esc closes the menu, as in DOOM
                StartGame = 1;
                if (!ingame)
                    StartCPMusic (INTROSONG);
                VL_FadeOut (0, 255, 0, 0, 0, 10);
                break;

            case quicksave:
                if (NativeQuickSave ())
                {
                    StartGame = 1;
                    VL_FadeOut (0, 255, 0, 0, 0, 10);
                }
                else
                {
                    Message ("Unable to save the game.");
                    IN_ClearKeysDown ();
                    IN_Ack ();
                    DrawMainMenu ();
                    MenuFadeIn ();
                }
                break;

            case quit:
                CP_Quit (0);
                break;

            default:
                if (!StartGame)
                {
                    DrawMainMenu ();
                    MenuFadeIn ();
                }
        }

        //
        // "EXIT OPTIONS" OR "NEW GAME" EXITS
        //
    }
    while (!StartGame);

    //
    // DEALLOCATE EVERYTHING
    //
    CleanupControlPanel ();

    //
    // CHANGE MAINMENU ITEM
    //
    if (startgame || loadedgame)
        EnableEndGameMenuItem();

    // RETURN/START GAME EXECUTION

#ifdef SPEAR
    UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif
}

void EnableEndGameMenuItem()
{
    MainMenu[viewscores].routine = NULL;
    strcpy (MainMenu[viewscores].string, STR_EG);
}

////////////////////////
//
// DRAW MAIN MENU SCREEN
//
void
DrawMainMenu (void)
{
    ClearMScreen ();

    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);
    DrawStripes (10);
    VWB_DrawPic (84, 0, C_OPTIONSPIC);

    DrawWindow (MENU_X - 8, MENU_Y - 3, MENU_W, MENU_H, BKGDCOLOR);

    //
    // Native panel (2026-10-10): in a game the first row is Resume, as in
    // DOOM; outside one Esc closes the menu. Unavailable rows are hidden.
    //
    MainMenu[backtodemo].active = (ingame && !demoplayback) ? 1 : 0;
    MainMenu[quicksave].active = MainMenu[savegame].active && (ingame && !demoplayback && !demorecord) ? 1 : 0;
    if (!MainMenu[MainItems.curpos].active)
        MainItems.curpos = newgame;

    DrawMenu (&MainItems, &MainMenu[0]);
    VW_UpdateScreen ();
}


////////////////////////////////////////////////////////////////////
//
// BOSS KEY
//
////////////////////////////////////////////////////////////////////
void
BossKey (void)
{
    const char *prompt[] = {"C:\\>", "C:\\>_"};
    int i = 0;

    int lastoffs = StopMusic();
    int32_t lastBlinkTime = GetTimeCount();

    // [FG] instant fade-in to cleared screen
    VWB_Bar(0, 0, 320, 200, 0);
    VL_FadeIn(0, 255, gamepal, 1);

    fontnumber = 0;
    SETFONTCOLOR(15, 0);

    do
    {
        // [FG] clear screen
        VWB_Bar (0, 0, 320, 200, 0);

        // [FG] print DOS prompt line with blinking cursor
        if ((int32_t)GetTimeCount() - lastBlinkTime > 20)
        {
            lastBlinkTime = GetTimeCount();
            i ^= 1;
        }
        PrintX = PrintY = 0;
        US_Print(prompt[i]);

        VW_UpdateScreen();
        TicDelay(20);
    } while (!(IN_KeyDown(sc_Escape) || IN_KeyDown(sc_Control) || IN_KeyDown(sc_Alt)));

    IN_ClearKeysDown ();

    ContinueMusic(lastoffs);
}


////////////////////////////////////////////////////////////////////
//
// CHECK QUICK-KEYS & QUIT (WHILE IN A GAME)
//
////////////////////////////////////////////////////////////////////
int
CP_CheckQuick (ScanCode scancode)
{
    switch (scancode)
    {
        //
        // END GAME
        //
        case sc_F7:
            CA_CacheGrChunk (STARTFONT + 1);

            WindowH = 160;
            if (Confirm (ENDGAMESTR))
            {
                playstate = ex_died;
                killerobj = NULL;
                pickquick = gamestate.lives = 0;
            }

            WindowH = 200;
            fontnumber = 0;
            MainMenu[savegame].active = 0;
            return 1;

        //
        // QUICKSAVE
        //
        case sc_F8:
            SetupSaveGames();
            if(nativeQuickSlot<0)nativeQuickSlot=latestWolfSlot();
            if(nativeQuickSlot>=0){LSItems.curpos=nativeQuickSlot;pickquick=1;}
            if (SaveGamesAvail[LSItems.curpos] && pickquick)
            {
                CA_CacheGrChunk (STARTFONT + 1);
                fontnumber = 1;
                Message (STR_SAVING "...");
                CP_SaveGame (1);
                fontnumber = 0;
            }
            else
            {
#ifndef SPEAR
                CA_CacheGrChunk (STARTFONT + 1);
                CA_CacheGrChunk (C_CURSOR1PIC);
                CA_CacheGrChunk (C_CURSOR2PIC);
                CA_CacheGrChunk (C_DISKLOADING1PIC);
                CA_CacheGrChunk (C_DISKLOADING2PIC);
                CA_CacheGrChunk (C_SAVEGAMEPIC);
                CA_CacheGrChunk (C_MOUSELBACKPIC);
#else
                CacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
                CA_CacheGrChunk (C_CURSOR1PIC);
#endif

                VW_FadeOut ();
                if(screenHeight % 200 != 0)
                    VL_ClearScreen(0);

                lastgamemusicoffset = StartCPMusic (MENUSONG);
                pickquick = CP_SaveGame (0);

                SETFONTCOLOR (0, 15);
                IN_ClearKeysDown ();
                VW_FadeOut();
                if(viewsize != 21)
                    DrawPlayScreen ();

                if (!startgame && !loadedgame)
                    ContinueMusic (lastgamemusicoffset);

                if (loadedgame)
                    playstate = ex_abort;
                lasttimecount = GetTimeCount ();

#ifndef SPEAR
                UNCACHEGRCHUNK (C_CURSOR1PIC);
                UNCACHEGRCHUNK (C_CURSOR2PIC);
                UNCACHEGRCHUNK (C_DISKLOADING1PIC);
                UNCACHEGRCHUNK (C_DISKLOADING2PIC);
                UNCACHEGRCHUNK (C_SAVEGAMEPIC);
                UNCACHEGRCHUNK (C_MOUSELBACKPIC);
#else
                UnCacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
#endif
            }
            return 1;

        //
        // QUICKLOAD
        //
        case sc_F9:
            SetupSaveGames();
            if(nativeQuickSlot<0)nativeQuickSlot=latestWolfSlot();
            if(nativeQuickSlot>=0){LSItems.curpos=nativeQuickSlot;pickquick=1;}
            if (SaveGamesAvail[LSItems.curpos] && pickquick)
            {
                char string[100] = STR_LGC;


                CA_CacheGrChunk (STARTFONT + 1);
                fontnumber = 1;

                strcat (string, SaveGameNames[LSItems.curpos]);
                strcat (string, "\"?");

                if (Confirm (string))
                    CP_LoadGame (1);

                fontnumber = 0;
            }
            else
            {
#ifndef SPEAR
                CA_CacheGrChunk (STARTFONT + 1);
                CA_CacheGrChunk (C_CURSOR1PIC);
                CA_CacheGrChunk (C_CURSOR2PIC);
                CA_CacheGrChunk (C_DISKLOADING1PIC);
                CA_CacheGrChunk (C_DISKLOADING2PIC);
                CA_CacheGrChunk (C_LOADGAMEPIC);
                CA_CacheGrChunk (C_MOUSELBACKPIC);
#else
                CA_CacheGrChunk (C_CURSOR1PIC);
                CacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
#endif

                VW_FadeOut ();
                if(screenHeight % 200 != 0)
                    VL_ClearScreen(0);

                lastgamemusicoffset = StartCPMusic (MENUSONG);
                pickquick = CP_LoadGame (0);    // loads lastgamemusicoffs

                SETFONTCOLOR (0, 15);
                IN_ClearKeysDown ();
                VW_FadeOut();
                if(viewsize != 21)
                    DrawPlayScreen ();

                if (!startgame && !loadedgame)
                    ContinueMusic (lastgamemusicoffset);

                if (loadedgame)
                    playstate = ex_abort;

                lasttimecount = GetTimeCount ();

#ifndef SPEAR
                UNCACHEGRCHUNK (C_CURSOR1PIC);
                UNCACHEGRCHUNK (C_CURSOR2PIC);
                UNCACHEGRCHUNK (C_DISKLOADING1PIC);
                UNCACHEGRCHUNK (C_DISKLOADING2PIC);
                UNCACHEGRCHUNK (C_LOADGAMEPIC);
                UNCACHEGRCHUNK (C_MOUSELBACKPIC);
#else
                UnCacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
#endif
            }
            return 1;

        //
        // QUIT
        //
        case sc_F10:
            CA_CacheGrChunk (STARTFONT + 1);

            WindowX = WindowY = 0;
            WindowW = 320;
            WindowH = 160;
            nativeConfirmQuit = true;
            int answer = Confirm (endStrings[US_RndT () & (0x7 + (US_RndT () & 1))]);
            nativeConfirmQuit = false;
            if (answer)
            {
                WriteConfig ();
                VW_UpdateScreen ();
                SD_MusicOff ();
                SD_StopSound ();
                MenuFadeOut ();

                Quit (NULL);
            }

            DrawPlayBorder ();
            WindowH = 200;
            fontnumber = 0;
            return 1;
    }

    return 0;
}


////////////////////////////////////////////////////////////////////
//
// END THE CURRENT GAME
//
////////////////////////////////////////////////////////////////////
int
CP_EndGame (int)
{
    int res;
    res = Confirm (ENDGAMESTR);
    DrawMainMenu();
    if(!res) return 0;

    pickquick = gamestate.lives = 0;
    playstate = ex_died;
    killerobj = NULL;

    MainMenu[savegame].active = 0;
    MainMenu[viewscores].routine = CP_ViewScores;
    strcpy (MainMenu[viewscores].string, STR_VS);

    return 1;
}


////////////////////////////////////////////////////////////////////
//
// VIEW THE HIGH SCORES
//
////////////////////////////////////////////////////////////////////
int
CP_ViewScores (int)
{
    // The roster is a game screen, shown as drawn.
    struct Show { Show () { wolfMenuFrozen = false; } ~Show () { wolfMenuFrozen = true; } } show;
    fontnumber = 0;

#ifdef SPEAR
    UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
    StartCPMusic (XAWARD_MUS);
#else
    StartCPMusic (ROSTER_MUS);
#endif

    DrawHighScores ();
    VW_UpdateScreen ();
    MenuFadeIn ();
    fontnumber = 1;

    IN_Ack ();

    StartCPMusic (MENUSONG);
    MenuFadeOut ();

#ifdef SPEAR
    CacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
    CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif
    return 0;
}


////////////////////////////////////////////////////////////////////
//
// START A NEW GAME
//
////////////////////////////////////////////////////////////////////
int
CP_NewGame (int)
{
    int which, episode;

#ifdef SPEAR
    UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif


#ifndef SPEAR
  firstpart:

    DrawNewEpisode ();
    do
    {
        which = HandleMenu (&NewEitems, &NewEmenu[0], NULL);
        switch (which)
        {
            case -1:
                MenuFadeOut ();
                return 0;

            default:
                if (!EpisodeSelect[which / 2])
                {
                    SD_PlaySound (NOWAYSND);
                    Message ("This episode is part of the\n"
                             "full game, not the shareware.");
                    IN_ClearKeysDown ();
                    IN_Ack ();
                    DrawNewEpisode ();
                    which = 0;
                }
                else
                {
                    episode = which / 2;
                    which = 1;
                }
                break;
        }

    }
    while (!which);

    ShootSnd ();

    //
    // ALREADY IN A GAME?
    //
    if (ingame)
        if (!Confirm (CURGAME))
        {
            MenuFadeOut ();
            return 0;
        }

    MenuFadeOut ();

#else
    episode = 0;

    //
    // ALREADY IN A GAME?
    //
    CacheLump (NEWGAME_LUMP_START, NEWGAME_LUMP_END);
    DrawNewGame ();
    if (ingame)
        if (!Confirm (CURGAME))
        {
            MenuFadeOut ();
            UnCacheLump (NEWGAME_LUMP_START, NEWGAME_LUMP_END);
            CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
            return 0;
        }

#endif

    DrawNewGame ();
    which = HandleMenu (&NewItems, &NewMenu[0], DrawNewGameDiff);
    if (which < 0)
    {
        MenuFadeOut ();
#ifndef SPEAR
        goto firstpart;
#else
        UnCacheLump (NEWGAME_LUMP_START, NEWGAME_LUMP_END);
        CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
        return 0;
#endif
    }

    Wolf_NativeMenuCue (WOLF_CUE_START);
    NewGame (which, episode);
    StartGame = 1;
    MenuFadeOut ();

    pickquick = 0;

#ifdef SPEAR
    UnCacheLump (NEWGAME_LUMP_START, NEWGAME_LUMP_END);
    CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif

    return 0;
}


#ifndef SPEAR
/////////////////////
//
// DRAW NEW EPISODE MENU
//
void
DrawNewEpisode (void)
{
    int i;

    ClearMScreen ();
    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);

    DrawWindow (NE_X - 4, NE_Y - 4, NE_W + 8, NE_H + 8, BKGDCOLOR);
    SETFONTCOLOR (READHCOLOR, BKGDCOLOR);
    PrintY = 2;
    WindowX = 0;
    US_CPrint ("Which episode to play?");

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    DrawMenu (&NewEitems, &NewEmenu[0]);

    for (i = 0; i < 6; i++)
        VWB_DrawPic (NE_X + 32, NE_Y + i * 26, C_EPISODE1PIC + i);

    VW_UpdateScreen ();
    MenuFadeIn ();
    WaitKeyUp ();
}
#endif

/////////////////////
//
// DRAW NEW GAME MENU
//
void
DrawNewGame (void)
{
    ClearMScreen ();
    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);

    SETFONTCOLOR (READHCOLOR, BKGDCOLOR);
    PrintX = NM_X + 20;
    PrintY = NM_Y - 32;

#ifndef SPEAR
    US_Print ("How tough are you?");
#else
    VWB_DrawPic (PrintX, PrintY, C_HOWTOUGHPIC);
#endif

    DrawWindow (NM_X - 5, NM_Y - 10, NM_W, NM_H, BKGDCOLOR);

    DrawMenu (&NewItems, &NewMenu[0]);
    DrawNewGameDiff (NewItems.curpos);
    VW_UpdateScreen ();
    MenuFadeIn ();
    WaitKeyUp ();
}


////////////////////////
//
// DRAW NEW GAME GRAPHIC
//
void
DrawNewGameDiff (int w)
{
    VWB_DrawPic (NM_X + 185, NM_Y + 7, w + C_BABYMODEPIC);
}


//
// DRAW LOAD/SAVE IN PROGRESS
//
void
DrawLSAction (int which)
{
#define LSA_X   96
#define LSA_Y   80
#define LSA_W   130
#define LSA_H   42

    DrawWindow (LSA_X, LSA_Y, LSA_W, LSA_H, TEXTCOLOR);
    DrawOutline (LSA_X, LSA_Y, LSA_W, LSA_H, 0, HIGHLIGHT);
    VWB_DrawPic (LSA_X + 8, LSA_Y + 5, C_DISKLOADING1PIC);

    fontnumber = 1;
    SETFONTCOLOR (0, TEXTCOLOR);
    PrintX = LSA_X + 46;
    PrintY = LSA_Y + 13;

    if (!which)
        US_Print (STR_LOADING "...");
    else
        US_Print (STR_SAVING "...");

    VW_UpdateScreen ();
}


////////////////////////////////////////////////////////////////////
//
// LOAD SAVED GAMES
//
////////////////////////////////////////////////////////////////////
int
CP_LoadGame (int quick)
{
    FILE *file;
    int which, exit = 0;
    char name[13];
    char loadpath[300];

    strcpy (name, SaveName);

    //
    // QUICKLOAD?
    //
    if (quick)
    {
        which = LSItems.curpos;

        if (SaveGamesAvail[which])
        {
            name[7] = which + '0';

            if(configdir[0])
                snprintf(loadpath, sizeof(loadpath), "%s/%s", configdir, name);
            else
                strcpy(loadpath, name);

            file = fopen (loadpath, "rb");
            loadedgame = true;
            bool restored=file && fseek(file,32,SEEK_SET)==0 && LoadTheGame(file,0,0);
            loadedgame = false;
            if(file)fclose(file);
            if(!restored) {
                Message("Unable to load this save.\nIt is missing or damaged.");
                IN_ClearKeysDown();IN_Ack();return 0;
            }

            DrawFace ();
            DrawHealth ();
            DrawLives ();
            DrawLevel ();
            DrawAmmo ();
            DrawKeys ();
            DrawWeapon ();
            DrawScore ();
            ContinueMusic (lastgamemusicoffset);
            return 1;
        }
    }


#ifdef SPEAR
    UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
    CacheLump (LOADSAVE_LUMP_START, LOADSAVE_LUMP_END);
#endif

    DrawLoadSaveScreen (0);

    do
    {
        which = HandleMenu (&LSItems, &LSMenu[0], TrackWhichGame);
        if (which >= 0 && SaveGamesAvail[which])
        {
            ShootSnd ();
            name[7] = which + '0';

            if(configdir[0])
                snprintf(loadpath, sizeof(loadpath), "%s/%s", configdir, name);
            else
                strcpy(loadpath, name);

            file = fopen (loadpath, "rb");

            DrawLSAction (0);
            loadedgame = true;

            bool restored=file && fseek(file,32,SEEK_SET)==0 && LoadTheGame(file,LSA_X+8,LSA_Y+5);
            if(file)fclose(file);
            if(!restored) {
                loadedgame=false;
                Message("Unable to load this save.\nIt is missing or damaged.");
                IN_ClearKeysDown();IN_Ack();DrawLoadSaveScreen(0);continue;
            }

            StartGame = 1;
            Wolf_NativeMenuCue (WOLF_CUE_START);

            exit = 1;
            break;
        }

    }
    while (which >= 0);

    MenuFadeOut ();

#ifdef SPEAR
    UnCacheLump (LOADSAVE_LUMP_START, LOADSAVE_LUMP_END);
    CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif

    return exit;
}


///////////////////////////////////
//
// HIGHLIGHT CURRENT SELECTED ENTRY
//
void
TrackWhichGame (int w)
{
    static int lastgameon = 0;

    PrintLSEntry (lastgameon, TEXTCOLOR);
    PrintLSEntry (w, HIGHLIGHT);

    lastgameon = w;
}


////////////////////////////
//
// DRAW THE LOAD/SAVE SCREEN
//
void
DrawLoadSaveScreen (int loadsave)
{
    nativeLoadSave = loadsave;
#define DISKX   100
#define DISKY   0

    int i;


    ClearMScreen ();
    fontnumber = 1;
    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);
    DrawWindow (LSM_X - 10, LSM_Y - 5, LSM_W, LSM_H, BKGDCOLOR);
    DrawStripes (10);

    if (!loadsave)
        VWB_DrawPic (60, 0, C_LOADGAMEPIC);
    else
        VWB_DrawPic (60, 0, C_SAVEGAMEPIC);

    for (i = 0; i < 10; i++)
        PrintLSEntry (i, TEXTCOLOR);

    DrawMenu (&LSItems, &LSMenu[0]);
    VW_UpdateScreen ();
    MenuFadeIn ();
    WaitKeyUp ();
}


///////////////////////////////////////////
//
// PRINT LOAD/SAVE GAME ENTRY W/BOX OUTLINE
//
void
PrintLSEntry (int w, int color)
{
    SETFONTCOLOR (color, BKGDCOLOR);
    DrawOutline (LSM_X + LSItems.indent, LSM_Y + w * 13, LSM_W - LSItems.indent - 15, 11, color,
                 color);
    PrintX = LSM_X + LSItems.indent + 2;
    PrintY = LSM_Y + w * 13 + 1;
    fontnumber = 0;

    char label[100];
    bool latest=w==latestWolfSlot();
    snprintf(label,sizeof(label),"%d. %.20s%s",w+1,SaveGamesAvail[w]?SaveGameNames[w]:"- EMPTY -",latest?" (Latest)":"");
    word width,height;USL_MeasureString(label,&width,&height);
    for(int limit=19;width>LSM_W-LSItems.indent-18 && limit>=0;--limit) {
        snprintf(label,sizeof(label),"%d. %.*s%s",w+1,limit,SaveGamesAvail[w]?SaveGameNames[w]:"- EMPTY -",latest?" (Latest)":"");USL_MeasureString(label,&width,&height);
    }
    US_Print(label);

    fontnumber = 1;
}


////////////////////////////////////////////////////////////////////
//
// SAVE CURRENT GAME
//
////////////////////////////////////////////////////////////////////
int
CP_SaveGame (int quick)
{
    int which, exit = 0;
    FILE *file;
    char name[13];
    char savepath[300];
    char input[32]={};
    char temporary[340];

    strcpy (name, SaveName);

    //
    // QUICKSAVE?
    //
    if (quick)
    {
        which = LSItems.curpos;

        if (SaveGamesAvail[which])
        {
            name[7] = which + '0';

            if(configdir[0])
                snprintf(savepath, sizeof(savepath), "%s/%s", configdir, name);
            else
                strcpy(savepath, name);

            file = beginNativeSave(savepath,temporary,sizeof(temporary));
            if(!file) { Message("SAVE FAILED");return 0; }

            strcpy (input, &SaveGameNames[which][0]);

            fwrite (input, 1, 32, file);
            fseek (file, 32, SEEK_SET);
            bool written=SaveTheGame (file, 0, 0);
            if(!finishNativeSave(file,temporary,savepath,written,which)){Message("SAVE FAILED");return 0;}
            return 1;
        }
    }


#ifdef SPEAR
    UnCacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
    CacheLump (LOADSAVE_LUMP_START, LOADSAVE_LUMP_END);
#endif

    DrawLoadSaveScreen (1);

    do
    {
        which = HandleMenu (&LSItems, &LSMenu[0], TrackWhichGame);
        if (which >= 0)
        {
            //
            // OVERWRITE EXISTING SAVEGAME?
            //
            if (SaveGamesAvail[which])
            {
                if (!Confirm (GAMESVD))
                {
                    DrawLoadSaveScreen (1);
                    continue;
                }
                else
                {
                    DrawLoadSaveScreen (1);
                    PrintLSEntry (which, HIGHLIGHT);
                    VW_UpdateScreen ();
                }
            }

            ShootSnd ();

            strcpy (input, &SaveGameNames[which][0]);
            name[7] = which + '0';

            fontnumber = 0;
            if (!SaveGamesAvail[which])
                VWB_Bar (LSM_X + LSItems.indent + 1, LSM_Y + which * 13 + 1,
                         LSM_W - LSItems.indent - 16, 10, BKGDCOLOR);
            VW_UpdateScreen ();

            // Saves are named automatically: episode, floor and time.
            {
                time_t now = time (NULL);
                char clock[8];
                strftime (clock, sizeof(clock), "%H:%M", localtime (&now));
                snprintf (input, sizeof(input), "E%d F%d  %s", gamestate.episode + 1, gamestate.mapon + 1, clock);
            }
            if (true)
            {


                if(configdir[0])
                    snprintf(savepath, sizeof(savepath), "%s/%s", configdir, name);
                else
                    strcpy(savepath, name);

                file = beginNativeSave(savepath,temporary,sizeof(temporary));
                if(!file) { Message("SAVE FAILED");continue; }
                fwrite (input, 32, 1, file);
                fseek (file, 32, SEEK_SET);

                DrawLSAction (1);
                bool written=SaveTheGame (file, LSA_X + 8, LSA_Y + 5);
                if(!finishNativeSave(file,temporary,savepath,written,which)) {Message("SAVE FAILED");continue;}

                ShootSnd ();
                exit = 1;
            }
            else
            {
                VWB_Bar (LSM_X + LSItems.indent + 1, LSM_Y + which * 13 + 1,
                         LSM_W - LSItems.indent - 16, 10, BKGDCOLOR);
                PrintLSEntry (which, HIGHLIGHT);
                VW_UpdateScreen ();
                SD_PlaySound (ESCPRESSEDSND);
                continue;
            }

            fontnumber = 1;
            break;
        }

    }
    while (which >= 0);

    MenuFadeOut ();

#ifdef SPEAR
    UnCacheLump (LOADSAVE_LUMP_START, LOADSAVE_LUMP_END);
    CacheLump (OPTIONS_LUMP_START, OPTIONS_LUMP_END);
#endif

    return exit;
}

// Native settings (2026-10-10): one list of categories, the same as DOOM's,
// each a short page drawn by the shared native panel over a plain backdrop.
// The original Sound and Control screens (DOS sound cards, mouse and joystick
// switches) mean nothing on this Mac and are gone.
static int NativeVideo (int);
static int NativePlay (int);
static CP_itemtype OptMenu[] = {
    {1, "Audio", NativeAudio},
    {1, "Video", NativeVideo},
    {1, "Gameplay", NativePlay},
    {1, "Controls", NativeControls},
    {1, "Controller", NativeController}
};
static CP_iteminfo OptItems = { CTL_X, 70, lengthof(OptMenu), 0, 56 };

enum { VIDEO_FULLSCREEN, VIDEO_STATUSBAR };
static CP_itemtype VideoMenu[] = { {1, "Fullscreen", 0}, {1, "Status Bar", 0} };
static CP_iteminfo VideoItems = { CTL_X, 70, lengthof(VideoMenu), 0, 56 };

enum { AUDIO_EFFECTS, AUDIO_MUSIC };
static CP_itemtype AudioMenu[] = { {1, "Sound Effects", 0}, {1, "Music", 0} };
static CP_iteminfo AudioItems = { CTL_X, 70, lengthof(AudioMenu), 0, 56 };

enum { PLAY_ALWAYSRUN, PLAY_CROSSHAIR };
static CP_itemtype PlayMenu[] = { {1, "Always Run", 0}, {1, "Crosshair", 0} };
static CP_iteminfo PlayItems = { CTL_X, 70, lengthof(PlayMenu), 0, 56 };

enum { CONTROLS_SENS, CONTROLS_SENSV, CONTROLS_KEYS, CONTROLS_MOUSE };
static CP_itemtype ControlsMenu[] = { {1, "Mouse Sensitivity", 0}, {1, "Vertical Sensitivity", 0}, {1, "Customize Keyboard…", NativeKeys}, {1, "Customize Mouse…", NativeMouse} };
static CP_iteminfo ControlsItems = { CTL_X, 70, lengthof(ControlsMenu), 0, 56 };

enum { PAD_HAPTICS, PAD_HAPTICSTRENGTH, PAD_TRIGGERS, PAD_TRIGGERSTRENGTH, PAD_DEADZONE, PAD_TURN };
static CP_itemtype PadMenu[] = {
    {1, "Haptics", 0},
    {1, "Haptic Strength", 0},
    {1, "Adaptive Triggers", 0},
    {1, "Trigger Resistance", 0},
    {1, "Stick Deadzone", 0},
    {1, "Turn Sensitivity", 0}
};
static CP_iteminfo PadItems = { CTL_X, 40, lengthof(PadMenu), 0, 56 };

static void DrawNativeBackdrop (void)
{
    ClearMScreen ();
    DrawStripes (10);
    VW_UpdateScreen ();
}

// Keyboard and mouse customization as native lists (2026-10-10).
static const struct { const char *name; int *scan; } keyRows[] = {
    {"Fire", &buttonscan[bt_attack]}, {"Strafe", &buttonscan[bt_strafe]}, {"Run", &buttonscan[bt_run]}, {"Open / Use", &buttonscan[bt_use]},
    {"Knife", &buttonscan[bt_readyknife]}, {"Pistol", &buttonscan[bt_readypistol]}, {"Machine Gun", &buttonscan[bt_readymachinegun]}, {"Chain Gun", &buttonscan[bt_readychaingun]},
    {"Next Weapon", &buttonscan[bt_nextweapon]}, {"Previous Weapon", &buttonscan[bt_prevweapon]}, {"Strafe Left", &buttonscan[bt_strafeleft]}, {"Strafe Right", &buttonscan[bt_straferight]},
    {"Forward", &dirscan[di_north]}, {"Backward", &dirscan[di_south]}, {"Turn Left", &dirscan[di_west]}, {"Turn Right", &dirscan[di_east]}
};
#define KEY_ROW {1, "-", 0}
static CP_itemtype KeyMenu[] = { KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW, KEY_ROW };
#undef KEY_ROW
static CP_iteminfo KeyItems = { CTL_X, 20, lengthof(KeyMenu), 0, 56 };

static const struct { const char *name; int button; } mouseActions[] = {
    {"None", bt_nobutton}, {"Fire", bt_attack}, {"Strafe", bt_strafe}, {"Run", bt_run}, {"Open / Use", bt_use},
    {"Next Weapon", bt_nextweapon}, {"Previous Weapon", bt_prevweapon}
};
static const char *mouseButtonNames[] = { "Left Button", "Right Button", "Middle Button", "Button 4" };
static CP_itemtype MouseMenu[] = { {1, "-", 0}, {1, "-", 0}, {1, "-", 0}, {1, "-", 0} };
static CP_iteminfo MouseItems = { CTL_X, 70, lengthof(MouseMenu), 0, 56 };

int
NativeKeys (int)
{
    int which;
    DrawNativeBackdrop ();
    WaitKeyUp ();
    do
    {
        which = HandleMenu (&KeyItems, KeyMenu, NULL);
        if (which < 0)
            break;
        // Wait for the new key; Escape keeps the current one.
        snprintf (nativeNotice, sizeof(nativeNotice), "Press a key for %s\nEsc keeps the current key", keyRows[which].name);
        IN_ClearKeysDown ();
        WaitKeyUp ();
        LastScan = sc_None;
        while (!LastScan)
        {
            Wolf_Delay (5);
            IN_ProcessEvents ();
        }
        if (LastScan != sc_Escape)
        {
            *keyRows[which].scan = LastScan;
            ShootSnd ();
        }
        else
            SD_PlaySound (ESCPRESSEDSND);
        nativeNotice[0] = 0;
        IN_ClearKeysDown ();
        WaitKeyUp ();
    }
    while (1);
    return 0;
}

static void CycleMouseButton (int row, int direction)
{
    int count = lengthof(mouseActions), at = 0;
    for (int i = 0; i < count; i++)
        if (mouseActions[i].button == buttonmouse[row])
            at = i;
    buttonmouse[row] = mouseActions[(at + direction + count) % count].button;
    SD_PlaySound (MOVEGUN1SND);
}

int
NativeMouse (int)
{
    int which;
    DrawNativeBackdrop ();
    WaitKeyUp ();
    do
    {
        nativeMenuAdjust = CycleMouseButton;
        which = HandleMenu (&MouseItems, MouseMenu, NULL);
        nativeMenuAdjust = NULL;
        if (which >= 0)
            CycleMouseButton (which, 1);
    }
    while (which >= 0);
    return 0;
}


static void AdjustPadRow (int row, int direction)
{
    static const int setting[] = { WOLF_PAD_HAPTICS, WOLF_PAD_HAPTIC_STRENGTH, WOLF_PAD_TRIGGERS, WOLF_PAD_TRIGGER_STRENGTH, WOLF_PAD_DEADZONE, WOLF_PAD_TURN };
    Wolf_NativePadAdjust (setting[row], direction);
    SD_PlaySound (MOVEGUN1SND);
}

int
NativeController (int)
{
    int which;
    DrawNativeBackdrop ();
    MenuFadeIn ();
    WaitKeyUp ();
    do
    {
        nativeMenuAdjust = AdjustPadRow;
        which = HandleMenu (&PadItems, PadMenu, NULL);
        if (which >= 0)
            AdjustPadRow (which, 1);
    }
    while (which >= 0);
    nativeMenuAdjust = NULL;
    MenuFadeOut ();
    return 0;
}

// One settings page: Enter takes a row (opening its page, if it has one),
// left/right adjusts a value row.
static void NativeSettingsPage (CP_iteminfo *info, CP_itemtype *items, void (*choose) (int), void (*adjust) (int, int))
{
    int which;
    DrawNativeBackdrop ();
    MenuFadeIn ();
    WaitKeyUp ();
    do
    {
        nativeMenuAdjust = adjust;
        which = HandleMenu (info, items, NULL);
        nativeMenuAdjust = NULL;
        if (which >= 0 && items[which].routine)
        {
            DrawNativeBackdrop ();
            MenuFadeIn ();
        }
        else if (which >= 0 && choose)
            choose (which);
    }
    while (which >= 0);
    MenuFadeOut ();
}

int
NativeOptions (int)
{
    NativeSettingsPage (&OptItems, OptMenu, NULL, NULL);
    return 0;
}

static int
NativeVideo (int)
{
    auto choose = [] (int row) {
        switch (row)
        {
            case VIDEO_FULLSCREEN: Wolf_NativeToggleFullscreen (); break;
            case VIDEO_STATUSBAR: Wolf_NativeSetStatusBar (!Wolf_NativeStatusBar ()); break;
            default: return;
        }
        ShootSnd ();
    };
    NativeSettingsPage (&VideoItems, VideoMenu, choose, NULL);
    return 0;
}

static bool EffectsOn (void)
{
    return SoundMode != sdm_Off || DigiMode != sds_Off;
}

int
NativeAudio (int)
{
    // Effects are the AdLib sounds with the digitized ones over them, as the
    // original plays them on a Sound Blaster; music is the AdLib score.
    auto choose = [] (int row) {
        if (row == AUDIO_EFFECTS)
        {
            bool on = !EffectsOn ();
            SD_WaitSoundDone ();
            SD_SetSoundMode (on ? (AdLibPresent ? sdm_AdLib : sdm_PC) : sdm_Off);
            SD_SetDigiDevice (on && SoundBlasterPresent ? sds_SoundBlaster : sds_Off);
            if (on)
                CA_LoadAllSounds ();
        }
        else
        {
            SD_SetMusicMode (MusicMode == smm_Off ? smm_AdLib : smm_Off);
            if (MusicMode != smm_Off)
                StartCPMusic (MENUSONG);
        }
        ShootSnd ();
    };
    NativeSettingsPage (&AudioItems, AudioMenu, choose, NULL);
    return 0;
}

static int
NativePlay (int)
{
    auto choose = [] (int row) {
        switch (row)
        {
            case PLAY_ALWAYSRUN: always_run ^= 1; break;
            case PLAY_CROSSHAIR: crosshair ^= 1; break;
        }
        ShootSnd ();
    };
    NativeSettingsPage (&PlayItems, PlayMenu, choose, NULL);
    return 0;
}

int
NativeControls (int)
{
    // Mouse sensitivity rows: left/right, 1–10.
    auto adjust = [] (int row, int direction) {
        int *value = row == CONTROLS_SENS ? &mouseadjustment : row == CONTROLS_SENSV ? &mouseadjustment_v : NULL;
        if (!value)
            return;
        int next = *value + direction;
        if (next < 0 || next > 9)
            return;
        *value = next;
        SD_PlaySound (MOVEGUN1SND);
    };
    NativeSettingsPage (&ControlsItems, ControlsMenu, NULL, adjust);
    return 0;
}

////////////////////////////////////////////////////////////////////
//
// CUSTOMIZE CONTROLS
//
////////////////////////////////////////////////////////////////////
enum
{ FIRE, STRAFE, RUN, OPEN };
// [FG] support up to 32 joystick buttons
char mbarray[32][4] = { "b00", "b01", "b02", "b03", "b04", "b05", "b06", "b07", "b08", "b09",
                        "b10", "b11", "b12", "b13", "b14", "b15", "b16", "b17", "b18", "b19",
                        "b20", "b21", "b22", "b23", "b24", "b25", "b26", "b27", "b28", "b29",
                        "b30", "b31" };
// [FG] support up to 32 joystick buttons
int8_t order[32] = { RUN, OPEN, FIRE, STRAFE, 4, 5, 6, 7, 8, 9,
                    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                    31, 31 };


// [FG] extended "Customize" menus
int
Custom2Controls (int)
{
    int which;

    DrawCustom2Screen ();
    do
    {
        which = HandleMenu (&Cus2Items, &Cus2Menu[0], FixupCustom2);
        switch (which)
        {
            case 0:
                DefineMouseBtns ();
                DrawCustMouse (1);
                break;
            case 3:
                DefineJoyBtns ();
                DrawCustJoy (0);
                break;
            // [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
            case 5:
                DefineJoy2Btns ();
                DrawCust2Joy (0);
                break;
            case 7:
                DefineJoy3Btns ();
                DrawCust3Joy (0);
                break;
        }
    }
    while (which >= 0);

    MenuFadeOut ();

    return 0;
}

int
CustomControls (int)
{
    int which;

    DrawCustomScreen ();
    do
    {
        which = HandleMenu (&CusItems, &CusMenu[0], FixupCustom);
        switch (which)
        {
            case 0:
                DefineKeyBtns ();
                DrawCustKeybd (0);
                break;
            case 2:
                DefineKeyMove ();
                DrawCustKeys (0);
                break;
            // [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
            case 4:
                DefineKey2Btns ();
                DrawCust2Keybd (0);
                break;
            // [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
            case 6:
                DefineKey3Btns ();
                DrawCust3Keybd (0);
                break;
            // [FG] keyboard buttons 4 (StrLft, StrRgt)
            case 8:
                DefineKey4Btns ();
                DrawCust4Keybd (0);
                break;
        }
    }
    while (which >= 0);

    MenuFadeOut ();

    return 0;
}


////////////////////////
//
// DEFINE THE MOUSE BUTTONS
//
void
DefineMouseBtns (void)
{
    CustomCtrls mouseallowed = { 0, 1, 1, 1 };
    EnterCtrlData (2, &mouseallowed, DrawCustMouse, PrintCustMouse, MOUSE);
}


////////////////////////
//
// DEFINE THE JOYSTICK BUTTONS
//
void
DefineJoyBtns (void)
{
    CustomCtrls joyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (5, &joyallowed, DrawCustJoy, PrintCustJoy, JOYSTICK);
}

// [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
void
DefineJoy2Btns (void)
{
    CustomCtrls joyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (7, &joyallowed, DrawCust2Joy, PrintCust2Joy, JOYSTICK2);
}

// [FG] joystick buttons 3 (StrLft, StrRgt)
void
DefineJoy3Btns (void)
{
    CustomCtrls joyallowed = { 1, 1, 0, 0 };
    EnterCtrlData (9, &joyallowed, DrawCust3Joy, PrintCust3Joy, JOYSTICK3);
}


////////////////////////
//
// DEFINE THE KEYBOARD BUTTONS
//
void
DefineKeyBtns (void)
{
    CustomCtrls keyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (2, &keyallowed, DrawCustKeybd, PrintCustKeybd, KEYBOARDBTNS);
}

// [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
void
DefineKey2Btns (void)
{
    CustomCtrls keyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (6, &keyallowed, DrawCust2Keybd, PrintCust2Keybd, KEYBOARD2BTNS);
}

// [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
void
DefineKey3Btns (void)
{
    CustomCtrls keyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (8, &keyallowed, DrawCust3Keybd, PrintCust3Keybd, KEYBOARD3BTNS);
}

// [FG] keyboard buttons 4 (StrLft, StrRgt)
void
DefineKey4Btns (void)
{
    CustomCtrls keyallowed = { 1, 1, 0, 0 };
    EnterCtrlData (10, &keyallowed, DrawCust4Keybd, PrintCust4Keybd, KEYBOARD4BTNS);
}


////////////////////////
//
// DEFINE THE KEYBOARD BUTTONS
//
void
DefineKeyMove (void)
{
    CustomCtrls keyallowed = { 1, 1, 1, 1 };
    EnterCtrlData (4, &keyallowed, DrawCustKeys, PrintCustKeys, KEYBOARDMOVE);
}


////////////////////////
//
// ENTER CONTROL DATA FOR ANY TYPE OF CONTROL
//
enum
{ FWRD, RIGHT, BKWD, LEFT };
int moveorder[4] = { LEFT, RIGHT, FWRD, BKWD };

void
EnterCtrlData (int index, CustomCtrls * cust, void (*DrawRtn) (int), void (*PrintRtn) (int),
               int type)
{
    int j, exit, tick, redraw, which = 0, x, picked, lastFlashTime;
    ControlInfo ci;


    ShootSnd ();
    PrintY = CST_Y + 13 * index;
    IN_ClearKeysDown ();
    exit = 0;
    redraw = 1;
    //
    // FIND FIRST SPOT IN ALLOWED ARRAY
    //
    for (j = 0; j < 4; j++)
        if (cust->allowed[j])
        {
            which = j;
            break;
        }

    do
    {
        if (redraw)
        {
            x = CST_START + CST_SPC * which;
            DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);

            DrawRtn (1);
            DrawWindow (x - 2, PrintY, CST_SPC, 11, TEXTCOLOR);
            DrawOutline (x - 2, PrintY, CST_SPC, 11, 0, HIGHLIGHT);
            SETFONTCOLOR (0, TEXTCOLOR);
            PrintRtn (which);
            PrintX = x;
            SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
            VW_UpdateScreen ();
            WaitKeyUp ();
            redraw = 0;
        }

        Wolf_Delay(5);
        ReadAnyControl (&ci);

        // [FG] extended "Customize" menus
        if (type == MOUSE || type == JOYSTICK || type == JOYSTICK2 || type == JOYSTICK3)
            if (IN_KeyDown (sc_Enter) || IN_KeyDown (sc_Control) || IN_KeyDown (sc_Alt))
            {
                IN_ClearKeysDown ();
                ci.button0 = ci.button1 = false;
            }

        //
        // CHANGE BUTTON VALUE?
        //
        // [FG] extended "Customize" menus, support up to 32 joystick buttons
        if (((type != KEYBOARD4BTNS && type != KEYBOARD3BTNS && type != KEYBOARD2BTNS &&
              type != KEYBOARDBTNS && type != KEYBOARDMOVE) && (ci.button0 | ci.button1 | ci.button2 | ci.button3 | IN_JoyButtons())) ||
            ((type == KEYBOARD4BTNS || type == KEYBOARD3BTNS || type == KEYBOARD2BTNS ||
              type == KEYBOARDBTNS || type == KEYBOARDMOVE) && LastScan == sc_Enter))
        {
            lastFlashTime = GetTimeCount();
            tick = picked = 0;
            SETFONTCOLOR (0, TEXTCOLOR);

            // [FG] extended "Customize" menus
            if (type == KEYBOARD4BTNS || type == KEYBOARD3BTNS || type == KEYBOARD2BTNS ||
                type == KEYBOARDBTNS || type == KEYBOARDMOVE)
                IN_ClearKeysDown ();

            while(1)
            {
                int button, result = 0;

                //
                // FLASH CURSOR
                //
                if (GetTimeCount() - lastFlashTime > 10)
                {
                    switch (tick)
                    {
                        case 0:
                            VWB_Bar (x, PrintY + 1, CST_SPC - 2, 10, TEXTCOLOR);
                            break;
                        case 1:
                            PrintX = x;
                            US_Print ("?");
                            SD_PlaySound (HITWALLSND);
                    }
                    tick ^= 1;
                    lastFlashTime = GetTimeCount();
                    VW_UpdateScreen ();
                }
                else Wolf_Delay(5);

                //
                // WHICH TYPE OF INPUT DO WE PROCESS?
                //
                switch (type)
                {
                    case MOUSE:
                        button = IN_MouseButtons();
                        switch (button)
                        {
                            case 1:
                                result = 1;
                                break;
                            case 2:
                                result = 2;
                                break;
                            case 4:
                                result = 3;
                                break;
                        }

                        if (result)
                        {
                            for (int z = 0; z < 4; z++)
                                if (order[which] == buttonmouse[z])
                                {
                                    buttonmouse[z] = bt_nobutton;
                                    break;
                                }

                            buttonmouse[result - 1] = order[which];
                            picked = 1;
                            SD_PlaySound (SHOOTDOORSND);
                        }
                        break;

                    case JOYSTICK:
                        button = IN_JoyButtons();
                        // [FG] support up to 32 joystick buttons
                        for (int i = 0; i < 32; i++)
                        {
                          if (button & (1 << i))
                          {
                            result = i + 1;
                            break;
                          }
                        }

                        if (result)
                        {
                            // [FG] support up to 32 joystick buttons
                            for (int z = 0; z < 32; z++)
                            {
                                if (order[which] == buttonjoy[z])
                                {
                                    buttonjoy[z] = bt_nobutton;
                                    break;
                                }
                            }

                            buttonjoy[result - 1] = order[which];
                            picked = 1;
                            SD_PlaySound (SHOOTDOORSND);

                        }
                        break;

                    // [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
                    case JOYSTICK2:
                        button = IN_JoyButtons();
                        // [FG] support up to 32 joystick buttons
                        for (int i = 0; i < 32; i++)
                        {
                          if (button & (1 << i))
                          {
                            result = i + 1;
                            break;
                          }
                        }

                        if (result)
                        {
                            // [FG] support up to 32 joystick buttons
                            for (int z = 0; z < 32; z++)
                            {
                                if (which+8 == buttonjoy[z])
                                {
                                    buttonjoy[z] = bt_nobutton;
                                    break;
                                }
                            }

                            buttonjoy[result - 1] = which+8;
                            picked = 1;
                            SD_PlaySound (SHOOTDOORSND);
                        }
                        break;

                    // [FG] joystick buttons 3 (StrLft, StrRgt)
                    case JOYSTICK3:
                        button = IN_JoyButtons();
                        // [FG] support up to 32 joystick buttons
                        for (int i = 0; i < 32; i++)
                        {
                          if (button & (1 << i))
                          {
                            result = i + 1;
                            break;
                          }
                        }

                        if (result)
                        {
                            // [FG] support up to 32 joystick buttons
                            for (int z = 0; z < 32; z++)
                            {
                                if (which+12 == buttonjoy[z])
                                {
                                    buttonjoy[z] = bt_nobutton;
                                    break;
                                }
                            }

                            buttonjoy[result - 1] = which+12;
                            picked = 1;
                            SD_PlaySound (SHOOTDOORSND);
                        }
                        break;

                    case KEYBOARDBTNS:
                        if (LastScan && LastScan != sc_Escape)
                        {
                            buttonscan[order[which]] = LastScan;
                            picked = 1;
                            ShootSnd ();
                            IN_ClearKeysDown ();
                        }
                        break;

                    // [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
                    case KEYBOARD2BTNS:
                        if (LastScan && LastScan != sc_Escape)
                        {
                            buttonscan[which+4] = LastScan;
                            picked = 1;
                            ShootSnd ();
                            IN_ClearKeysDown ();
                        }
                        break;

                    // [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
                    case KEYBOARD3BTNS:
                        if (LastScan && LastScan != sc_Escape)
                        {
                            buttonscan[which+8] = LastScan;
                            picked = 1;
                            ShootSnd ();
                            IN_ClearKeysDown ();
                        }
                        break;

                    // [FG] keyboard buttons 4 (StrLft, StrRgt)
                    case KEYBOARD4BTNS:
                        if (LastScan && LastScan != sc_Escape)
                        {
                            buttonscan[which+12] = LastScan;
                            picked = 1;
                            ShootSnd ();
                            IN_ClearKeysDown ();
                        }
                        break;

                    case KEYBOARDMOVE:
                        if (LastScan && LastScan != sc_Escape)
                        {
                            dirscan[moveorder[which]] = LastScan;
                            picked = 1;
                            ShootSnd ();
                            IN_ClearKeysDown ();
                        }
                        break;
                }

                //
                // EXIT INPUT?
                //
                if (IN_KeyDown (sc_Escape) || (type != JOYSTICK && ci.button1))
                {
                    picked = 1;
                    SD_PlaySound (ESCPRESSEDSND);
                }

                if(picked) break;

                ReadAnyControl (&ci);
            }

            SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
            redraw = 1;
            WaitKeyUp ();
            continue;
        }

        if (ci.button1 || IN_KeyDown (sc_Escape))
            exit = 1;

        //
        // MOVE TO ANOTHER SPOT?
        //
        switch (ci.dir)
        {
            case dir_West:
                do
                {
                    which--;
                    if (which < 0)
                        which = 3;
                }
                while (!cust->allowed[which]);
                redraw = 1;
                SD_PlaySound (MOVEGUN1SND);
                while (ReadAnyControl (&ci), ci.dir != dir_None) Wolf_Delay(5);
                IN_ClearKeysDown ();
                break;

            case dir_East:
                do
                {
                    which++;
                    if (which > 3)
                        which = 0;
                }
                while (!cust->allowed[which]);
                redraw = 1;
                SD_PlaySound (MOVEGUN1SND);
                while (ReadAnyControl (&ci), ci.dir != dir_None) Wolf_Delay(5);
                IN_ClearKeysDown ();
                break;
            case dir_North:
            case dir_South:
                exit = 1;
            default:
                break;
        }
    }
    while (!exit);

    SD_PlaySound (ESCPRESSEDSND);
    WaitKeyUp ();
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
}


////////////////////////
//
// FIXUP GUN CURSOR OVERDRAW SHIT
//
// [FG] extended "Customize" menus
void
FixupCustom2 (int w)
{
    static int lastwhich = -1;
    int y = CST_Y + 26 + w * 13;


    VWB_Hlin (7, 32, y - 1, DEACTIVE);
    VWB_Hlin (7, 32, y + 12, BORD2COLOR);
#ifndef SPEAR
    VWB_Hlin (7, 32, y - 2, BORDCOLOR);
    VWB_Hlin (7, 32, y + 13, BORDCOLOR);
#else
    VWB_Hlin (7, 32, y - 2, BORD2COLOR);
    VWB_Hlin (7, 32, y + 13, BORD2COLOR);
#endif

    switch (w)
    {
        case 0:
            DrawCustMouse (1);
            break;
        case 3:
            DrawCustJoy (1);
            break;
        // [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
        case 5:
            DrawCust2Joy (1);
            break;
        // [FG] joystick buttons 3 (StrLft, StrRgt)
        case 7:
            DrawCust3Joy (1);
            break;
    }


    if (lastwhich >= 0)
    {
        y = CST_Y + 26 + lastwhich * 13;
        VWB_Hlin (7, 32, y - 1, DEACTIVE);
        VWB_Hlin (7, 32, y + 12, BORD2COLOR);
#ifndef SPEAR
        VWB_Hlin (7, 32, y - 2, BORDCOLOR);
        VWB_Hlin (7, 32, y + 13, BORDCOLOR);
#else
        VWB_Hlin (7, 32, y - 2, BORD2COLOR);
        VWB_Hlin (7, 32, y + 13, BORD2COLOR);
#endif

        if (lastwhich != w)
            switch (lastwhich)
            {
                case 0:
                    DrawCustMouse (0);
                    break;
                case 3:
                    DrawCustJoy (0);
                    break;
                // [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
                case 5:
                    DrawCust2Joy (0);
                    break;
                // [FG] joystick buttons 3 (StrLft, StrRgt)
                case 7:
                    DrawCust3Joy (0);
                    break;
            }
    }

    lastwhich = w;
}

void
FixupCustom (int w)
{
    static int lastwhich = -1;
    int y = CST_Y + 26 + w * 13;


    VWB_Hlin (7, 32, y - 1, DEACTIVE);
    VWB_Hlin (7, 32, y + 12, BORD2COLOR);
#ifndef SPEAR
    VWB_Hlin (7, 32, y - 2, BORDCOLOR);
    VWB_Hlin (7, 32, y + 13, BORDCOLOR);
#else
    VWB_Hlin (7, 32, y - 2, BORD2COLOR);
    VWB_Hlin (7, 32, y + 13, BORD2COLOR);
#endif

    switch (w)
    {
        case 0:
            DrawCustKeybd (1);
            break;
        case 2:
            DrawCustKeys (1);
            break;
        // [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
        case 4:
            DrawCust2Keybd (1);
            break;
        // [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
        case 6:
            DrawCust3Keybd (1);
            break;
        // [FG] keyboard buttons 4 (StrLft, StrRgt)
        case 8:
            DrawCust4Keybd (1);
            break;
    }


    if (lastwhich >= 0)
    {
        y = CST_Y + 26 + lastwhich * 13;
        VWB_Hlin (7, 32, y - 1, DEACTIVE);
        VWB_Hlin (7, 32, y + 12, BORD2COLOR);
#ifndef SPEAR
        VWB_Hlin (7, 32, y - 2, BORDCOLOR);
        VWB_Hlin (7, 32, y + 13, BORDCOLOR);
#else
        VWB_Hlin (7, 32, y - 2, BORD2COLOR);
        VWB_Hlin (7, 32, y + 13, BORD2COLOR);
#endif

        if (lastwhich != w)
            switch (lastwhich)
            {
        case 0:
            DrawCustKeybd (0);
            break;
        case 2:
            DrawCustKeys (0);
            break;
        // [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
        case 4:
            DrawCust2Keybd (0);
            break;
        // [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
        case 6:
            DrawCust3Keybd (0);
            break;
        // [FG] keyboard buttons 4 (StrLft, StrRgt)
        case 8:
            DrawCust4Keybd (0);
            break;
            }
    }

    lastwhich = w;
}


////////////////////////
//
// DRAW CUSTOMIZE SCREEN
//
// [FG] extended "Customize" menus
void
DrawCustom2Screen (void)
{
    int i;


    ClearMScreen ();
    WindowX = 0;
    WindowW = 320;
    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);
    DrawStripes (10);
    VWB_DrawPic (80, 0, C_CUSTOMIZEPIC);
    PrintY = CST_Y;

    //
    // MOUSE
    //
    SETFONTCOLOR (READCOLOR, BKGDCOLOR);
    WindowX = 0;
    WindowW = 320;

#ifndef SPEAR
    PrintY = CST_Y;
    US_CPrint ("Mouse\n");
#else
    PrintY = CST_Y + 13;
    VWB_DrawPic (128, 48, C_MOUSEPIC);
#endif

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print (STR_CRUN);
    PrintX = CST_START + CST_SPC * 1;
    US_Print (STR_COPEN);
    PrintX = CST_START + CST_SPC * 2;
    US_Print (STR_CFIRE);
    PrintX = CST_START + CST_SPC * 3;
    US_Print (STR_CSTRAFE "\n");

    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCustMouse (0);
    US_Print ("\n");


    //
    // JOYSTICK/PAD
    //
#ifndef SPEAR
    SETFONTCOLOR (READCOLOR, BKGDCOLOR);
    US_CPrint ("Joystick/Gravis GamePad\n");
#else
    PrintY += 13;
    VWB_DrawPic (40, 88, C_JOYSTICKPIC);
#endif

#ifdef SPEAR
    VWB_DrawPic (112, 120, C_KEYBOARDPIC);
#endif

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print (STR_CRUN);
    PrintX = CST_START + CST_SPC * 1;
    US_Print (STR_COPEN);
    PrintX = CST_START + CST_SPC * 2;
    US_Print (STR_CFIRE);
    PrintX = CST_START + CST_SPC * 3;
    US_Print (STR_CSTRAFE "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCustJoy (0);
    US_Print ("\n");

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print ("Wp+");
    PrintX = CST_START + CST_SPC * 1;
    US_Print ("Wp-");
    PrintX = CST_START + CST_SPC * 2;
    US_Print ("Menu");
    PrintX = CST_START + CST_SPC * 3;
    US_Print ("Pause" "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCust2Joy (0);
    US_Print ("\n");

    PrintX = CST_START;
    US_Print ("StrLft");
    PrintX = CST_START + CST_SPC * 1;
    US_Print ("StrRgt" "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCust3Joy (0);
    US_Print ("\n");

    //
    // PICK STARTING POINT IN MENU
    //
    if (Cus2Items.curpos < 0)
        for (i = 0; i < Cus2Items.amount; i++)
            if (Cus2Menu[i].active)
            {
                Cus2Items.curpos = i;
                break;
            }


    VW_UpdateScreen ();
    MenuFadeIn ();
}

void
DrawCustomScreen (void)
{
    int i;


    ClearMScreen ();
    WindowX = 0;
    WindowW = 320;
    VWB_DrawPic (112, 184, C_MOUSELBACKPIC);
    DrawStripes (10);
    VWB_DrawPic (80, 0, C_CUSTOMIZEPIC);
    PrintY = CST_Y;

    //
    // KEYBOARD
    //
#ifndef SPEAR
    SETFONTCOLOR (READCOLOR, BKGDCOLOR);
    US_CPrint ("Keyboard\n");
#else
    PrintY += 13;
#endif
    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print (STR_CRUN);
    PrintX = CST_START + CST_SPC * 1;
    US_Print (STR_COPEN);
    PrintX = CST_START + CST_SPC * 2;
    US_Print (STR_CFIRE);
    PrintX = CST_START + CST_SPC * 3;
    US_Print (STR_CSTRAFE "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCustKeybd (0);
    US_Print ("\n");


    //
    // KEYBOARD MOVE KEYS
    //
    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print (STR_LEFT);
    PrintX = CST_START + CST_SPC * 1;
    US_Print (STR_RIGHT);
    PrintX = CST_START + CST_SPC * 2;
    US_Print (STR_FRWD);
    PrintX = CST_START + CST_SPC * 3;
    US_Print (STR_BKWD "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCustKeys (0);
    US_Print ("\n");

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print ("Wp1");
    PrintX = CST_START + CST_SPC * 1;
    US_Print ("Wp2");
    PrintX = CST_START + CST_SPC * 2;
    US_Print ("Wp3");
    PrintX = CST_START + CST_SPC * 3;
    US_Print ("Wp4" "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCust2Keybd (0);
    US_Print ("\n");

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print ("Wp+");
    PrintX = CST_START + CST_SPC * 1;
    US_Print ("Wp-");
    PrintX = CST_START + CST_SPC * 2;
    US_Print ("Menu");
    PrintX = CST_START + CST_SPC * 3;
    US_Print ("Pause" "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCust3Keybd (0);
    US_Print ("\n");

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    PrintX = CST_START;
    US_Print ("StrLft");
    PrintX = CST_START + CST_SPC * 1;
    US_Print ("StrRgt" "\n");
    DrawWindow (5, PrintY - 1, 310, 13, BKGDCOLOR);
    DrawCust4Keybd (0);
    US_Print ("\n");

    //
    // PICK STARTING POINT IN MENU
    //
    if (CusItems.curpos < 0)
        for (i = 0; i < CusItems.amount; i++)
            if (CusMenu[i].active)
            {
                CusItems.curpos = i;
                break;
            }


    VW_UpdateScreen ();
    MenuFadeIn ();
}


void
PrintCustMouse (int i)
{
    int j;

    for (j = 0; j < 4; j++)
        if (order[i] == buttonmouse[j])
        {
            PrintX = CST_START + CST_SPC * i;
            US_Print (mbarray[j]);
            break;
        }
}

void
DrawCustMouse (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    if (!mouseenabled)
    {
        SETFONTCOLOR (DEACTIVE, BKGDCOLOR);
        CusMenu[0].active = 0;
    }
    else
        CusMenu[0].active = 1;

    PrintY = CST_Y + 13 * 2;
    for (i = 0; i < 4; i++)
        PrintCustMouse (i);
}

void
PrintCustJoy (int i)
{
    // [FG] support up to 32 joystick buttons
    for (int j = 0; j < 32; j++)
    {
        if (order[i] == buttonjoy[j])
        {
            PrintX = CST_START + CST_SPC * i;
            US_Print (mbarray[j]);
            break;
        }
    }
}

// [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
void
PrintCust2Joy (int i)
{
    // [FG] support up to 32 joystick buttons
    for (int j = 0; j < 32; j++)
    {
        if (i + 8 == buttonjoy[j])
        {
            PrintX = CST_START + CST_SPC * i;
            US_Print (mbarray[j]);
            break;
        }
    }
}

// [FG] joystick buttons 3 (StrLft, StrRgt)
void
PrintCust3Joy (int i)
{
    // [FG] support up to 32 joystick buttons
    for (int j = 0; j < 32; j++)
    {
        if (i + 12 == buttonjoy[j])
        {
            PrintX = CST_START + CST_SPC * i;
            US_Print (mbarray[j]);
            break;
        }
    }
}

void
DrawCustJoy (int hilight)
{
    int i, color;

    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    if (!joystickenabled)
    {
        SETFONTCOLOR (DEACTIVE, BKGDCOLOR);
        Cus2Menu[3].active = 0;
    }
    else
        Cus2Menu[3].active = 1;

    // [FG] redraw other joystick buttons lines
    if (hilight)
    {
        DrawCust2Joy(0);
        DrawCust3Joy(0);
    }

    PrintY = CST_Y + 13 * 5;
    VWB_Bar(CST_START, PrintY - 1, 310 - CST_START + 5, 13, BKGDCOLOR);
    for (i = 0; i < 4; i++)
        PrintCustJoy (i);
}

// [FG] joystick buttons 2 (Wp+, Wp-, Menu, Pause)
void
DrawCust2Joy (int hilight)
{
    int i, color;

    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    if (!joystickenabled)
    {
        SETFONTCOLOR (DEACTIVE, BKGDCOLOR);
        Cus2Menu[5].active = 0;
    }
    else
        Cus2Menu[5].active = 1;

    // [FG] redraw other joystick buttons lines
    if (hilight)
    {
        DrawCustJoy(0);
        DrawCust3Joy(0);
    }

    PrintY = CST_Y + 13 * 7;
    VWB_Bar(CST_START, PrintY - 1, 310 - CST_START + 5, 13, BKGDCOLOR);
    for (i = 0; i < 4; i++)
        PrintCust2Joy (i);
}

// [FG] joystick buttons 3 (StrLft, StrRgt)
void
DrawCust3Joy (int hilight)
{
    int i, color;

    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    if (!joystickenabled)
    {
        SETFONTCOLOR (DEACTIVE, BKGDCOLOR);
        Cus2Menu[7].active = 0;
    }
    else
        Cus2Menu[7].active = 1;

    // [FG] redraw other joystick buttons lines
    if (hilight)
    {
        DrawCustJoy(0);
        DrawCust2Joy(0);
    }

    PrintY = CST_Y + 13 * 9;
    VWB_Bar(CST_START, PrintY - 1, 310 - CST_START + 5, 13, BKGDCOLOR);
    for (i = 0; i < 4; i++)
        PrintCust3Joy (i);
}


void
PrintCustKeybd (int i)
{
    PrintX = CST_START + CST_SPC * i;
    US_Print ((const char *) IN_GetScanName (buttonscan[order[i]]));
}

// [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
void
PrintCust2Keybd (int i)
{
    PrintX = CST_START + CST_SPC * i;
    US_Print ((const char *) IN_GetScanName (buttonscan[i+4]));
}

// [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
void
PrintCust3Keybd (int i)
{
    PrintX = CST_START + CST_SPC * i;
    US_Print ((const char *) IN_GetScanName (buttonscan[i+8]));
}

// [FG] keyboard buttons 4 (StrLft, StrRgt)
void
PrintCust4Keybd (int i)
{
    PrintX = CST_START + CST_SPC * i;
    US_Print ((const char *) IN_GetScanName (buttonscan[i+12]));
}

void
DrawCustKeybd (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    PrintY = CST_Y + 13 * 2;
    for (i = 0; i < 4; i++)
        PrintCustKeybd (i);
}

// [FG] keyboard buttons 2 (Wp1, Wp2, Wp3, Wp4)
void
DrawCust2Keybd (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    PrintY = CST_Y + 13 * 6;
    for (i = 0; i < 4; i++)
        PrintCust2Keybd (i);
}

// [FG] keyboard buttons 3 (Wp+, Wp-, Menu, Pause)
void
DrawCust3Keybd (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    PrintY = CST_Y + 13 * 8;
    for (i = 0; i < 4; i++)
        PrintCust3Keybd (i);
}

// [FG] keyboard buttons 4 (StrLft, StrRgt)
void
DrawCust4Keybd (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    PrintY = CST_Y + 13 * 10;
    for (i = 0; i < 2; i++)
        PrintCust4Keybd (i);
}

void
PrintCustKeys (int i)
{
    PrintX = CST_START + CST_SPC * i;
    US_Print ((const char *) IN_GetScanName (dirscan[moveorder[i]]));
}

void
DrawCustKeys (int hilight)
{
    int i, color;


    color = TEXTCOLOR;
    if (hilight)
        color = HIGHLIGHT;
    SETFONTCOLOR (color, BKGDCOLOR);

    PrintY = CST_Y + 13 * 4;
    for (i = 0; i < 4; i++)
        PrintCustKeys (i);
}


////////////////////////////////////////////////////////////////////
//
// QUIT THIS INFERNAL GAME!
//
////////////////////////////////////////////////////////////////////
// The quit confirmation; quits when it is accepted.
static void
NativeQuitPrompt (void)
{
    nativeConfirmQuit = true;
    int answer = Confirm (endStrings[US_RndT () & (0x7 + (US_RndT () & 1))]);
    nativeConfirmQuit = false;
    if (answer)
    {
        WriteConfig ();
        VW_UpdateScreen ();
        SD_MusicOff ();
        SD_StopSound ();
        Wolf_NativeMenuCue (WOLF_CUE_QUIT);
        MenuFadeOut ();
        // The quit sound plays out while the window fades away.
        Wolf_NativeQuitFade ();
        Quit (NULL);
    }
}

int
CP_Quit (int)
{
    NativeQuitPrompt ();
    DrawMainMenu ();
    return 0;
}


////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
//
// SUPPORT ROUTINES
//
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////
//
// Clear Menu screens to dark red
//
////////////////////////////////////////////////////////////////////
void
ClearMScreen (void)
{
#ifndef SPEAR
    VWB_Bar (0, 0, 320, 200, BORDCOLOR);
#else
    VWB_DrawPic (0, 0, C_BACKDROPPIC);
#endif
}


////////////////////////////////////////////////////////////////////
//
// Un/Cache a LUMP of graphics
//
////////////////////////////////////////////////////////////////////
void
CacheLump (int lumpstart, int lumpend)
{
    int i;

    for (i = lumpstart; i <= lumpend; i++)
        CA_CacheGrChunk (i);
}


void
UnCacheLump (int lumpstart, int lumpend)
{
    int i;

    for (i = lumpstart; i <= lumpend; i++)
        if (grsegs[i])
            UNCACHEGRCHUNK (i);
}


////////////////////////////////////////////////////////////////////
//
// Draw a window for a menu
//
////////////////////////////////////////////////////////////////////
void
DrawWindow (int x, int y, int w, int h, int wcolor)
{
    VWB_Bar (x, y, w, h, wcolor);
    DrawOutline (x, y, w, h, BORD2COLOR, DEACTIVE);
}


void
DrawOutline (int x, int y, int w, int h, int color1, int color2)
{
    VWB_Hlin (x, x + w, y, color2);
    VWB_Vlin (y, y + h, x, color2);
    VWB_Hlin (x, x + w, y + h, color1);
    VWB_Vlin (y, y + h, x + w, color1);
}


////////////////////////////////////////////////////////////////////
//
// Setup Control Panel stuff - graphics, etc.
//
////////////////////////////////////////////////////////////////////
void
SetupControlPanel (void)
{
    //
    // CACHE GRAPHICS & SOUNDS
    //
    CA_CacheGrChunk (STARTFONT + 1);
#ifndef SPEAR
    CacheLump (CONTROLS_LUMP_START, CONTROLS_LUMP_END);
#else
    CacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
#endif

    SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
    fontnumber = 1;
    WindowH = 200;
    if(screenHeight % 200 != 0)
        VL_ClearScreen(0);

    if (!ingame)
        CA_LoadAllSounds ();
    else
        MainMenu[savegame].active = 1;

    menuactive = true;
    IN_UpdateGrab();
}

////////////////////////////////////////////////////////////////////
//
// SEE WHICH SAVE GAME FILES ARE AVAILABLE & READ STRING IN
//
////////////////////////////////////////////////////////////////////
void SetupSaveGames()
{
    char name[13];
    char savepath[300];

    strcpy(name, SaveName);
    for(int i = 0; i < 10; i++)
    {
        name[7] = '0' + i;
        SaveGamesAvail[i]=0;SaveGameNames[i][0]=0;
            if(configdir[0])
                snprintf(savepath, sizeof(savepath), "%s/%s", configdir, name);
            else
                strcpy(savepath, name);

            const int handle = open(savepath, O_RDONLY | O_BINARY);
            if(handle >= 0)
            {
                char temp[32];

                SaveGamesAvail[i] = 1;
                if (read(handle, temp, 32) != 32)
                {
                    SaveGamesAvail[i] = 0;
                    close(handle);
                    continue;
                }
                close(handle);
                temp[31]=0;
                uint32_t header[4]={};
                int check=open(savepath,O_RDONLY);
                bool valid=check>=0 && lseek(check,32,SEEK_SET)==32 && read(check,header,sizeof(header))==sizeof(header) && header[0]==0x464c4f57 && header[1]==0x3143414d && header[2]==sizeof(objtype) && header[3]==sizeof(gamestate);
                if(check>=0)close(check);
                SaveGamesAvail[i]=valid;
                if(valid)strcpy(&SaveGameNames[i][0], temp);
            }
    }
}

////////////////////////////////////////////////////////////////////
//
// Clean up all the Control Panel stuff
//
////////////////////////////////////////////////////////////////////
void
CleanupControlPanel (void)
{
#ifndef SPEAR
    UnCacheLump (CONTROLS_LUMP_START, CONTROLS_LUMP_END);
#else
    UnCacheLump (BACKDROP_LUMP_START, BACKDROP_LUMP_END);
#endif

    fontnumber = 0;

    menuactive = false;
    nativeNotice[0] = 0;
}


// Native panel key repeat: a held direction steps again after a short
// pause, then at a steady pace. A tap returns as soon as it is released.
static bool MenuRepeat (bool held)
{
    ControlInfo ci;
    TicDelay (held ? 4 : 12);
    ReadAnyControl (&ci);
    return ci.dir != dir_None;
}

////////////////////////////////////////////////////////////////////
//
// Handle moving gun around a menu
//
////////////////////////////////////////////////////////////////////
int
HandleMenu (CP_iteminfo * item_i, CP_itemtype * items, void (*routine) (int w))
{
    // The shared native panel shows this page while its loop runs.
    struct NativePageScope {
        NativePageScope (CP_iteminfo *i, CP_itemtype *t) { nativeMenu = {i, t, i->curpos, true}; nativeOnly = NativeOnlyPage (t); nativeNotice[0] = 0; }
        ~NativePageScope () { nativeMenu.active = false; nativeOnly = false; }
    } nativePage (item_i, items);
    char key;
    static int redrawitem = 1, lastitem = -1;
    int i, x, y, basey, exit, which, shape;
    int32_t lastBlinkTime, timer;
    ControlInfo ci;
    bool held = false;


    which = item_i->curpos;
    x = item_i->x & -8;
    basey = item_i->y - 2;
    y = basey + which * 13;

    if (!nativeOnly)
        VWB_DrawPic (x, y, C_CURSOR1PIC);
    SetTextColor (items + which, 1);
    if (redrawitem && !nativeOnly)
    {
        PrintX = item_i->x + item_i->indent;
        PrintY = item_i->y + which * 13;
        US_Print ((items + which)->string);
    }
    //
    // CALL CUSTOM ROUTINE IF IT IS NEEDED
    //
    if (routine)
        routine (which);
    VW_UpdateScreen ();

    shape = C_CURSOR1PIC;
    timer = 8;
    exit = 0;
    lastBlinkTime = GetTimeCount ();
    IN_ClearKeysDown ();


    do
    {
        //
        // CHANGE GUN SHAPE
        //
        if ((int32_t)GetTimeCount () - lastBlinkTime > timer)
        {
            lastBlinkTime = GetTimeCount ();
            if (shape == C_CURSOR1PIC)
            {
                shape = C_CURSOR2PIC;
                timer = 8;
            }
            else
            {
                shape = C_CURSOR1PIC;
                timer = 70;
            }
            if (!nativeOnly)
                VWB_DrawPic (x, y, shape);
            if (routine)
                routine (which);
            VW_UpdateScreen ();
        }
        else Wolf_Delay(5);

        CheckPause ();
        if (Wolf_NativeQuitAsked ())
        {
            if (items == MainMenu)
                CP_Quit (0);
            else
                NativeQuitPrompt ();
        }

        //
        // SEE IF ANY KEYS ARE PRESSED FOR INITIAL CHAR FINDING
        //
        key = LastASCII;
        if (key)
        {
            int ok = 0;

            if (key >= 'a')
                key -= 'a' - 'A';

            for (i = which + 1; i < item_i->amount; i++)
                if ((items + i)->active && (items + i)->string[0] == key)
                {
                    EraseGun (item_i, items, x, y, which);
                    which = i;
                    DrawGun (item_i, items, x, &y, which, basey, routine);
                    ok = 1;
                    IN_ClearKeysDown ();
                    break;
                }

            //
            // DIDN'T FIND A MATCH FIRST TIME THRU. CHECK AGAIN.
            //
            if (!ok)
            {
                for (i = 0; i < which; i++)
                    if ((items + i)->active && (items + i)->string[0] == key)
                    {
                        EraseGun (item_i, items, x, y, which);
                        which = i;
                        DrawGun (item_i, items, x, &y, which, basey, routine);
                        IN_ClearKeysDown ();
                        break;
                    }
            }
        }

        //
        // GET INPUT
        //
        ReadAnyControl (&ci);
        nativeMenu.which = which;
        // A row clicked in the native panel is chosen like Enter.
        if (wolfMenuClick >= 0)
        {
            int clicked = wolfMenuClick;
            wolfMenuClick = -1;
            if (clicked < item_i->amount && (items + clicked)->active)
            {
                EraseGun (item_i, items, x, y, which);
                which = clicked;
                DrawGun (item_i, items, x, &y, which, basey, routine);
                exit = 1;
                break;
            }
        }
        // The pointer moving onto a row selects it, quietly.
        if (wolfMenuHover >= 0)
        {
            int hovered = wolfMenuHover;
            wolfMenuHover = -1;
            if (hovered != which && hovered < item_i->amount && (items + hovered)->active)
            {
                EraseGun (item_i, items, x, y, which);
                which = hovered;
                nativeMenu.which = which;
                if (!nativeOnly)
                    DrawGun (item_i, items, x, &y, which, basey, routine);
                else
                {
                    y = basey + which * 13;
                    if (routine)
                        routine (which);
                }
            }
        }
        // Left/right adjust a value row on pages that have them.
        if (nativeMenuAdjust && (ci.dir == dir_East || ci.dir == dir_West))
        {
            nativeMenuAdjust (which, ci.dir == dir_East ? 1 : -1);
            held = MenuRepeat (held);
            continue;
        }
        switch (ci.dir)
        {
                ////////////////////////////////////////////////
                //
                // MOVE UP
                //
            case dir_North:

                EraseGun (item_i, items, x, y, which);

                //
                // ANIMATE HALF-STEP
                //
                if (which && (items + which - 1)->active)
                {
                    y -= 6;
                    DrawHalfStep (x, y);
                }

                //
                // MOVE TO NEXT AVAILABLE SPOT
                //
                do
                {
                    if (!which)
                        which = item_i->amount - 1;
                    else
                        which--;
                }
                while (!(items + which)->active);

                DrawGun (item_i, items, x, &y, which, basey, routine);
                //
                // WAIT FOR BUTTON-UP OR DELAY NEXT MOVE
                //
                held = MenuRepeat (held);
                break;

                ////////////////////////////////////////////////
                //
                // MOVE DOWN
                //
            case dir_South:

                EraseGun (item_i, items, x, y, which);
                //
                // ANIMATE HALF-STEP
                //
                if (which != item_i->amount - 1 && (items + which + 1)->active)
                {
                    y += 6;
                    DrawHalfStep (x, y);
                }

                do
                {
                    if (which == item_i->amount - 1)
                        which = 0;
                    else
                        which++;
                }
                while (!(items + which)->active);

                DrawGun (item_i, items, x, &y, which, basey, routine);

                //
                // WAIT FOR BUTTON-UP OR DELAY NEXT MOVE
                //
                held = MenuRepeat (held);
                break;

            default:
                held = false;
                break;
        }

        if (ci.button0 || Keyboard[sc_Space] || Keyboard[sc_Enter])
            exit = 1;

        if ((ci.button1 && !Keyboard[sc_Alt]) || Keyboard[sc_Escape])
            exit = 2;

    }
    while (!exit);

    nativeMenu.which = which;
    IN_ClearKeysDown ();

    //
    // ERASE EVERYTHING
    //
    if (nativeOnly)
        redrawitem = 1;
    else if (lastitem != which)
    {
        VWB_Bar (x - 1, y, 25, 16, BKGDCOLOR);
        PrintX = item_i->x + item_i->indent;
        PrintY = item_i->y + which * 13;
        US_Print ((items + which)->string);
        redrawitem = 1;
    }
    else
        redrawitem = 0;

    if (routine)
        routine (which);
    VW_UpdateScreen ();

    item_i->curpos = which;

    lastitem = which;
    switch (exit)
    {
        case 1:
            //
            // CALL THE ROUTINE
            //
            if ((items + which)->routine != NULL)
            {
                ShootSnd ();
                MenuFadeOut ();
                (items + which)->routine (0);
            }
            return which;

        case 2:
            SD_PlaySound (ESCPRESSEDSND);
            return -1;
    }

    return 0;                   // JUST TO SHUT UP THE ERROR MESSAGES!
}


//
// ERASE GUN & DE-HIGHLIGHT STRING
//
void
EraseGun (CP_iteminfo * item_i, CP_itemtype * items, int x, int y, int which)
{
    if (nativeOnly)
        return;
    VWB_Bar (x - 1, y, 25, 16, BKGDCOLOR);
    SetTextColor (items + which, 0);

    PrintX = item_i->x + item_i->indent;
    PrintY = item_i->y + which * 13;
    US_Print ((items + which)->string);
    VW_UpdateScreen ();
}


//
// DRAW HALF STEP OF GUN TO NEXT POSITION
//
void
DrawHalfStep (int x, int y)
{
    if (nativeOnly)
        return;
    VWB_DrawPic (x, y, C_CURSOR1PIC);
    VW_UpdateScreen ();
    SD_PlaySound (MOVEGUN1SND);
    Wolf_Delay (8 * 100 / 7);
}


//
// DRAW GUN AT NEW POSITION
//
void
DrawGun (CP_iteminfo * item_i, CP_itemtype * items, int x, int *y, int which, int basey,
         void (*routine) (int w))
{
    nativeMenu.which = which;
    if (nativeOnly)
    {
        *y = basey + which * 13;
        SD_PlaySound (MOVEGUN2SND);
        return;
    }
    VWB_Bar (x - 1, *y, 25, 16, BKGDCOLOR);
    *y = basey + which * 13;
    VWB_DrawPic (x, *y, C_CURSOR1PIC);
    SetTextColor (items + which, 1);

    PrintX = item_i->x + item_i->indent;
    PrintY = item_i->y + which * 13;
    US_Print ((items + which)->string);

    //
    // CALL CUSTOM ROUTINE IF IT IS NEEDED
    //
    if (routine)
        routine (which);
    VW_UpdateScreen ();
    SD_PlaySound (MOVEGUN2SND);
}

////////////////////////////////////////////////////////////////////
//
// DELAY FOR AN AMOUNT OF TICS OR UNTIL CONTROLS ARE INACTIVE
//
////////////////////////////////////////////////////////////////////
void
TicDelay (int count)
{
    ControlInfo ci;

    int32_t startTime = GetTimeCount ();
    do
    {
        Wolf_Delay(5);
        ReadAnyControl (&ci);
    }
    while ((int32_t) GetTimeCount () - startTime < count && ci.dir != dir_None);
}


////////////////////////////////////////////////////////////////////
//
// Draw a menu
//
////////////////////////////////////////////////////////////////////
void
DrawMenu (CP_iteminfo * item_i, CP_itemtype * items)
{
    int i, which = item_i->curpos;


    WindowX = PrintX = item_i->x + item_i->indent;
    WindowY = PrintY = item_i->y;
    WindowW = 320;
    WindowH = 200;

    for (i = 0; i < item_i->amount; i++)
    {
        SetTextColor (items + i, which == i);

        PrintY = item_i->y + i * 13;
        if ((items + i)->active)
            US_Print ((items + i)->string);
        else
        {
            SETFONTCOLOR (DEACTIVE, BKGDCOLOR);
            US_Print ((items + i)->string);
            SETFONTCOLOR (TEXTCOLOR, BKGDCOLOR);
        }

        US_Print ("\n");
    }
}


////////////////////////////////////////////////////////////////////
//
// SET TEXT COLOR (HIGHLIGHT OR NO)
//
////////////////////////////////////////////////////////////////////
void
SetTextColor (CP_itemtype * items, int hlight)
{
    if (hlight)
    {
        SETFONTCOLOR (color_hlite[items->active], BKGDCOLOR);
    }
    else
    {
        SETFONTCOLOR (color_norml[items->active], BKGDCOLOR);
    }
}


////////////////////////////////////////////////////////////////////
//
// WAIT FOR CTRLKEY-UP OR BUTTON-UP
//
////////////////////////////////////////////////////////////////////
void
WaitKeyUp (void)
{
    ControlInfo ci;
    while (ReadAnyControl (&ci), ci.button0 |
           ci.button1 |
           ci.button2 | ci.button3 | Keyboard[sc_Space] | Keyboard[sc_Enter] | Keyboard[sc_Escape])
    {
        IN_WaitAndProcessEvents();
    }
}


////////////////////////////////////////////////////////////////////
//
// READ KEYBOARD, JOYSTICK AND MOUSE FOR INPUT
//
////////////////////////////////////////////////////////////////////
void
ReadAnyControl (ControlInfo * ci)
{
    int mouseactive = 0;

    IN_ReadControl (0, ci);

    if (mouseenabled)
    {
        int mousex, mousey, buttons;
        buttons = Wolf_GetRelativeMouseState(&mousex, &mousey);
        int middlePressed = buttons & Wolf_BUTTON(Wolf_BUTTON_MIDDLE);
        int rightPressed = buttons & Wolf_BUTTON(Wolf_BUTTON_RIGHT);
        buttons &= ~(Wolf_BUTTON(Wolf_BUTTON_MIDDLE) | Wolf_BUTTON(Wolf_BUTTON_RIGHT));
        if(middlePressed) buttons |= 1 << 2;
        if(rightPressed) buttons |= 1 << 1;

        if(mousey < -SENSITIVE)
        {
            ci->dir = dir_North;
            mouseactive = 1;
        }
        else if(mousey  > SENSITIVE)
        {
            ci->dir = dir_South;
            mouseactive = 1;
        }

        if(mousex < -SENSITIVE)
        {
            ci->dir = dir_West;
            mouseactive = 1;
        }
        else if(mousex > SENSITIVE)
        {
            ci->dir = dir_East;
            mouseactive = 1;
        }

        if (buttons)
        {
            ci->button0 = buttons & 1;
            ci->button1 = buttons & 2;
            ci->button2 = buttons & 4;
            ci->button3 = false;
            mouseactive = 1;
        }
    }
    else
    {
        // Keep disabled mouse motion from building up for a later control read.
        Wolf_GetRelativeMouseState (NULL, NULL);
    }

    if (joystickenabled && !mouseactive)
    {
        int jx, jy, jb;

        IN_GetJoyDelta (&jx, &jy);
        if (jy < -SENSITIVE)
            ci->dir = dir_North;
        else if (jy > SENSITIVE)
            ci->dir = dir_South;

        if (jx < -SENSITIVE)
            ci->dir = dir_West;
        else if (jx > SENSITIVE)
            ci->dir = dir_East;

        jb = IN_JoyButtons ();
        if (jb)
        {
            ci->button0 = jb & 1;
            ci->button1 = jb & 2;
            ci->button2 = jb & 4;
            ci->button3 = jb & 8;
        }
    }
}


////////////////////////////////////////////////////////////////////
//
// DRAW DIALOG AND CONFIRM YES OR NO TO QUESTION
//
////////////////////////////////////////////////////////////////////
int
Confirm (const char *string)
{
    int xit = 0, x, y, tick = 0, lastBlinkTime;
    int whichsnd[2] = { ESCPRESSEDSND, SHOOTSND };
    ControlInfo ci;

    Message (string);
    nativeNoticeConfirm = true;
    nativeConfirmChoice = 0;
    IN_ClearKeysDown ();
    WaitKeyUp ();

    //
    // BLINK CURSOR
    //
    x = PrintX;
    y = PrintY;
    lastBlinkTime = GetTimeCount();

    // The native panel shows the answers as rows: arrows move between them,
    // Enter or a click takes one; Y and N still answer directly.
    bool chosen = false;
    do
    {
        ReadAnyControl(&ci);
        if (wolfMenuClick == 0 || wolfMenuClick == 1)
        {
            nativeConfirmChoice = wolfMenuClick;
            chosen = true;
        }
        wolfMenuClick = -1;
        if (wolfMenuHover == 0 || wolfMenuHover == 1)
            nativeConfirmChoice = wolfMenuHover;
        wolfMenuHover = -1;
        if (ci.dir == dir_North || ci.dir == dir_South)
        {
            nativeConfirmChoice ^= 1;
            SD_PlaySound (MOVEGUN2SND);
            while (ReadAnyControl (&ci), ci.dir == dir_North || ci.dir == dir_South)
                Wolf_Delay (5);
        }
        if (Keyboard[sc_Enter] || Keyboard[sc_Return] || ci.button0)
            chosen = true;

        if (GetTimeCount() - lastBlinkTime >= 10)
        {
            switch (tick)
            {
                case 0:
                    VWB_Bar (x, y, 8, 13, TEXTCOLOR);
                    break;
                case 1:
                    PrintX = x;
                    PrintY = y;
                    US_Print ("_");
            }
            VW_UpdateScreen ();
            tick ^= 1;
            lastBlinkTime = GetTimeCount();
        }
        else Wolf_Delay(5);

    }
    while (!chosen && !Keyboard[sc_Y] && !Keyboard[sc_N] && !Keyboard[sc_Escape] && !ci.button1);

    if (Keyboard[sc_Y] || (chosen && nativeConfirmChoice == 0))
    {
        xit = 1;
        ShootSnd ();
    }

    IN_ClearKeysDown ();
    WaitKeyUp ();
    nativeNotice[0] = 0;
    nativeNoticeConfirm = false;

    SD_PlaySound ((soundnames) whichsnd[xit]);

    return xit;
}


////////////////////////////////////////////////////////////////////
//
// PRINT A MESSAGE IN A WINDOW
//
////////////////////////////////////////////////////////////////////
void
Message (const char *string)
{
    if (menuactive)
        snprintf (nativeNotice, sizeof(nativeNotice), "%s", string);
    int h = 0, w = 0, mw = 0, i, len = (int) strlen(string);
    fontstruct *font;


    CA_CacheGrChunk (STARTFONT + 1);
    fontnumber = 1;
    void *p = grsegs[STARTFONT + fontnumber];
    font = (fontstruct *) p;
    h = font->height;
    for (i = 0; i < len; i++)
    {
        if (string[i] == '\n')
        {
            if (w > mw)
                mw = w;
            w = 0;
            h += font->height;
        }
        else
            w += font->width[(size_t)string[i]];
    }

    if (w + 10 > mw)
        mw = w + 10;

    PrintY = (WindowH / 2) - h / 2;
    PrintX = WindowX = 160 - mw / 2;

    DrawWindow (WindowX - 5, PrintY - 5, mw + 10, h + 10, TEXTCOLOR);
    DrawOutline (WindowX - 5, PrintY - 5, mw + 10, h + 10, 0, HIGHLIGHT);
    SETFONTCOLOR (0, TEXTCOLOR);
    US_Print (string);
    VW_UpdateScreen ();
}

////////////////////////////////////////////////////////////////////
//
// THIS MAY BE FIXED A LITTLE LATER...
//
////////////////////////////////////////////////////////////////////
static int lastmusic;

int
StartCPMusic (int song)
{
    int lastoffs;

    lastmusic = song;
    lastoffs = SD_MusicOff ();
    UNCACHEAUDIOCHUNK (STARTMUSIC + lastmusic);

    SD_StartMusic(STARTMUSIC + song);
    return lastoffs;
}

void
FreeMusic (void)
{
    UNCACHEAUDIOCHUNK (STARTMUSIC + lastmusic);
}


///////////////////////////////////////////////////////////////////////////
//
//      IN_GetScanName() - Returns a string containing the name of the
//              specified scan code
//
///////////////////////////////////////////////////////////////////////////
const char *
IN_GetScanName (ScanCode scan)
{
/*    const char **p;
    ScanCode *s;

    for (s = ExtScanCodes, p = ExtScanNames; *s; p++, s++)
        if (*s == scan)
            return (*p);*/

    std::unordered_map<ScanCode, const char*>::iterator it = ScanNames.find(scan);
    if (it == ScanNames.end())
    {
        return "?";
    }
    return it->second;
}


///////////////////////////////////////////////////////////////////////////
//
// CHECK FOR PAUSE KEY (FOR MUSIC ONLY)
//
///////////////////////////////////////////////////////////////////////////
void
CheckPause (void)
{
    if (Paused)
    {
        switch (SoundStatus)
        {
            case 0:
                SD_MusicOn ();
                break;
            case 1:
                SD_MusicOff ();
                break;
        }

        SoundStatus ^= 1;
        VW_WaitVBL (3);
        IN_ClearKeysDown ();
        Paused = false;
    }
}


///////////////////////////////////////////////////////////////////////////
//
// DRAW GUN CURSOR AT CORRECT POSITION IN MENU
//
///////////////////////////////////////////////////////////////////////////
void
DrawMenuGun (CP_iteminfo * iteminfo)
{
    int x, y;


    x = iteminfo->x;
    y = iteminfo->y + iteminfo->curpos * 13 - 2;
    VWB_DrawPic (x, y, C_CURSOR1PIC);
}


///////////////////////////////////////////////////////////////////////////
//
// DRAW SCREEN TITLE STRIPES
//
///////////////////////////////////////////////////////////////////////////
void
DrawStripes (int y)
{
#ifndef SPEAR
    VWB_Bar (0, y, 320, 24, 0);
    VWB_Hlin (0, 319, y + 22, STRIPE);
#else
    VWB_Bar (0, y, 320, 22, 0);
    VWB_Hlin (0, 319, y + 23, 0);
#endif
}

void
ShootSnd (void)
{
    SD_PlaySound (SHOOTSND);
}


///////////////////////////////////////////////////////////////////////////
//
// CHECK FOR EPISODES
//
///////////////////////////////////////////////////////////////////////////
void
CheckForEpisodes (void)
{
    struct stat statbuf;

    // On Linux like systems, the configdir defaults to $HOME/.wolf4sdl
#if !defined(_WIN32)
    if(configdir[0] == 0)
    {
        // Set config location to home directory for multi-user support
        char *homedir = getenv("HOME");
        if(homedir == NULL)
        {
            Quit("Your $HOME directory is not defined. You must set this before playing.");
        }
        #define WOLFDIR "/.wolf4sdl"
        if(strlen(homedir) + sizeof(WOLFDIR) > sizeof(configdir))
        {
            Quit("Your $HOME directory path is too long. It cannot be used for saving games.");
        }
        snprintf(configdir, sizeof(configdir), "%s" WOLFDIR, homedir);
    }
#endif

    if(configdir[0] != 0)
    {
        // Ensure config directory exists and create if necessary
        if(stat(configdir, &statbuf) != 0)
        {
#ifdef _WIN32
            if(_mkdir(configdir) != 0)
#else
            if(mkdir(configdir, 0755) != 0)
#endif
            {
                Quit("The configuration directory \"%s\" could not be created.", configdir);
            }
        }
    }


//
// ENGLISH
//
#ifdef UPLOAD
    if(!stat("vswap.wl1", &statbuf))
    {
        strcpy (extension, "wl1");
        numEpisodesMissing = 5;
    }
    else
        Quit ("NO WOLFENSTEIN 3-D DATA FILES to be found!");
#else
#ifndef SPEAR
    if(!stat("vswap.wl6", &statbuf))
    {
        strcpy (extension, "wl6");
        NewEmenu[2].active =
            NewEmenu[4].active =
            NewEmenu[6].active =
            NewEmenu[8].active =
            NewEmenu[10].active =
            EpisodeSelect[1] =
            EpisodeSelect[2] = EpisodeSelect[3] = EpisodeSelect[4] = EpisodeSelect[5] = 1;
    }
    else
    {
        if(!stat("vswap.wl3", &statbuf))
        {
            strcpy (extension, "wl3");
            numEpisodesMissing = 3;
            NewEmenu[2].active = NewEmenu[4].active = EpisodeSelect[1] = EpisodeSelect[2] = 1;
        }
        else
        {
            if(!stat("vswap.wl1", &statbuf))
            {
                strcpy (extension, "wl1");
                numEpisodesMissing = 5;
            }
            else
                Quit ("NO WOLFENSTEIN 3-D DATA FILES to be found!");
        }
    }
#endif
#endif


#ifdef SPEAR
#ifndef SPEARDEMO
    if(param_mission == 0)
    {
        if(!stat("vswap.sod", &statbuf))
            strcpy (extension, "sod");
        else
            Quit ("NO SPEAR OF DESTINY DATA FILES TO BE FOUND!");
    }
    else if(param_mission == 1)
    {
        if(!stat("vswap.sd1", &statbuf))
            strcpy (extension, "sd1");
        else
            Quit ("NO SPEAR OF DESTINY DATA FILES TO BE FOUND!");
    }
    else if(param_mission == 2)
    {
        if(!stat("vswap.sd2", &statbuf))
            strcpy (extension, "sd2");
        else
            Quit ("NO SPEAR OF DESTINY DATA FILES TO BE FOUND!");
    }
    else if(param_mission == 3)
    {
        if(!stat("vswap.sd3", &statbuf))
            strcpy (extension, "sd3");
        else
            Quit ("NO SPEAR OF DESTINY DATA FILES TO BE FOUND!");
    }
    else
        Quit ("UNSUPPORTED MISSION!");
    strcpy (graphext, "sod");
    strcpy (audioext, "sod");
#else
    if(!stat("vswap.sdm", &statbuf))
    {
        strcpy (extension, "sdm");
    }
    else
        Quit ("NO SPEAR OF DESTINY DEMO DATA FILES TO BE FOUND!");
    strcpy (graphext, "sdm");
    strcpy (audioext, "sdm");
#endif
#else
    strcpy (graphext, extension);
    strcpy (audioext, extension);
#endif

    strcat (configname, extension);
    strcat (SaveName, extension);
    strcat (demoname, extension);

#ifndef SPEAR
    strcat (endfilename, extension);
#endif
}


////////////////////////////////////////////////////////////////////
//
// NATIVE PANEL PAGES (2026-10-10)
// Titles and row labels with their current values for the shared native
// menu panel. Pages with their own artwork (key customization) return -1 and
// keep the original drawing.
//
////////////////////////////////////////////////////////////////////
// Native menu cues: the sounds the control panel borrows from the game play
// the panel's own tones while it is open.
bool Wolf_MenuCue (int sound)
{
    if (!menuactive && !wolfMenuFrozen)
        return false;
    int cue;
    switch (sound)
    {
        case MOVEGUN2SND: cue = WOLF_CUE_MOVE; break;
        case MOVEGUN1SND: case SHOOTDOORSND: cue = WOLF_CUE_ADJUST; break;
        case SHOOTSND: cue = WOLF_CUE_SELECT; break;
        case ESCPRESSEDSND: cue = WOLF_CUE_BACK; break;
        case NOWAYSND: case HITWALLSND: cue = WOLF_CUE_DENY; break;
        default: return false;
    }
    Wolf_NativeMenuCue (cue);
    return true;
}

bool Wolf_NativeMenuNoticeConfirm ()
{
    return nativeNoticeConfirm;
}

// The panel title of a list page, or NULL for pages it does not show.
static const char *NativePageName (CP_itemtype *items)
{
    const char *name = NULL;
    if (items == MainMenu) name = "";
    else if (items == OptMenu) name = "Settings";
    else if (items == VideoMenu) name = "Video";
    else if (items == AudioMenu) name = "Audio";
    else if (items == PlayMenu) name = "Gameplay";
    else if (items == ControlsMenu) name = "Controls";
    else if (items == PadMenu) name = "Controller";
    else if (items == KeyMenu) name = "Customize Keyboard";
    else if (items == MouseMenu) name = "Customize Mouse";
#ifndef SPEAR
    else if (items == NewEmenu) name = "Choose Episode";
#endif
    else if (items == NewMenu) name = "Choose Difficulty";
    else if (items == LSMenu) name = nativeLoadSave ? "Save Game" : "Load Game";
    return name;
}

static bool NativeOnlyPage (CP_itemtype *items)
{
    return NativePageName (items) != NULL;
}

int Wolf_NativeMenuPage (char *title, int capacity)
{
    if (menuactive && nativeNotice[0])
    {
        snprintf (title, capacity, "%s", nativeNotice);
        if (!nativeNoticeConfirm)
            return 0;
        // The answers are rows, so the "(Y or N)" prompt is left out.
        if (char *prompt = strstr (title, "(Y"))
        {
            *prompt = 0;
            for (size_t n = strlen (title); n && (title[n - 1] == ' ' || title[n - 1] == '\n'); --n)
                title[n - 1] = 0;
        }
        return 2;
    }
    if (nativeMenu.active && nativeMenu.items == MainMenu && MainMenu[backtodemo].active)
    {
        snprintf (title, capacity, "Paused");
        return nativeMenu.info->amount;
    }
    if (!nativeMenu.active)
        return -1;
    const char *name = NativePageName (nativeMenu.items);
    if (!name)
        return -1;
    if (nativeMenu.items == PadMenu)
    {
        // The connected controller's own button names.
        const char *pad = Wolf_NativePadButton (WOLF_BUTTON_NAME);
        if (!pad)
        {
            snprintf (title, capacity, "%s\nNo controller connected. Connect a DualSense, DualShock, Xbox or other game controller with Bluetooth or USB.", name);
            return nativeMenu.info->amount;
        }
        const char *touch = Wolf_NativePadButton (WOLF_BUTTON_TOUCHPAD), *pause = Wolf_NativePadButton (WOLF_BUTTON_PAUSE);
        int n = snprintf (title, capacity, "%s\n%s\nFire: %s   Use: %s   Run: %s or %s\nWeapons: %s, %s, d-pad%s%s\nMenu: %s", name, pad,
            Wolf_NativePadButton (WOLF_BUTTON_RT), Wolf_NativePadButton (WOLF_BUTTON_SOUTH), Wolf_NativePadButton (WOLF_BUTTON_EAST), Wolf_NativePadButton (WOLF_BUTTON_LT),
            Wolf_NativePadButton (WOLF_BUTTON_LB), Wolf_NativePadButton (WOLF_BUTTON_RB), touch ? "   Status bar: " : "", touch ? touch : "", Wolf_NativePadButton (WOLF_BUTTON_MENU));
        if (pause && n > 0 && n < capacity)
            snprintf (title + n, capacity - n, "   Pause: %s", pause);
        return nativeMenu.info->amount;
    }
    snprintf (title, capacity, "%s", name);
    return nativeMenu.info->amount;
}

int Wolf_NativeMenuSelection (void)
{
    if (menuactive && nativeNotice[0] && nativeNoticeConfirm)
        return nativeConfirmChoice;
    return nativeMenu.active ? nativeMenu.which : -1;
}

int Wolf_NativeMenuRow (int index, char *label, int capacity)
{
    if (menuactive && nativeNotice[0] && nativeNoticeConfirm)
    {
        if (index < 0 || index > 1)
            return -1;
        snprintf (label, capacity, "%s", index ? "Cancel" : nativeConfirmQuit ? "Quit" : "Yes");
        return 1;
    }
    if (!nativeMenu.active || index < 0 || index >= nativeMenu.info->amount)
        return -1;
    CP_itemtype *items = nativeMenu.items, *item = items + index;
    int status = item->active ? 1 : 0;
    auto mark = [] (bool on) { return on ? "On" : "Off"; };
    if (items == LSMenu)
    {
        bool latest = index == latestWolfSlot ();
        snprintf (label, capacity, "%d. %s%s", index + 1, SaveGamesAvail[index] ? SaveGameNames[index] : "Empty", latest ? " (Latest)" : "");
        return status;
    }
    if (!item->string[0] || (items == MainMenu && !item->active))
        return -1;
    if (items == MainMenu && index == backtodemo)
    {
        snprintf (label, capacity, "Resume");
        return status;
    }
    if (items == VideoMenu)
    {
        switch (index)
        {
            case VIDEO_FULLSCREEN: snprintf (label, capacity, "Fullscreen    %s", mark (Wolf_NativeFullscreen ())); return status;
            case VIDEO_STATUSBAR: snprintf (label, capacity, "Status Bar    %s", mark (Wolf_NativeStatusBar ())); return status;
        }
    }
    if (items == AudioMenu)
    {
        snprintf (label, capacity, "%s    %s", item->string, mark (index == AUDIO_EFFECTS ? EffectsOn () : MusicMode != smm_Off));
        return status;
    }
    if (items == PlayMenu)
    {
        switch (index)
        {
            case PLAY_ALWAYSRUN: snprintf (label, capacity, "Always Run    %s", mark (always_run)); return status;
            case PLAY_CROSSHAIR: snprintf (label, capacity, "Crosshair    %s", mark (crosshair)); return status;
        }
    }
    if (items == ControlsMenu)
    {
        switch (index)
        {
            case CONTROLS_SENS: snprintf (label, capacity, "Mouse Sensitivity    %d", mouseadjustment + 1); return status;
            case CONTROLS_SENSV: snprintf (label, capacity, "Vertical Sensitivity    %d", mouseadjustment_v + 1); return status;
        }
    }
    if (items == PadMenu)
    {
        // DualSense triggers resist; others may vibrate in the triggers.
        // A row the connected controller cannot use is shown dimmed.
        int kind = Wolf_NativePadKind ();
        int vibration = kind && !Wolf_NativePadVibration () ? 0 : status, triggers = kind && !Wolf_NativePadTriggerEffects () ? 0 : status;
        switch (index)
        {
            case PAD_HAPTICS: snprintf (label, capacity, "Vibration    %s", mark (Wolf_NativePadValue (WOLF_PAD_HAPTICS))); return vibration;
            case PAD_HAPTICSTRENGTH: snprintf (label, capacity, "Vibration Strength    %d", Wolf_NativePadValue (WOLF_PAD_HAPTIC_STRENGTH)); return vibration;
            case PAD_TRIGGERS: snprintf (label, capacity, "%s    %s", kind == 1 ? "Adaptive Triggers" : "Trigger Effects", mark (Wolf_NativePadValue (WOLF_PAD_TRIGGERS))); return triggers;
            case PAD_TRIGGERSTRENGTH: snprintf (label, capacity, "%s    %d", kind == 1 ? "Trigger Resistance" : "Trigger Strength", Wolf_NativePadValue (WOLF_PAD_TRIGGER_STRENGTH)); return triggers;
            case PAD_DEADZONE: snprintf (label, capacity, "Stick Deadzone    %d", Wolf_NativePadValue (WOLF_PAD_DEADZONE)); return status;
            case PAD_TURN: snprintf (label, capacity, "Turn Sensitivity    %d", Wolf_NativePadValue (WOLF_PAD_TURN)); return status;
        }
    }
    if (items == KeyMenu)
    {
        snprintf (label, capacity, "%s    %s", keyRows[index].name, IN_GetScanName ((ScanCode) *keyRows[index].scan));
        return status;
    }
    if (items == MouseMenu)
    {
        const char *action = "None";
        for (unsigned i = 0; i < lengthof(mouseActions); i++)
            if (mouseActions[i].button == buttonmouse[index])
                action = mouseActions[i].name;
        snprintf (label, capacity, "%s    %s", mouseButtonNames[index], action);
        return status;
    }
    // Multi-line strings (episode names) read as one line.
    snprintf (label, capacity, "%s", item->string);
    for (char *c = label; *c; ++c)
        if (*c == '\n') *c = ' ';
    return status;
}
