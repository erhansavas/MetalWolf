// SPDX-License-Identifier: GPL-2.0-only
// Presentation copies; the original actors, AI visibility and pickups remain
// authoritative. Rendering additional display frames never runs a game step.
#include "wl_def.h"
#include "PresentationMetrics.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <climits>
bool wolfPresentationOnly=false;
namespace {
struct Pose { fixed x,y;int angle,health;statetype *state; };
struct Snapshot {
    std::array<Pose,MAXACTORS> poses{};
    std::array<word,MAXDOORS> doors{};
    word push=0;int pushX=0,pushY=0,map=-1,episode=-1;
    double view=0;
    bool valid=false;
};
Snapshot prior,current;
std::array<objtype,MAXACTORS> copies;
objtype *presentationPlayer;
byte presentationVisibility[MAPSIZE][MAPSIZE];
std::array<word,MAXDOORS> presentationDoors;
word presentationPush;
double presentationView;
bool waiting=false,drawing=false;
double presentedAt;
int index(const objtype *p) {
    auto address=reinterpret_cast<uintptr_t>(p),base=reinterpret_cast<uintptr_t>(objlist);
    if(!p || address<base || address>=base+sizeof(objlist) || (address-base)%sizeof(objtype))return -1;
    return int((address-base)/sizeof(objtype));
}
void capture(Snapshot &s) {
    for(int i=0;i<MAXACTORS;++i)s.poses[i]={objlist[i].x,objlist[i].y,objlist[i].angle,objlist[i].hitpoints,objlist[i].state};
    std::copy_n(doorposition,MAXDOORS,s.doors.begin());s.push=pwallpos;s.pushX=pwallx;s.pushY=pwally;
    s.map=gamestate.mapon;s.episode=gamestate.episode;s.valid=true;
    // The player's turn in whole degrees plus the fraction the original
    // keeps in anglefrac (1/20 degree units, wl_agent.cpp ANGLESCALE).
    s.view=index(player)>=0?player->angle-anglefrac/20.0:0;
}
objtype *copy(const objtype *p) { int i=index(p);return i<0?nullptr:&copies[i]; }
fixed blend(fixed a,fixed b,double alpha) { return fixed(int64_t(a)+std::llround((int64_t(b)-a)*alpha)); }
void present() {
    if(drawing || !current.valid || !Wolf_RenderEnabled() || current.map!=gamestate.mapon || current.episode!=gamestate.episode || index(player)<0)return;
    drawing=true;
    Wolf_NativeAwaitFrame();
    bool adjacent=prior.valid && prior.map==current.map && prior.episode==current.episode;
    // Display a constant one tic behind real time. prior/current span `tics`
    // (lasttimecount-tics .. lasttimecount); a latency equal to the last step's
    // size made two-tic steps freeze for a frame and then jump.
    const int span=std::max(1,int(tics));
    double alpha=adjacent?std::clamp((double(Wolf_GetTicks())*.07-lasttimecount-1+span)/span,0.0,1.0):1;
    std::copy_n(objlist,MAXACTORS,copies.begin());
    for(int i=0;i<MAXACTORS;++i) {
        auto &actor=copies[i];actor.next=copy(actor.next);actor.prev=copy(actor.prev);
        const auto &a=prior.poses[i],&b=current.poses[i];
        // Dead/new actors and large jumps are discontinuities.
        if(adjacent && a.state && b.state && (a.health>0)==(b.health>0) && std::llabs(int64_t(b.x)-a.x)<TILEGLOBAL && std::llabs(int64_t(b.y)-a.y)<TILEGLOBAL) {
            actor.x=blend(a.x,b.x,alpha);actor.y=blend(a.y,b.y,alpha);
            int delta=(b.angle-a.angle+ANGLES+ANGLES/2)%ANGLES-ANGLES/2;
            actor.angle=(a.angle+int(std::lround(delta*alpha))+ANGLES)%ANGLES;
        }
        int32_t joltX,joltY; Wolf_GoreFlinch(&objlist[i],&joltX,&joltY);
        actor.x+=joltX; actor.y+=joltY;
    }
    presentationPlayer=copy(player);
    // The view turns through the fraction of a degree too: in whole degrees a
    // slow stick turn moved the picture in visible 1 degree steps.
    double view=current.view;
    if(adjacent) { double delta=std::remainder(current.view-prior.view,double(ANGLES)); if(std::fabs(delta)<45) view=prior.view+delta*alpha; }
    presentationView=view;
    std::copy_n(doorposition,MAXDOORS,presentationDoors.begin());presentationPush=pwallpos;
    if(adjacent) {
        for(int i=0;i<MAXDOORS;++i)presentationDoors[i]=word(blend(prior.doors[i],current.doors[i],alpha));
        if(prior.pushX==current.pushX && prior.pushY==current.pushY)presentationPush=word(blend(prior.push,current.push,alpha));
    }
    Native_MetricsRenderBegin();
    wolfPresentationOnly=true;ThreeDRefresh();wolfPresentationOnly=false;
    presentedAt=Wolf_GetTicks();drawing=false;
}
}
void *Wolf_RenderPlayer() { return wolfPresentationOnly?presentationPlayer:player; }
bool Wolf_RenderViewAngle(double *degrees) { if(wolfPresentationOnly)*degrees=presentationView; return wolfPresentationOnly; }
byte (*Wolf_RenderVisibility())[MAPSIZE] { return wolfPresentationOnly?presentationVisibility:spotvis; }
bool Wolf_RenderStaticVisible(const byte *pointer) {
    if(!pointer)return false;
    if(!wolfPresentationOnly)return *pointer!=0;
    auto address=reinterpret_cast<uintptr_t>(pointer),base=reinterpret_cast<uintptr_t>(spotvis);
    return address>=base && address-base<sizeof(spotvis) && reinterpret_cast<byte *>(presentationVisibility)[address-base]!=0;
}
word Wolf_RenderDoorPosition(unsigned index) { return wolfPresentationOnly?presentationDoors[index]:doorposition[index]; }
word Wolf_RenderPushPosition() { return wolfPresentationOnly?presentationPush:pwallpos; }
bool Wolf_RenderEnabled() { return ingame && !menuactive && !Paused && !demoplayback && !demorecord && !gamestate.victoryflag ; }
bool Wolf_RenderUnseen() { return !wolfPresentationOnly && !fizzlein && Wolf_RenderEnabled(); }
void Wolf_RenderReset() { prior.valid=current.valid=false;waiting=drawing=false; }
// PollControls can wait and present between these calls. Keep the completed
// pair intact throughout that wait, then advance it once the next step exists.
void Wolf_RenderBeforeStep() { if(!Wolf_RenderEnabled())prior.valid=current.valid=false; }
void Wolf_RenderAfterStep() { if(Wolf_RenderEnabled()){prior=current;capture(current);present();}else prior.valid=current.valid=false; }
void Wolf_RenderWaiting(bool value) { waiting=value; }
void Wolf_RenderIdle() { if(waiting && Wolf_RenderEnabled() && double(Wolf_GetTicks())-presentedAt>=Wolf_NativePresentInterval()-1)present(); }
