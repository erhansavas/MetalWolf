// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
// WL_MAIN.C

#ifdef _WIN32
    #include <io.h>
#else
    #include <unistd.h>
#endif

#include "wl_def.h"
#include <cstddef>
#include <array>
#include <vector>
#include <new>
#include <memory>
#include "wl_atmos.h"
#include "WolfNative.hpp"


/*
=============================================================================

                             WOLFENSTEIN 3-D

                        An Id Software production

                             by John Carmack

=============================================================================
*/

/*
=============================================================================

                             LOCAL CONSTANTS

=============================================================================
*/


#define FOCALLENGTH     (0x5700l)               // in global coordinates
#define VIEWGLOBAL      0x10000                 // globals visable flush to wall

#define VIEWWIDTH       256                     // size of view window
#define VIEWHEIGHT      144

/*
=============================================================================

                            GLOBAL VARIABLES

=============================================================================
*/

char    str[80];
int     dirangle[9] = {0,ANGLES/8,2*ANGLES/8,3*ANGLES/8,4*ANGLES/8,
                       5*ANGLES/8,6*ANGLES/8,7*ANGLES/8,ANGLES};

//
// proejection variables
//
fixed    focallength;
unsigned screenofs;
int      viewscreenx, viewscreeny;
int      viewwidth;
int      viewheight;
short    centerx;
int      shootdelta;           // pixels away from centerx a target can be
fixed    scale;
int32_t  heightnumerator;


void    Quit (const char *error,...);

boolean startgame;
boolean loadedgame;
int     mouseadjustment, mouseadjustment_v;

char    configdir[256] = "";
char    configname[13] = "config.";

//
// Command line parameter variables
//
boolean param_debugmode = false;
boolean param_nowait = false;
boolean param_demotest = false;
int     param_difficulty = 1;           // default is "normal"
int     param_tedlevel = -1;            // default is not to start a level
int     param_joystickindex = 0;

int     param_joystickhat = -1;
int     param_samplerate = 44100;
int     param_audiobuffer = -1;

int     param_mission = 0;
boolean param_ignorenumchunks = false;
boolean always_run = false;

/*
=============================================================================

                            LOCAL VARIABLES

=============================================================================
*/


/*
====================
=
= ReadConfig
=
====================
*/

void ReadConfig(void)
{
    SDMode  sd;
    SMMode  sm;
    SDSMode sds;
    HighScore defaultScores[MaxScores];
    int defaultDirections[4],defaultButtons[NUMBUTTONS],defaultMouse[4],defaultJoystick[32];
    memcpy(defaultScores,Scores,sizeof(defaultScores));
    memcpy(defaultDirections,dirscan,sizeof(defaultDirections));
    memcpy(defaultButtons,buttonscan,sizeof(defaultButtons));
    memcpy(defaultMouse,buttonmouse,sizeof(defaultMouse));
    memcpy(defaultJoystick,buttonjoy,sizeof(defaultJoystick));

    char configpath[300];

    if(configdir[0])
        snprintf(configpath, sizeof(configpath), "%s/%s", configdir, configname);
    else
        strcpy(configpath, configname);

#define read2(a,b,c) if(read(a,b,c)!=ssize_t(c)){close(file);goto noconfig;}
    const int file = open(configpath, O_RDONLY | O_BINARY);
    if (file != -1)
    {
        //
        // valid config file
        //
        word tmp;
        read2(file,&tmp,sizeof(tmp));
        if(tmp!=0xfefa)
        {
            close(file);
            goto noconfig;
        }
        read2(file,Scores,sizeof(HighScore) * MaxScores);

        read2(file,&sd,sizeof(sd));
        read2(file,&sm,sizeof(sm));
        read2(file,&sds,sizeof(sds));

        read2(file,&mouseenabled,sizeof(mouseenabled));
        read2(file,&joystickenabled,sizeof(joystickenabled));
        boolean dummyJoypadEnabled;
        read2(file,&dummyJoypadEnabled,sizeof(dummyJoypadEnabled));
        boolean dummyJoystickProgressive;
        read2(file,&dummyJoystickProgressive,sizeof(dummyJoystickProgressive));
        int dummyJoystickPort = 0;
        read2(file,&dummyJoystickPort,sizeof(dummyJoystickPort));

        read2(file,dirscan,sizeof(dirscan));
        read2(file,buttonscan,sizeof(buttonscan));
        read2(file,buttonmouse,sizeof(buttonmouse));
        read2(file,buttonjoy,sizeof(buttonjoy));

        read2(file,&viewsize,sizeof(viewsize));
        read2(file,&mouseadjustment,sizeof(mouseadjustment));

        // [FG] vertical mouse sensitivity
        if (read(file,&mouseadjustment_v,sizeof(mouseadjustment_v))!=sizeof(mouseadjustment_v))
        {
            mouseadjustment_v = mouseadjustment;
        }

        // [FG] toggle always run
        if (read(file,&always_run,sizeof(always_run))!=sizeof(always_run))
        {
            always_run = false;
        }

        // [FG] toggle crosshair
        if (read(file,&crosshair,sizeof(crosshair))!=sizeof(crosshair))
        {
            crosshair = false;
        }

        close(file);
#undef read2
        if(sd<sdm_Off || sd>sdm_AdLib || sm<smm_Off || sm>smm_AdLib || sds<sds_Off || sds>sds_SoundBlaster)goto noconfig;
        for(int i=0;i<MaxScores;++i)Scores[i].name[MaxHighName]='\0';
        for(auto &button:buttonmouse)if(button<bt_nobutton || button>=NUMBUTTONS)button=bt_nobutton;
        for(auto &button:buttonjoy)if(button<bt_nobutton || button>=NUMBUTTONS)button=bt_nobutton;

        if ((sd == sdm_AdLib || sm == smm_AdLib) && !AdLibPresent
                && !SoundBlasterPresent)
        {
            sd = sdm_PC;
            sm = smm_Off;
        }

        if ((sds == sds_SoundBlaster && !SoundBlasterPresent))
            sds = sds_Off;

        // make sure values are correct

        // Native (2026-10-10): the menu has no mouse switch; the mouse is on
        // whenever one is present.
        mouseenabled = MousePresent;
        if(joystickenabled) joystickenabled=true;

        if (!IN_JoyPresent())
            joystickenabled = false;

        if(mouseadjustment<0) mouseadjustment=0;
        else if(mouseadjustment>9) mouseadjustment=9;
        if(mouseadjustment_v<0) mouseadjustment_v=0;
        else if(mouseadjustment_v>9) mouseadjustment_v=9;

        if(viewsize<4) viewsize=4;
        else if(viewsize>21) viewsize=21;

        always_run = !!always_run;
        crosshair = !!crosshair;

        MainItems.curpos=newgame;
    }
    else
    {
        //
        // no config file, so select by hardware
        //
noconfig:
        // Short/corrupt files must not leave half-read bindings or score names.
        memcpy(Scores,defaultScores,sizeof(defaultScores));
        memcpy(dirscan,defaultDirections,sizeof(defaultDirections));
        memcpy(buttonscan,defaultButtons,sizeof(defaultButtons));
        memcpy(buttonmouse,defaultMouse,sizeof(defaultMouse));
        memcpy(buttonjoy,defaultJoystick,sizeof(defaultJoystick));
        mouseenabled=MousePresent;
        joystickenabled=IN_JoyPresent();
        if (SoundBlasterPresent || AdLibPresent)
        {
            sd = sdm_AdLib;
            sm = smm_AdLib;
        }
        else
        {
            sd = sdm_PC;
            sm = smm_Off;
        }

        if (SoundBlasterPresent)
            sds = sds_SoundBlaster;
        else
            sds = sds_Off;

        if (MousePresent)
            mouseenabled = true;

        if (IN_JoyPresent())
            joystickenabled = true;

        viewsize = 21;                          // native: the full-screen view; the status card shows over it
        mouseadjustment_v=mouseadjustment=5;
        always_run = false;
        crosshair = false;
    }

    SD_SetMusicMode (sm);
    SD_SetSoundMode (sd);
    SD_SetDigiDevice (sds);
}

/*
====================
=
= WriteConfig
=
====================
*/

void WriteConfig(void)
{
    char configpath[300];
    char temporary[320];

    if(configdir[0])
        snprintf(configpath, sizeof(configpath), "%s/%s", configdir, configname);
    else
        strcpy(configpath, configname);

    snprintf(temporary,sizeof(temporary),"%s.XXXXXX",configpath);
#define write2(a,b,c) if(write(a,b,c)!=ssize_t(c)){close(file);unlink(temporary);return;}
    const int file = mkstemp(temporary);
    if (file != -1)
    {
        word tmp=0xfefa;
        write2(file,&tmp,sizeof(tmp));
        write2(file,Scores,sizeof(HighScore) * MaxScores);

        write2(file,&SoundMode,sizeof(SoundMode));
        write2(file,&MusicMode,sizeof(MusicMode));
        write2(file,&DigiMode,sizeof(DigiMode));

        write2(file,&mouseenabled,sizeof(mouseenabled));
        write2(file,&joystickenabled,sizeof(joystickenabled));
        boolean dummyJoypadEnabled = false;
        write2(file,&dummyJoypadEnabled,sizeof(dummyJoypadEnabled));
        boolean dummyJoystickProgressive = false;
        write2(file,&dummyJoystickProgressive,sizeof(dummyJoystickProgressive));
        int dummyJoystickPort = 0;
        write2(file,&dummyJoystickPort,sizeof(dummyJoystickPort));

        write2(file,dirscan,sizeof(dirscan));
        write2(file,buttonscan,sizeof(buttonscan));
        write2(file,buttonmouse,sizeof(buttonmouse));
        write2(file,buttonjoy,sizeof(buttonjoy));

        write2(file,&viewsize,sizeof(viewsize));
        write2(file,&mouseadjustment,sizeof(mouseadjustment));

        // [FG] vertical mouse sensitivity
        write2(file,&mouseadjustment_v,sizeof(mouseadjustment_v));

        // [FG] toggle always run
        write2(file,&always_run,sizeof(always_run));

        // [FG] toggle crosshair
        write2(file,&crosshair,sizeof(crosshair));

        bool written=fsync(file)==0;
        if(close(file)!=0)written=false;
        if(!written || rename(temporary,configpath)!=0)unlink(temporary);
    }
#undef write2
}


//===========================================================================

/*
=====================
=
= NewGame
=
= Set up new game to start from the beginning
=
=====================
*/

void NewGame (int difficulty,int episode)
{
    memset (&gamestate,0,sizeof(gamestate));
    gamestate.difficulty = difficulty;
    gamestate.weapon = gamestate.bestweapon
            = gamestate.chosenweapon = wp_pistol;
    gamestate.health = 100;
    gamestate.ammo = STARTAMMO;
    gamestate.lives = 3;
    gamestate.nextextra = EXTRAPOINTS;
    gamestate.episode=episode;

    startgame = true;
}

//===========================================================================

void DiskFlopAnim(int x,int y)
{
    static int8_t which=0;
    if (!x && !y)
        return;
    VWB_DrawPic(x,y,C_DISKLOADING1PIC+which);
    VW_UpdateScreen();
    which^=1;
}


int32_t DoChecksum(byte *source,unsigned size,int32_t checksum)
{
    unsigned i;

    for (i=0;i<size-1;i++)
    checksum += source[i]^source[i+1];

    return checksum;
}


/*
==================
=
= SaveTheGame
=
==================
*/

boolean SaveTheGame(FILE *file,int x,int y)
{
    if(!file || !player)return false;
    std::array<word,MAXACTORS> savedActorIndex{};
    savedActorIndex.fill(0xffff);
    unsigned savedActorCount=0;
    for(objtype *actor=player;actor;actor=actor->next) {
        uintptr_t address=reinterpret_cast<uintptr_t>(actor),base=reinterpret_cast<uintptr_t>(objlist);
        if(address<base || address-base>=sizeof(objlist) || (address-base)%sizeof(objtype) || savedActorCount>=MAXACTORS)return false;
        auto slot=(address-base)/sizeof(objtype);
        if(savedActorIndex[slot]!=0xffff)return false;
        savedActorIndex[slot]=word(savedActorCount++);
    }
//    struct diskfree_t dfree;
//    int32_t avail,size,checksum;
    int checksum;
    objtype *ob;
    objtype nullobj;
    statobj_t nullstat;

/*    if (_dos_getdiskfree(0,&dfree))
        Quit("Error in _dos_getdiskfree call");

    avail = (int32_t)dfree.avail_clusters *
                  dfree.bytes_per_sector *
                  dfree.sectors_per_cluster;

    size = 0;
    for (ob = player; ob ; ob=ob->next)
        size += sizeof(*ob);
    size += sizeof(nullobj);

    size += sizeof(gamestate) +
            sizeof(LRstruct)*LRpack +
            sizeof(tilemap) +
            sizeof(actorat) +
            sizeof(laststatobj) +
            sizeof(statobjlist) +
            sizeof(doorposition) +
            sizeof(pwallstate) +
            sizeof(pwalltile) +
            sizeof(pwallx) +
            sizeof(pwally) +
            sizeof(pwalldir) +
            sizeof(pwallpos);

    if (avail < size)
    {
        Message(STR_NOSPACE1"\n"STR_NOSPACE2);
        return false;
    }*/

    checksum = 0;
    const uint32_t nativeHeader[4]={0x464c4f57,0x3143414d,sizeof(objtype),sizeof(gamestate)};
    if(fwrite(nativeHeader,sizeof(nativeHeader),1,file)!=1)return false;

    DiskFlopAnim(x,y);
    fwrite(&gamestate,sizeof(gamestate),1,file);
    checksum = DoChecksum((byte *)&gamestate,sizeof(gamestate),checksum);

    DiskFlopAnim(x,y);
    fwrite(&LevelRatios[0],sizeof(LRstruct)*LRpack,1,file);
    checksum = DoChecksum((byte *)&LevelRatios[0],sizeof(LRstruct)*LRpack,checksum);

    DiskFlopAnim(x,y);
    fwrite(tilemap,sizeof(tilemap),1,file);
    checksum = DoChecksum((byte *)tilemap,sizeof(tilemap),checksum);
    DiskFlopAnim(x,y);

    int i;
    for(i=0;i<MAPSIZE;i++)
    {
        for(int j=0;j<MAPSIZE;j++)
        {
            word actnum;
            objtype *objptr=actorat[i][j];
            if(ISPOINTER(objptr)) {
                uintptr_t address=reinterpret_cast<uintptr_t>(objptr),base=reinterpret_cast<uintptr_t>(objlist);
                if(address<base || address-base>=sizeof(objlist) || (address-base)%sizeof(objtype))return false;
                word slot=savedActorIndex[(address-base)/sizeof(objtype)];
                if(slot==0xffff)return false;
                actnum=0x8000 | slot;
            }
            else
                actnum=(word)(uintptr_t)objptr;
            fwrite(&actnum,sizeof(actnum),1,file);
            checksum = DoChecksum((byte *)&actnum,sizeof(actnum),checksum);
        }
    }

    fwrite (areaconnect,sizeof(areaconnect),1,file);
    fwrite (areabyplayer,sizeof(areabyplayer),1,file);

    // player object needs special treatment as it's in WL_AGENT.CPP and not in
    // WL_ACT2.CPP which could cause problems for the relative addressing

    ob = player;
    DiskFlopAnim(x,y);
    memcpy(&nullobj,ob,sizeof(nullobj));
    nullobj.next=nullobj.prev=NULL;
    nullobj.state=(statetype *) (nullobj.state-states); // [FG] statetype states[] array
    fwrite(&nullobj,sizeof(nullobj),1,file);
    ob = ob->next;

    DiskFlopAnim(x,y);
    for (; ob ; ob=ob->next)
    {
        memcpy(&nullobj,ob,sizeof(nullobj));
        nullobj.next=nullobj.prev=NULL;
    nullobj.state=(statetype *) (nullobj.state-states); // [FG] statetype states[] array
        fwrite(&nullobj,sizeof(nullobj),1,file);
    }
    nullobj.active = ac_badobject;          // end of file marker
    DiskFlopAnim(x,y);
    fwrite(&nullobj,sizeof(nullobj),1,file);

    DiskFlopAnim(x,y);
    word laststatobjnum=(word) (laststatobj-statobjlist);
    fwrite(&laststatobjnum,sizeof(laststatobjnum),1,file);
    checksum = DoChecksum((byte *)&laststatobjnum,sizeof(laststatobjnum),checksum);

    DiskFlopAnim(x,y);
    for(i=0;i<MAXSTATS;i++)
    {
        memcpy(&nullstat,statobjlist+i,sizeof(nullstat));
        nullstat.visspot=i<laststatobj-statobjlist?(byte *) ((uintptr_t) nullstat.visspot-(uintptr_t)spotvis):NULL;
        fwrite(&nullstat,sizeof(nullstat),1,file);
        checksum = DoChecksum((byte *)&nullstat,sizeof(nullstat),checksum);
    }

    DiskFlopAnim(x,y);
    fwrite (doorposition,sizeof(doorposition),1,file);
    checksum = DoChecksum((byte *)doorposition,sizeof(doorposition),checksum);
    DiskFlopAnim(x,y);
    fwrite (doorobjlist,sizeof(doorobjlist),1,file);
    checksum = DoChecksum((byte *)doorobjlist,sizeof(doorobjlist),checksum);

    DiskFlopAnim(x,y);
    fwrite (&pwallstate,sizeof(pwallstate),1,file);
    checksum = DoChecksum((byte *)&pwallstate,sizeof(pwallstate),checksum);
    fwrite (&pwalltile,sizeof(pwalltile),1,file);
    checksum = DoChecksum((byte *)&pwalltile,sizeof(pwalltile),checksum);
    fwrite (&pwallx,sizeof(pwallx),1,file);
    checksum = DoChecksum((byte *)&pwallx,sizeof(pwallx),checksum);
    fwrite (&pwally,sizeof(pwally),1,file);
    checksum = DoChecksum((byte *)&pwally,sizeof(pwally),checksum);
    fwrite (&pwalldir,sizeof(pwalldir),1,file);
    checksum = DoChecksum((byte *)&pwalldir,sizeof(pwalldir),checksum);
    fwrite (&pwallpos,sizeof(pwallpos),1,file);
    checksum = DoChecksum((byte *)&pwallpos,sizeof(pwallpos),checksum);

    //
    // WRITE OUT CHECKSUM
    //
    fwrite (&checksum,sizeof(checksum),1,file);

    fwrite (&lastgamemusicoffset,sizeof(lastgamemusicoffset),1,file);

    return(!ferror(file));
}

//===========================================================================

/*
==================
=
= LoadTheGame
=
==================
*/

// Validate an immutable copy before SetupGameLevel or any live-state writes.
// A failed load must leave the running game usable.
static bool NativeSaveIsValid(const std::vector<byte> &bytes)
{
    size_t cursor=0;
    int32_t checksum=0;
    auto take=[&](void *out,size_t size,bool checked=false) {
        if(size>bytes.size()-cursor)return false;
        memcpy(out,bytes.data()+cursor,size);
        if(checked)checksum=DoChecksum(const_cast<byte *>(bytes.data()+cursor),unsigned(size),checksum);
        cursor+=size;return true;
    };
    uint32_t header[4];gametype game;
    if(!take(header,sizeof(header)) || header[0]!=0x464c4f57 || header[1]!=0x3143414d || header[2]!=sizeof(objtype) || header[3]!=sizeof(gamestate) || !take(&game,sizeof(game),true))return false;
    extern int EpisodeSelect[6];
    if(game.episode<0 || game.episode>5 || !EpisodeSelect[game.episode] || game.mapon<0 || game.mapon>9 || game.difficulty<gd_baby || game.difficulty>gd_hard || game.weapon<wp_knife || game.weapon>wp_chaingun || game.bestweapon<wp_knife || game.bestweapon>wp_chaingun || game.chosenweapon<wp_knife || game.chosenweapon>wp_chaingun || game.attackframe<0 || game.attackframe>=14)return false;
    std::array<LRstruct,LRpack> ratios;
    byte tiles[MAPSIZE][MAPSIZE];word actors[MAPSIZE][MAPSIZE];
    byte connections[NUMAREAS][NUMAREAS];boolean areas[NUMAREAS];
    if(!take(ratios.data(),sizeof(ratios),true) || !take(tiles,sizeof(tiles),true))return false;
    for(int i=0;i<MAPSIZE;++i)for(int j=0;j<MAPSIZE;++j) {
        if(!take(&actors[i][j],sizeof(word),true))return false;
        if((actors[i][j]&0x8000) && (actors[i][j]&0x7fff)>=MAXACTORS)return false;
        if((tiles[i][j]&0x80) && (tiles[i][j]&0x7f)>=MAXDOORS)return false;
    }
    if(!take(connections,sizeof(connections)) || !take(areas,sizeof(areas)))return false;
    unsigned count=0;
    while(true) {
        objtype actor;
        if(!take(&actor,sizeof(actor)))return false;
        if(actor.active==ac_badobject){if(!count)return false;break;}
        if(++count>MAXACTORS || uintptr_t(actor.state)>=numstates || actor.active<ac_no || actor.active>ac_allways || actor.obclass<nothing || actor.obclass>sparkobj || actor.dir<east || actor.dir>nodir || actor.tilex>=MAPSIZE || actor.tiley>=MAPSIZE || actor.areanumber>=NUMAREAS || actor.x<0 || actor.y<0 || actor.x>=MAPSIZE*TILEGLOBAL || actor.y>=MAPSIZE*TILEGLOBAL)return false;
        if(count==1 && (actor.obclass!=playerobj || actor.angle<0 || actor.angle>=ANGLES))return false;
    }
    word statCount;
    if(!take(&statCount,sizeof(statCount),true) || statCount>MAXSTATS)return false;
    for(int i=0;i<MAXSTATS;++i) {
        statobj_t object;
        if(!take(&object,sizeof(object),true))return false;
        if(i<statCount && (uintptr_t(object.visspot)>=sizeof(spotvis) || object.tilex>=MAPSIZE || object.tiley>=MAPSIZE || object.itemnumber>bo_spear))return false;
    }
    word doors[MAXDOORS];doorobj_t objects[MAXDOORS];
    if(!take(doors,sizeof(doors),true) || !take(objects,sizeof(objects),true))return false;
    for(const auto &door:objects)if(door.tilex>=MAPSIZE || door.tiley>=MAPSIZE || door.action<dr_open || door.action>dr_closing || door.lock>dr_elevator)return false;
    word pushState,pushX,pushY,pushPosition;byte pushTile,pushDirection;
    if(!take(&pushState,sizeof(pushState),true) || !take(&pushTile,sizeof(pushTile),true) || !take(&pushX,sizeof(pushX),true) || !take(&pushY,sizeof(pushY),true) || !take(&pushDirection,sizeof(pushDirection),true) || !take(&pushPosition,sizeof(pushPosition),true))return false;
    if(pushX>=MAPSIZE || pushY>=MAPSIZE || pushDirection>di_west || pushPosition>63 || (pushState && (!pushX || !pushY || pushX>=MAPSIZE-1 || pushY>=MAPSIZE-1 || !pushTile || pushTile>=64)))return false;
    int32_t storedChecksum;int musicOffset;
    return take(&storedChecksum,sizeof(storedChecksum)) && take(&musicOffset,sizeof(musicOffset)) && storedChecksum==checksum && cursor==bytes.size();
}

boolean LoadTheGame(FILE *source,int x,int y)
{
    if(!source)return false;
    long start=ftell(source);
    if(start<0 || fseek(source,0,SEEK_END)!=0)return false;
    long end=ftell(source);
    if(end<start || end-start>1024*1024 || fseek(source,start,SEEK_SET)!=0)return false;
    std::vector<byte> snapshot;
    try { snapshot.resize(size_t(end-start)); }catch(const std::bad_alloc &){return false;}
    if(snapshot.empty() || fread(snapshot.data(),1,snapshot.size(),source)!=snapshot.size() || !NativeSaveIsValid(snapshot))return false;
    std::unique_ptr<FILE,decltype(&fclose)> stableFile(fmemopen(snapshot.data(),snapshot.size(),"rb"),&fclose);
    FILE *file=stableFile.get();
    if(!file)return false;
    int32_t checksum,oldchecksum;
    objtype nullobj;
    statobj_t nullstat;
    int actnum=0, i;

    checksum = 0;
    uint32_t nativeHeader[4];
    if(fread(nativeHeader,sizeof(nativeHeader),1,file)!=1 || nativeHeader[0]!=0x464c4f57 || nativeHeader[1]!=0x3143414d || nativeHeader[2]!=sizeof(objtype) || nativeHeader[3]!=sizeof(gamestate))return false;

#define fread2(a,b,c,d) if(!fread(a,b,c,d)){goto fail;}
    DiskFlopAnim(x,y);
    fread2 (&gamestate,sizeof(gamestate),1,file);
    checksum = DoChecksum((byte *)&gamestate,sizeof(gamestate),checksum);

    DiskFlopAnim(x,y);
    fread2 (&LevelRatios[0],sizeof(LRstruct)*LRpack,1,file);
    checksum = DoChecksum((byte *)&LevelRatios[0],sizeof(LRstruct)*LRpack,checksum);

    DiskFlopAnim(x,y);
    if(gamestate.episode<0 || gamestate.episode>5 || gamestate.mapon<0 || gamestate.mapon>9 || gamestate.weapon<wp_knife || gamestate.weapon>wp_chaingun)goto fail;
    SetupGameLevel ();

    DiskFlopAnim(x,y);
    fread2 (tilemap,sizeof(tilemap),1,file);
    checksum = DoChecksum((byte *)tilemap,sizeof(tilemap),checksum);

    DiskFlopAnim(x,y);

    for(i=0;i<MAPSIZE;i++)
    {
        for(int j=0;j<MAPSIZE;j++)
        {
            fread2 (&actnum,sizeof(word),1,file);
            checksum = DoChecksum((byte *) &actnum,sizeof(word),checksum);
            if(actnum&0x8000) {
                if((actnum&0x7fff)>=MAXACTORS)goto fail;
                actorat[i][j]=objlist+(actnum&0x7fff);
            }
            else
                actorat[i][j]=(objtype *)(uintptr_t) actnum;
        }
    }

    fread2 (areaconnect,sizeof(areaconnect),1,file);
    fread2 (areabyplayer,sizeof(areabyplayer),1,file);

    InitActorList ();
    DiskFlopAnim(x,y);
    fread2 (player,sizeof(*player),1,file);
    if ((size_t)player->state >= numstates) goto fail; // [FG] detect unsupported savegame format
    player->next=player->prev=NULL;
    player->state=&states[(size_t)player->state]; // [FG] statetype states[] array

    while (1)
    {
        DiskFlopAnim(x,y);
        fread2 (&nullobj,sizeof(nullobj),1,file);
        if (nullobj.active == ac_badobject)
            break;
        GetNewActor ();
        if ((size_t)nullobj.state >= numstates) goto fail; // [FG] detect unsupported savegame format
        nullobj.state=&states[(size_t)nullobj.state]; // [FG] statetype states[] array
        // don't copy over the links
        memcpy (newobj,&nullobj,offsetof(objtype,next));
    }

    DiskFlopAnim(x,y);
    word laststatobjnum;
    fread2 (&laststatobjnum,sizeof(laststatobjnum),1,file);
    if(laststatobjnum>MAXSTATS)goto fail;
    laststatobj=statobjlist+laststatobjnum;
    checksum = DoChecksum((byte *)&laststatobjnum,sizeof(laststatobjnum),checksum);

    DiskFlopAnim(x,y);
    for(i=0;i<MAXSTATS;i++)
    {
        fread2(&nullstat,sizeof(nullstat),1,file);
        checksum = DoChecksum((byte *)&nullstat,sizeof(nullstat),checksum);
        if(i<laststatobjnum && (uintptr_t)nullstat.visspot>=sizeof(spotvis))goto fail;
        nullstat.visspot=i<laststatobjnum?(byte *) ((uintptr_t)nullstat.visspot+(uintptr_t)spotvis):NULL;
        memcpy(statobjlist+i,&nullstat,sizeof(nullstat));
    }

    DiskFlopAnim(x,y);
    fread2 (doorposition,sizeof(doorposition),1,file);
    checksum = DoChecksum((byte *)doorposition,sizeof(doorposition),checksum);
    DiskFlopAnim(x,y);
    fread2 (doorobjlist,sizeof(doorobjlist),1,file);
    checksum = DoChecksum((byte *)doorobjlist,sizeof(doorobjlist),checksum);

    DiskFlopAnim(x,y);
    fread2 (&pwallstate,sizeof(pwallstate),1,file);
    checksum = DoChecksum((byte *)&pwallstate,sizeof(pwallstate),checksum);
    fread2 (&pwalltile,sizeof(pwalltile),1,file);
    checksum = DoChecksum((byte *)&pwalltile,sizeof(pwalltile),checksum);
    fread2 (&pwallx,sizeof(pwallx),1,file);
    checksum = DoChecksum((byte *)&pwallx,sizeof(pwallx),checksum);
    fread2 (&pwally,sizeof(pwally),1,file);
    checksum = DoChecksum((byte *)&pwally,sizeof(pwally),checksum);
    fread2 (&pwalldir,sizeof(pwalldir),1,file);
    checksum = DoChecksum((byte *)&pwalldir,sizeof(pwalldir),checksum);
    fread2 (&pwallpos,sizeof(pwallpos),1,file);
    checksum = DoChecksum((byte *)&pwallpos,sizeof(pwallpos),checksum);

    if (gamestate.secretcount)      // assign valid floorcodes under moved pushwalls
    {
        word *map, *obj; word tile, sprite;
        map = mapsegs[0]; obj = mapsegs[1];
        for (y=0;y<mapheight;y++)
            for (x=0;x<mapwidth;x++)
            {
                tile = *map++; sprite = *obj++;
                if (sprite == PUSHABLETILE && !tilemap[x][y]
                    && (tile < AREATILE || tile >= (AREATILE+NUMMAPS)))
                {
                    if (*map >= AREATILE)
                        tile = *map;
                    if (*(map-1-mapwidth) >= AREATILE)
                        tile = *(map-1-mapwidth);
                    if (*(map-1+mapwidth) >= AREATILE)
                        tile = *(map-1+mapwidth);
                    if ( *(map-2) >= AREATILE)
                        tile = *(map-2);

                    *(map-1) = tile; *(obj-1) = 0;
                }
            }
    }

    Thrust(0,0);    // set player->areanumber to the floortile you're standing on

    fread2 (&oldchecksum,sizeof(oldchecksum),1,file);

    fread2 (&lastgamemusicoffset,sizeof(lastgamemusicoffset),1,file);
    if(lastgamemusicoffset<0) lastgamemusicoffset=0;
#undef fread2


    if (oldchecksum != checksum)
    {
        Message(STR_SAVECHT1"\n"
                STR_SAVECHT2"\n"
                STR_SAVECHT3"\n"
                STR_SAVECHT4);

        IN_ClearKeysDown();
        IN_Ack();

        gamestate.oldscore = gamestate.score = 0;
        gamestate.lives = 1;
        gamestate.weapon =
            gamestate.chosenweapon =
            gamestate.bestweapon = wp_pistol;
        gamestate.ammo = 8;
    }

    Wolf_RenderReset();
    return true;

// [FG] detect unsupported savegame format
fail:
	Message("Unsupported savegame\nformat detected!");

	IN_ClearKeysDown();
	IN_Ack();

	memset(objlist, 0, sizeof(objlist));
	loadedgame = false;

	return false;
}

//===========================================================================

/*
==========================
=
= ShutdownId
=
= Shuts down all ID_?? managers
=
==========================
*/

void ShutdownId (void)
{
    US_Shutdown ();         // This line is completely useless...
    SD_Shutdown ();
    PM_Shutdown ();
    IN_Shutdown ();
    VW_Shutdown ();
    CA_Shutdown ();
}


//===========================================================================

/*
==================
=
= BuildTables
=
= Calculates:
=
= scale                 projection constant
= sintable/costable     overlapping fractional tables
=
==================
*/

const float radtoint = (float)(FINEANGLES/2/PI);

void BuildTables (void)
{
    //
    // calculate fine tangents
    //

    int i;
    for(i=0;i<FINEANGLES/8;i++)
    {
        double tang=tan((i+0.5)/radtoint);
        finetangent[i]=(int32_t)(tang*GLOBAL1);
        finetangent[FINEANGLES/4-1-i]=(int32_t)((1/tang)*GLOBAL1);
    }

    //
    // costable overlays sintable with a quarter phase shift
    // ANGLES is assumed to be divisable by four
    //

    float angle=0;
    float anglestep=(float)(PI/2/ANGLEQUAD);
    for(i=0; i<ANGLEQUAD; i++)
    {
        fixed value=(int32_t)(GLOBAL1*sin(angle));
        sintable[i]=sintable[i+ANGLES]=sintable[ANGLES/2-i]=value;
        sintable[ANGLES-i]=sintable[ANGLES/2+i]=-value;
        angle+=anglestep;
    }
    sintable[ANGLEQUAD] = 65536;
    sintable[3*ANGLEQUAD] = -65536;

#if defined(USE_STARSKY) || defined(USE_RAIN) || defined(USE_SNOW)
    Init3DPoints();
#endif
}

//===========================================================================


/*
====================
=
= CalcProjection
=
= Uses focallength
=
====================
*/

void CalcProjection (int32_t focal)
{
    int     i;
    int    intang;
    float   angle;
    double  tang;
    int     halfview;
    double  facedist;

    focallength = focal;
    facedist = focal+MINDIST;
    halfview = viewwidth/2;                                 // half view in pixels

    //
    // calculate scale value for vertical height calculations
    // and sprite x calculations
    //
    scale = (fixed) (halfview*facedist/(VIEWGLOBAL/2));

    //
    // divide heightnumerator by a posts distance to get the posts height for
    // the heightbuffer.  The pixel height is height>>2
    //
    heightnumerator = (TILEGLOBAL*scale)>>6;

    //
    // calculate the angle offset from view angle of each pixel's ray
    //

    for (i=0;i<halfview;i++)
    {
        // start 1/2 pixel over, so viewangle bisects two middle pixels
        tang = (int32_t)i*VIEWGLOBAL/viewwidth/facedist;
        angle = (float) atan(tang);
        intang = (int) (angle*radtoint);
        pixelangle[halfview-1-i] = intang;
        pixelangle[halfview+i] = -intang;
    }
}



//===========================================================================

/*
===================
=
= SetupWalls
=
= Map tile values to scaled pics
=
===================
*/

void SetupWalls (void)
{
    int     i;

    horizwall[0]=0;
    vertwall[0]=0;

    for (i=1;i<MAXWALLTILES;i++)
    {
        horizwall[i]=(i-1)*2;
        vertwall[i]=(i-1)*2+1;
    }
}

//===========================================================================

/*
=====================
=
= InitDigiMap
=
=====================
*/

// channel mapping:
//  -1: any non reserved channel
//   0: player weapons
//   1: boss weapons

static int wolfdigimap[] =
    {
        // These first sounds are in the upload version
#ifndef SPEAR
        HALTSND,                0,  -1,
        DOGBARKSND,             1,  -1,
        CLOSEDOORSND,           2,  -1,
        OPENDOORSND,            3,  -1,
        ATKMACHINEGUNSND,       4,   0,
        ATKPISTOLSND,           5,   0,
        ATKGATLINGSND,          6,   0,
        SCHUTZADSND,            7,  -1,
        GUTENTAGSND,            8,  -1,
        MUTTISND,               9,  -1,
        BOSSFIRESND,            10,  1,
        SSFIRESND,              11, -1,
        DEATHSCREAM1SND,        12, -1,
        DEATHSCREAM2SND,        13, -1,
        DEATHSCREAM3SND,        13, -1,
        TAKEDAMAGESND,          14, -1,
        PUSHWALLSND,            15, -1,

        LEBENSND,               20, -1,
        NAZIFIRESND,            21, -1,
        SLURPIESND,             22, -1,

        YEAHSND,                32, -1,

#ifndef UPLOAD
        // These are in all other episodes
        DOGDEATHSND,            16, -1,
        AHHHGSND,               17, -1,
        DIESND,                 18, -1,
        EVASND,                 19, -1,

        TOT_HUNDSND,            23, -1,
        MEINGOTTSND,            24, -1,
        SCHABBSHASND,           25, -1,
        HITLERHASND,            26, -1,
        SPIONSND,               27, -1,
        NEINSOVASSND,           28, -1,
        DOGATTACKSND,           29, -1,
        LEVELDONESND,           30, -1,
        MECHSTEPSND,            31, -1,

        SCHEISTSND,             33, -1,
        DEATHSCREAM4SND,        34, -1,         // AIIEEE
        DEATHSCREAM5SND,        35, -1,         // DEE-DEE
        DONNERSND,              36, -1,         // EPISODE 4 BOSS DIE
        EINESND,                37, -1,         // EPISODE 4 BOSS SIGHTING
        ERLAUBENSND,            38, -1,         // EPISODE 6 BOSS SIGHTING
        DEATHSCREAM6SND,        39, -1,         // FART
        DEATHSCREAM7SND,        40, -1,         // GASP
        DEATHSCREAM8SND,        41, -1,         // GUH-BOY!
        DEATHSCREAM9SND,        42, -1,         // AH GEEZ!
        KEINSND,                43, -1,         // EPISODE 5 BOSS SIGHTING
        MEINSND,                44, -1,         // EPISODE 6 BOSS DIE
        ROSESND,                45, -1,         // EPISODE 5 BOSS DIE

#endif
#else
//
// SPEAR OF DESTINY DIGISOUNDS
//
        HALTSND,                0,  -1,
        CLOSEDOORSND,           2,  -1,
        OPENDOORSND,            3,  -1,
        ATKMACHINEGUNSND,       4,   0,
        ATKPISTOLSND,           5,   0,
        ATKGATLINGSND,          6,   0,
        SCHUTZADSND,            7,  -1,
        BOSSFIRESND,            8,   1,
        SSFIRESND,              9,  -1,
        DEATHSCREAM1SND,        10, -1,
        DEATHSCREAM2SND,        11, -1,
        TAKEDAMAGESND,          12, -1,
        PUSHWALLSND,            13, -1,
        AHHHGSND,               15, -1,
        LEBENSND,               16, -1,
        NAZIFIRESND,            17, -1,
        SLURPIESND,             18, -1,
        LEVELDONESND,           22, -1,
        DEATHSCREAM4SND,        23, -1,         // AIIEEE
        DEATHSCREAM3SND,        23, -1,         // DOUBLY-MAPPED!!!
        DEATHSCREAM5SND,        24, -1,         // DEE-DEE
        DEATHSCREAM6SND,        25, -1,         // FART
        DEATHSCREAM7SND,        26, -1,         // GASP
        DEATHSCREAM8SND,        27, -1,         // GUH-BOY!
        DEATHSCREAM9SND,        28, -1,         // AH GEEZ!
        GETGATLINGSND,          38, -1,         // Got Gat replacement

#ifndef SPEARDEMO
        DOGBARKSND,             1,  -1,
        DOGDEATHSND,            14, -1,
        SPIONSND,               19, -1,
        NEINSOVASSND,           20, -1,
        DOGATTACKSND,           21, -1,
        TRANSSIGHTSND,          29, -1,         // Trans Sight
        TRANSDEATHSND,          30, -1,         // Trans Death
        WILHELMSIGHTSND,        31, -1,         // Wilhelm Sight
        WILHELMDEATHSND,        32, -1,         // Wilhelm Death
        UBERDEATHSND,           33, -1,         // Uber Death
        KNIGHTSIGHTSND,         34, -1,         // Death Knight Sight
        KNIGHTDEATHSND,         35, -1,         // Death Knight Death
        ANGELSIGHTSND,          36, -1,         // Angel Sight
        ANGELDEATHSND,          37, -1,         // Angel Death
        GETSPEARSND,            39, -1,         // Got Spear replacement
#endif
#endif
        LASTSOUND
    };


void InitDigiMap (void)
{
    int *map;

    for (map = wolfdigimap; *map != LASTSOUND; map += 3)
    {
        DigiMap[map[0]] = map[1];
        DigiChannel[map[1]] = map[2];
        SD_PrepareSound(map[1]);
    }
}

#ifndef SPEAR
CP_iteminfo MusicItems={CTL_X,CTL_Y,6,0,32};
CP_itemtype MusicMenu[]=
    {
        {1,"Get Them!",0},
        {1,"Searching",0},
        {1,"P.O.W.",0},
        {1,"Suspense",0},
        {1,"War March",0},
        {1,"Around The Corner!",0},

        {1,"Nazi Anthem",0},
        {1,"Lurking...",0},
        {1,"Going After Hitler",0},
        {1,"Pounding Headache",0},
        {1,"Into the Dungeons",0},
        {1,"Ultimate Conquest",0},

        {1,"Kill the S.O.B.",0},
        {1,"The Nazi Rap",0},
        {1,"Twelfth Hour",0},
        {1,"Zero Hour",0},
        {1,"Ultimate Conquest",0},
        {1,"Wolfpack",0}
    };
#else
CP_iteminfo MusicItems={CTL_X,CTL_Y-20,9,0,32};
CP_itemtype MusicMenu[]=
    {
        {1,"Funky Colonel Bill",0},
        {1,"Death To The Nazis",0},
        {1,"Tiptoeing Around",0},
        {1,"Is This THE END?",0},
        {1,"Evil Incarnate",0},
        {1,"Jazzin' Them Nazis",0},
        {1,"Puttin' It To The Enemy",0},
        {1,"The SS Gonna Get You",0},
        {1,"Towering Above",0}
    };
#endif

#ifndef SPEARDEMO
void DoJukebox(void)
{
    int which,lastsong=-1;
    unsigned start;
    unsigned songs[]=
        {
#ifndef SPEAR
            GETTHEM_MUS,
            SEARCHN_MUS,
            POW_MUS,
            SUSPENSE_MUS,
            WARMARCH_MUS,
            CORNER_MUS,

            NAZI_OMI_MUS,
            PREGNANT_MUS,
            GOINGAFT_MUS,
            HEADACHE_MUS,
            DUNGEON_MUS,
            ULTIMATE_MUS,

            INTROCW3_MUS,
            NAZI_RAP_MUS,
            TWELFTH_MUS,
            ZEROHOUR_MUS,
            ULTIMATE_MUS,
            PACMAN_MUS
#else
            XFUNKIE_MUS,             // 0
            XDEATH_MUS,              // 2
            XTIPTOE_MUS,             // 4
            XTHEEND_MUS,             // 7
            XEVIL_MUS,               // 17
            XJAZNAZI_MUS,            // 18
            XPUTIT_MUS,              // 21
            XGETYOU_MUS,             // 22
            XTOWER2_MUS              // 23
#endif
        };

    IN_ClearKeysDown();
    if (!AdLibPresent && !SoundBlasterPresent)
        return;

    MenuFadeOut();

#ifndef SPEAR
#ifndef UPLOAD
    start = ((Wolf_GetTicks()/10)%3)*6;
#else
    start = 0;
#endif
#else
    start = 0;
#endif

    CA_CacheGrChunk (STARTFONT+1);
#ifdef SPEAR
    CacheLump (BACKDROP_LUMP_START,BACKDROP_LUMP_END);
#else
    CacheLump (CONTROLS_LUMP_START,CONTROLS_LUMP_END);
#endif
    CA_LoadAllSounds ();

    fontnumber=1;
    ClearMScreen ();
    VWB_DrawPic(112,184,C_MOUSELBACKPIC);
    DrawStripes (10);
    SETFONTCOLOR (TEXTCOLOR,BKGDCOLOR);

#ifndef SPEAR
    DrawWindow (CTL_X-2,CTL_Y-6,280,13*7,BKGDCOLOR);
#else
    DrawWindow (CTL_X-2,CTL_Y-26,280,13*10,BKGDCOLOR);
#endif

    DrawMenu (&MusicItems,&MusicMenu[start]);

    SETFONTCOLOR (READHCOLOR,BKGDCOLOR);
    PrintY=15;
    WindowX = 0;
    WindowY = 320;
    US_CPrint ("Robert's Jukebox");

    SETFONTCOLOR (TEXTCOLOR,BKGDCOLOR);
    VW_UpdateScreen();
    MenuFadeIn();

    do
    {
        which = HandleMenu(&MusicItems,&MusicMenu[start],NULL);
        if (which>=0)
        {
            if (lastsong >= 0)
                MusicMenu[start+lastsong].active = 1;

            StartCPMusic(songs[start + which]);
            MusicMenu[start+which].active = 2;
            DrawMenu (&MusicItems,&MusicMenu[start]);
            VW_UpdateScreen();
            lastsong = which;
        }
    } while(which>=0);

    MenuFadeOut();
    IN_ClearKeysDown();
#ifdef SPEAR
    UnCacheLump (BACKDROP_LUMP_START,BACKDROP_LUMP_END);
#else
    UnCacheLump (CONTROLS_LUMP_START,CONTROLS_LUMP_END);
#endif
}
#endif

/*
==========================
=
= InitGame
=
= Load a few things right away
=
==========================
*/

static void InitGame()
{
    // initialize SDL
    if(Wolf_Init(Wolf_INIT_VIDEO | Wolf_INIT_AUDIO | Wolf_INIT_JOYSTICK) < 0)
    {
        printf("Unable to init SDL: %s\n", Wolf_GetError());
        exit(1);
    }
    atexit(Wolf_Quit);

    int numJoysticks = Wolf_NumJoysticks();
    if(param_joystickindex && (param_joystickindex < -1 || param_joystickindex >= numJoysticks))
    {
        if(!numJoysticks)
            printf("No joysticks are available to SDL!\n");
        else
            printf("The joystick index must be between -1 and %i!\n", numJoysticks - 1);
        exit(1);
    }

    // Native (2026-10-10): no signon or system-config loading screen; the
    // window stays black until the menu or the game is ready to show.
    VL_SetVGAPlaneMode ();

#if defined _WIN32
    if(!fullscreen)
    {
        struct Wolf_SysWMinfo wmInfo;
        Wolf_VERSION(&wmInfo.version);

        if(Wolf_GetWindowWMInfo(window,&wmInfo) != -1)
        {
            HWND hwndSDL = wmInfo.info.win.window;
            DWORD style = GetWindowLong(hwndSDL, GWL_STYLE) & ~WS_SYSMENU;
            SetWindowLong(hwndSDL, GWL_STYLE, style);
            SetWindowPos(hwndSDL, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
#endif

    VH_Startup ();
    IN_Startup ();
    PM_Startup ();
    SD_Startup ();
    CA_Startup ();
    US_Startup ();

    // TODO: Will any memory checking be needed someday??
#ifdef NOTYET
#ifndef SPEAR
    if (mminfo.mainmem < 235000L)
#else
    if (mminfo.mainmem < 257000L && !MS_CheckParm("debugmode"))
#endif
    {
        byte *screen;

        CA_CacheGrChunk (ERRORSCREEN);
        screen = grsegs[ERRORSCREEN];
        ShutdownId();
/*        memcpy((byte *)0xb8000,screen+7+7*160,17*160);
        gotoxy (1,23);*/
        exit(1);
    }
#endif


//
// build some tables
//
    InitDigiMap ();

    ReadConfig ();

    SetupSaveGames();

//
// HOLDING DOWN 'M' KEY?
//
	IN_ProcessEvents();

#ifndef SPEARDEMO
    if (Keyboard[sc_M])
        DoJukebox();
#endif

//
// load in and lock down some basic chunks
//

    CA_CacheGrChunk(STARTFONT);
    CA_CacheGrChunk(STATUSBARPIC);

    LoadLatchMem ();
    BuildTables ();          // trig tables
    SetupWalls ();

    viewsize = 21;      // native: always the full-screen view (the status bar is a card)
    NewViewSize (viewsize);

//
// initialize variables
//
    InitRedShifts ();

#ifdef NOTYET
    vdisp = (byte *) (0xa0000+PAGE1START);
    vbuf = (byte *) (0xa0000+PAGE2START);
#endif
}

//===========================================================================

/*
==========================
=
= SetViewSize
=
==========================
*/

boolean SetViewSize (unsigned width, unsigned height)
{
    viewwidth = width&~1;                  // native backing pixels; only paired rays require an even width
    viewheight = height&~1;                 // must be even
    centerx = viewwidth/2-1;
    shootdelta = viewwidth/10;
    if((unsigned) viewheight == screenHeight)
        viewscreenx = viewscreeny = screenofs = 0;
    else
    {
        viewscreenx = (screenWidth-viewwidth) / 2;
        viewscreeny = (screenHeight-scaleFactor*STATUSLINES-viewheight)/2;
        screenofs = viewscreeny*screenWidth+viewscreenx;
    }

//
// calculate trace angles and projection constants
//
    CalcProjection (FOCALLENGTH);

    return true;
}


void ShowViewSize (int width)
{
    int oldwidth,oldheight;

    oldwidth = viewwidth;
    oldheight = viewheight;

    if(width == 21)
    {
        viewwidth = screenWidth;
        viewheight = screenHeight;
        VWB_BarScaledCoord (0, 0, screenWidth, screenHeight, 0);
    }
    else if(width == 20)
    {
        viewwidth = screenWidth;
        viewheight = screenHeight - scaleFactor*STATUSLINES;
        DrawPlayBorder ();
    }
    else
    {
        viewwidth = width*16*screenWidth/320;
        viewheight = (int) (width*16*HEIGHTRATIO*screenHeight/200);
        DrawPlayBorder ();
    }

    viewwidth = oldwidth;
    viewheight = oldheight;
}


void NewViewSize (int width)
{
    viewsize = width;
    if(viewsize == 21)
        SetViewSize(screenWidth, screenHeight);
    else if(viewsize == 20)
        SetViewSize(screenWidth, screenHeight - scaleFactor * STATUSLINES);
    else
        SetViewSize(width*16*screenWidth/320, (unsigned) (width*16*HEIGHTRATIO*screenHeight/200));
}



//===========================================================================

/*
==========================
=
= Quit
=
==========================
*/

void Quit (const char *errorStr, ...)
{
#ifdef NOTYET
    byte *screen;
#endif
    char error[256];
    if(errorStr != NULL)
    {
        va_list vlist;
        va_start(vlist, errorStr);
        vsnprintf(error, sizeof(error), errorStr, vlist);
        va_end(vlist);
    }
    else error[0] = 0;

    if (!pictable)  // don't try to display the red box before it's loaded
    {
        ShutdownId();
        if (*error)
        {
#ifdef NOTYET
            SetTextCursor(0,0);
#endif
            puts(error);
#ifdef NOTYET
            SetTextCursor(0,2);
#endif
            VW_WaitVBL(100);
        }
        exit(1);
    }

    if (!*error)
    {
#ifdef NOTYET
        CA_CacheGrChunk (ORDERSCREEN);
        screen = grsegs[ORDERSCREEN];
#endif
        WriteConfig ();
    }
#ifdef NOTYET
    else
    {
        CA_CacheGrChunk (ERRORSCREEN);
        screen = grsegs[ERRORSCREEN];
    }
#endif

    ShutdownId ();

    if (*error)
    {
#ifdef NOTYET
        memcpy((byte *)0xb8000,screen+7,7*160);
        SetTextCursor(9,3);
#endif
        puts(error);
#ifdef NOTYET
        SetTextCursor(0,7);
#endif
        VW_WaitVBL(200);
        exit(1);
    }
    else
    if (!*error)
    {
#ifdef NOTYET
        memcpy((byte *)0xb8000,screen+7,24*160); // 24 for SPEAR/UPLOAD compatibility
        SetTextCursor(0,23);
#endif
    }

    exit(wolfExitStatus);
}

//===========================================================================



/*
=====================
=
= DemoLoop
=
=====================
*/


static void DemoLoop()
{
    int LastDemo = 0;

//
// check for launch from ted
//
    if (param_tedlevel != -1)
    {
        param_nowait = true;
        EnableEndGameMenuItem();
        NewGame(param_difficulty,0);

#ifndef SPEAR
        gamestate.episode = param_tedlevel/10;
        gamestate.mapon = param_tedlevel%10;
#else
        gamestate.episode = 0;
        gamestate.mapon = param_tedlevel;
#endif
        GameLoop();
        Quit (NULL);
    }


//
// main game cycle
//

if (!param_demotest)
{

    StartCPMusic(INTROSONG);

    if (!param_nowait)
        PG13 ();

}

    while (1)
    {
        while (!param_nowait)
        {
//
// title page
//
if (!param_demotest)
{

#ifdef SPEAR
            Wolf_Color pal[256];
            CA_CacheGrChunk (TITLEPALETTE);
            VL_ConvertPalette(grsegs[TITLEPALETTE], pal, 256);

            CA_CacheGrChunk (TITLE1PIC);
            VWB_DrawPic (0,0,TITLE1PIC);
            UNCACHEGRCHUNK (TITLE1PIC);

            CA_CacheGrChunk (TITLE2PIC);
            VWB_DrawPic (0,80,TITLE2PIC);
            UNCACHEGRCHUNK (TITLE2PIC);
            VW_UpdateScreen ();
            VL_FadeIn(0,255,pal,30);

            UNCACHEGRCHUNK (TITLEPALETTE);
#else
            CA_CacheScreen (TITLEPIC);
            VW_UpdateScreen ();
            VW_FadeIn();
#endif
            if (IN_UserInput(TickBase*15))
                break;
            VW_FadeOut();
//
// credits page
//
            CA_CacheScreen (CREDITSPIC);
            VW_UpdateScreen();
            VW_FadeIn ();
            if (IN_UserInput(TickBase*10))
                break;
            VW_FadeOut ();
//
// high scores
//
            DrawHighScores ();
            VW_UpdateScreen ();
            VW_FadeIn ();

            if (IN_UserInput(TickBase*10))
                break;
}
//
// demo
//

            #ifndef SPEARDEMO
            PlayDemo (LastDemo++%4);
            #else
            PlayDemo (LastDemo);
            #endif

            if (playstate == ex_abort)
                break;
            VW_FadeOut();
            if(screenHeight % 200 != 0)
                VL_ClearScreen(0);
            StartCPMusic(INTROSONG);
        }

        VW_FadeOut ();

#ifdef DEBUGKEYS
        if (Keyboard[sc_Tab] && param_debugmode)
            RecordDemo ();
        else
            US_ControlPanel (0);
#else
        US_ControlPanel (0);
#endif

        if (startgame || loadedgame)
        {
            GameLoop ();
            if(!param_nowait)
            {
                VW_FadeOut();
                StartCPMusic(INTROSONG);
            }
        }
    }
}


//===========================================================================

#define IFARG(str) if(!strcmp(arg, (str)))

void CheckParameters(int argc, char *argv[])
{
    bool hasError = false, showHelp = false;
    int defaultSampleRate = param_samplerate;

    for(int i = 1; i < argc; i++)
    {
        char *arg = argv[i];
#ifndef SPEAR
        IFARG("--goobers")
#else
        IFARG("--debugmode")
#endif
            param_debugmode = true;
        else IFARG("--baby")
            param_difficulty = 0;
        else IFARG("--easy")
            param_difficulty = 1;
        else IFARG("--normal")
            param_difficulty = 2;
        else IFARG("--hard")
            param_difficulty = 3;
        else IFARG("--nowait")
            param_nowait = true;
        else IFARG("--demotest")
            param_demotest = true;
        else IFARG("--tedlevel")
        {
            if(++i >= argc)
            {
                printf("The tedlevel option is missing the level argument!\n");
                hasError = true;
            }
            else param_tedlevel = atoi(argv[i]);
        }
        else IFARG("--fullscreen")
            fullscreen = true;
        else IFARG("--nograbmouse")
            grabmouse = false;
        else IFARG("--res")
        {
            if(i + 2 >= argc)
            {
                printf("The res option needs the width and/or the height argument!\n");
                hasError = true;
            }
            else
            {
                screenWidth = atoi(argv[++i]);
                screenHeight = atoi(argv[++i]);
                unsigned factor = screenWidth / 320;
                if(screenWidth % 320 || screenHeight != 200 * factor)
                    printf("Screen size must be a multiple of 320x200!\n"), hasError = true;
                aspect = true;
            }
        }
        else IFARG("--resf")
        {
            if(i + 2 >= argc)
            {
                printf("The resf option needs the width and/or the height argument!\n");
                hasError = true;
            }
            else
            {
                screenWidth = atoi(argv[++i]);
                screenHeight = atoi(argv[++i]);
                if(screenWidth < 320)
                    printf("Screen width must be at least 320!\n"), hasError = true;
                if(screenHeight < 200)
                    printf("Screen height must be at least 200!\n"), hasError = true;
                aspect = false;
            }
        }
        else IFARG("--joystick")
        {
            if(++i >= argc)
            {
                printf("The joystick option is missing the index argument!\n");
                hasError = true;
            }
            else param_joystickindex = atoi(argv[i]);   // index is checked in InitGame
        }
        else IFARG("--joystickhat")
        {
            if(++i >= argc)
            {
                printf("The joystickhat option is missing the index argument!\n");
                hasError = true;
            }
            else param_joystickhat = atoi(argv[i]);
        }
        else IFARG("--samplerate")
        {
            if(++i >= argc)
            {
                printf("The samplerate option is missing the rate argument!\n");
                hasError = true;
            }
            else param_samplerate = atoi(argv[i]);
        }
        else IFARG("--audiobuffer")
        {
            if(++i >= argc)
            {
                printf("The audiobuffer option is missing the size argument!\n");
                hasError = true;
            }
            else param_audiobuffer = atoi(argv[i]);
        }
        else IFARG("--mission")
        {
            if(++i >= argc)
            {
                printf("The mission option is missing the mission argument!\n");
                hasError = true;
            }
            else
            {
                param_mission = atoi(argv[i]);
                if(param_mission < 0 || param_mission > 3)
                {
                    printf("The mission option must be between 0 and 3!\n");
                    hasError = true;
                }
            }
        }
        else IFARG("--configdir")
        {
            if(++i >= argc)
            {
                printf("The configdir option is missing the dir argument!\n");
                hasError = true;
            }
            else
            {
                size_t len = strlen(argv[i]);
                if(len + 2 > sizeof(configdir))
                {
                    printf("The config directory is too long!\n");
                    hasError = true;
                }
                else
                {
                    strcpy(configdir, argv[i]);
                    if(argv[i][len] != '/' && argv[i][len] != '\\')
                        strcat(configdir, "/");
                }
            }
        }
        else IFARG("--ignorenumchunks")
            param_ignorenumchunks = true;
        else IFARG("--help")
            showHelp = true;
        else hasError = true;
    }
    if(hasError || showHelp)
    {
        if(hasError) printf("\n");
        printf(
            "Wolf4SDL v1.7 ($Revision: 257 $)\n"
            "Ported by Chaos-Software (http://www.chaos-software.de.vu)\n"
            "Original Wolfenstein 3D by id Software\n\n"
            "Usage: Wolf4SDL [options]\n"
            "Options:\n"
            " --help                 This help page\n"
            " --tedlevel <level>     Starts the game in the given level\n"
            " --baby                 Sets the difficulty to baby for tedlevel\n"
            " --easy                 Sets the difficulty to easy for tedlevel\n"
            " --normal               Sets the difficulty to normal for tedlevel\n"
            " --hard                 Sets the difficulty to hard for tedlevel\n"
            " --nowait               Skips intro screens\n"
            " --demotest             Skips right into the demo loop\n"
            " --fullscreen           Starts the game in fullscreen mode\n"
            " --nograbmouse          Does not grab the mouse in windowed mode\n"
            " --res <width> <height> Sets the screen resolution\n"
            "                        (must be multiple of 320x200)\n"
            " --resf <w> <h>         Sets any screen resolution >= 320x200\n"
            "                        (which may result in graphic errors)\n"
            " --joystick <index>     Use the index-th joystick if available\n"
            "                        (-1 to disable joystick, default: 0)\n"
            " --joystickhat <index>  Enables movement with the given coolie hat\n"
            " --samplerate <rate>    Sets the sound sample rate (given in Hz, default: %i)\n"
            " --audiobuffer <size>   Sets the size of the audio buffer (-> sound latency)\n"
            "                        (given in bytes, default: 2048 / (44100 / samplerate))\n"
            " --ignorenumchunks      Ignores the number of chunks in VGAHEAD.*\n"
            "                        (may be useful for some broken mods)\n"
            " --configdir <dir>      Directory where config file and save games are stored\n"
#if defined(_WIN32)
            "                        (default: current directory)\n"
#else
            "                        (default: $HOME/.wolf4sdl)\n"
#endif
#if defined(SPEAR) && !defined(SPEARDEMO)
            " --mission <mission>    Mission number to play (0-3)\n"
            "                        (default: 0 -> .sod, 1-3 -> .sd*)\n"
#endif
            , defaultSampleRate
        );
        exit(1);
    }
}

/*
==========================
=
= main
=
==========================
*/

int Wolf_GameMain (int argc, char *argv[])
{
    CheckParameters(argc, argv);

    CheckForEpisodes();

    InitGame();

    DemoLoop();

    Quit("Demo loop exited???");
    return 1;
}
