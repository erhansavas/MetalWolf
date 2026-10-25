// SPDX-License-Identifier: GPL-2.0-only
// Cosmetic Wolf blood. Drops fly ballistically through the tile map, stain
// the exact wall faces and floor they hit, run down walls and pool under
// corpses. Everything here is presentation: it reads actors and the map,
// uses its own RNG and real time, and never writes game state.
#include "wl_def.h"
#include "GoreMasks.hpp"
#include "GoreSound.hpp"
#include "Parallel.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <tuple>
#include <unordered_map>
#include <vector>

extern fixed viewx,viewy,viewsin,viewcos;
extern int *wallheight;
extern Wolf_Color gamepal[];

namespace {
using gore::MaskSize;
constexpr double Step=1/70.0;
// A wall is ~1.9 m tall, so real gravity is ~5.2 tiles/s^2.
constexpr double Gravity=5.2;

struct Drop { double x=0,y=0,z=0,vx=0,vy=0,vz=0,size=0,age=0,life=0; uint8_t tone=0; bool active=false; };
// Floor stain: (c,s) major axis, stretch along it; grow is the spreading
// time constant in seconds for pools (0: instant splat).
// print: 0, or 1 + gore::PrintKind for a footprint (left mirrors the sole);
// load is the blood the sole still carried when it printed.
struct Floor { double x=0,y=0,radius=0,c=1,s=0,stretch=1,born=0,grow=0; uint8_t shape=0,tone=0; bool pool=false,active=false; uint8_t print=0; bool left=false; double load=1; };
// Wall stain on one face: key names the face, u runs along it in tiles.
struct Wall { double u=0,z=0,radius=0,drip=0,born=0; int key=-1,next=-1; uint8_t shape=0,tone=0,landed=0; bool active=false; };
struct PendingPool { double x=0,y=0,radius=0,at=0; };
double spurtHeard=-1;
// A killed body's arterial jet: (dx,dy) the shot's exit direction.
struct Spurt { double x=0,y=0,dx=0,dy=0,wound=0,born=0,beat=0; };

// Tissue and bone fragments: bullets tear tissue and splinter bone; they do
// not sever limbs, and the sprite corpse stays whole. Flesh is soft (little
// bounce, high friction), bone is hard and skitters.
struct Chunk { double x=0,y=0,z=0,vx=0,vy=0,vz=0,size=0,trail=0; uint8_t tone=0; bool bone=false,resting=false,active=false; };

std::array<Drop,2560> drops;
std::array<Chunk,256> chunks;
unsigned chunkCursor=0;
std::array<Floor,768> floors;
std::array<Wall,512> walls;
std::vector<PendingPool> pools;
std::vector<Spurt> spurts;
// Footprints and hit reactions: walking, wet soles and a damped flinch, in
// tiles and seconds.
struct Walker { double x=0,y=0,travelled=0,speed=0,standing=0; float load[2]={0,0}; uint8_t foot=0; int obclass=-1; bool seen=false; };
std::unordered_map<const objtype *,Walker> walkers;
struct Impulse { double start=0,dx=0,dy=0,amplitude=0; };
struct Flinch { std::array<Impulse,4> hits{}; unsigned next=0; int obclass=-1; };
std::unordered_map<const objtype *,Flinch> flinches;
double lastWalk=-1;
// Face key: orientation (vertical x-plane 0, horizontal y-plane 1), facing
// (ray travelling + or -), plane line 0..64 and the tile along the face.
constexpr int FaceKeys=2*2*65*64;
std::array<int,FaceKeys> faceHead;
unsigned dropCursor=0,floorCursor=0,wallCursor=0;
uint32_t seed=0x9e3779b9u;
double lastTime=-1,accumulated=0;
int levelMap=-1,levelEpisode=-1;
std::vector<int> columnKey;
std::vector<double> columnU;

double random01() { seed=seed*1664525u+1013904223u; return (seed>>8)/16777216.0; }
double range(double low,double high) { return low+(high-low)*random01(); }
double now() { return Wolf_GetTicks()/1000.0; }

bool solid(int x,int y) {
    if(x<0 || y<0 || x>=MAPSIZE || y>=MAPSIZE) return true;
    byte tile=tilemap[x][y];
    return tile && !(tile&0x80) && tile!=64;
}
int faceKey(int orientation,int step,int line,int along) {
    if(line<0 || line>64 || along<0 || along>=64) return -1;
    return ((orientation*2+(step>0))*65+line)*64+along;
}
// Nearest solid tile edge, so a stain never spreads under or through a wall.
double clearance(double x,double y,double radius) {
    int x0=int(std::floor(x-radius)),x1=int(std::floor(x+radius)),y0=int(std::floor(y-radius)),y1=int(std::floor(y+radius));
    for(int tx=x0;tx<=x1;++tx) for(int ty=y0;ty<=y1;++ty) if(solid(tx,ty)) {
        double dx=std::max({tx-x,0.0,x-(tx+1)}),dy=std::max({ty-y,0.0,y-(ty+1)});
        radius=std::min(radius,std::hypot(dx,dy));
    }
    return radius;
}

// Palette ramps found once by nearest color: wet blood from a lighter thin
// film to near black-red, and the brown crust old thin films dry into.
struct Ramps { uint8_t wet[9],dry[4]; };
const Ramps &ramps() {
    static Ramps r=[] {
        static const int wet[9][3]={{150,42,40},{138,32,30},{124,24,22},{110,16,14},{96,10,8},{82,6,5},{68,4,4},{56,2,2},{44,0,0}};
        static const int dry[4][3]={{88,40,26},{74,32,20},{60,26,16},{46,20,12}};
        auto nearest=[](const int *c) {
            int best=0,error=INT_MAX;
            for(int i=0;i<256;++i) {
                int dr=gamepal[i].r-c[0],dg=gamepal[i].g-c[1],db=gamepal[i].b-c[2],d=2*dr*dr+4*dg*dg+3*db*db;
                if(d<error) { best=i; error=d; }
            }
            return uint8_t(best);
        };
        Ramps out{};
        for(int i=0;i<9;++i) out.wet[i]=nearest(wet[i]);
        for(int i=0;i<4;++i) out.dry[i]=nearest(dry[i]);
        return out;
    }();
    return r;
}
// Color for a film thickness (1..255) at an age in seconds: thin films dry
// within ~20 s, thick pools stay wet for over a minute.
uint8_t shade(int density,double age,int tone) {
    const Ramps &r=ramps();
    int thick=density>200?4:density>150?3:density>90?2:density>40?1:0;
    double dryAt=20+70*density/255.0;
    int dried=age<dryAt*.5?0:age<dryAt?1:age<dryAt*3?2:3;
    if(dried==3 && thick<2) return r.dry[std::min(3,tone+thick)];
    return r.wet[std::clamp(tone+thick+dried,0,8)];
}

void addFloor(double x,double y,double radius,bool pool,double dx,double dy,double stretch,double grow) {
    if(solid(int(x),int(y))) return;
    double room=clearance(x,y,radius*stretch);
    if(room<radius*stretch) { stretch=std::max(1.0,room/radius); radius=std::min(radius,room); }
    if(radius<.004) return;
    Floor &f=floors[floorCursor++%floors.size()];
    double angle=std::hypot(dx,dy)>1e-6?std::atan2(dy,dx):range(0,6.2832);
    f={x,y,radius,std::cos(angle),std::sin(angle),stretch,now(),grow,uint8_t(random01()*(pool?gore::PoolShapes:gore::SplatShapes)),
       uint8_t(random01()*3),pool,true};
}
void unlinkWall(int index) {
    Wall &w=walls[index];
    if(w.key<0) return;
    for(int *link=&faceHead[w.key];*link>=0;link=&walls[*link].next) if(*link==index) { *link=w.next; break; }
}
void addWall(int key,double u,double z,double radius,double drip) {
    if(key<0 || z<=0 || z>=1) return;
    for(int i=faceHead[key];i>=0;i=walls[i].next) {
        Wall &e=walls[i];
        if(std::fabs(e.u-u)+radius*.5<e.radius && std::fabs(e.z-z)+radius*.5<e.radius) {
            e.radius=std::min(e.radius+radius*.1,.4);
            if(drip>e.drip) { e.drip=drip; e.landed=0; }
            return;
        }
    }
    int index=int(wallCursor++%walls.size());
    if(walls[index].active) unlinkWall(index);
    walls[index]={u,z,radius,drip,now(),key,faceHead[key],uint8_t(random01()*gore::SplatShapes),uint8_t(random01()*3),0,true};
    faceHead[key]=index;
}

// Rivulets: delayed start, viscous slowdown.
struct Rivulet { double offset,width,length,delay,tau; };
int rivulets(const Wall &w) { return w.drip<=0?0:1+(w.drip>.12)+(w.drip>.2); }
Rivulet rivulet(const Wall &w,int index,int k) {
    uint32_t h=gore::hash(gore::hash(uint32_t(index)*977u+uint32_t(w.born*1000))+uint32_t(k)),g=gore::hash(h);
    return {((h&255)/255.0-.5)*w.radius*.9,.006+((h>>8)&255)/255.0*.01,w.drip*(.45+((h>>16)&255)/255.0*.55),
            .2+((h>>24)&15)*.045,.7+(g&63)/40.0};
}
double run(const Rivulet &r,double seconds) { return seconds<=r.delay?0:r.length*(1-std::exp(-(seconds-r.delay)/r.tau)); }
// A face key's map position at u, and the unit normal into open space.
void facePoint(int key,double u,double &x,double &y,double &nx,double &ny) {
    int along=key%64,line=(key/64)%65,facing=(key/(64*65))%2,orientation=key/(64*65*2);
    (void)along;
    double out=facing?-1:1; // a ray travelling + sees the face that looks back at it
    if(!orientation) { x=line; y=u; nx=out; ny=0; } else { x=u; y=line; nx=0; ny=out; }
}

void spawn(double x,double y,double z,double vx,double vy,double vz,double size,double life,uint8_t tone) {
    Drop &d=drops[dropCursor++%drops.size()];
    d={x,y,z,vx,vy,vz,size,0,life,tone,true};
}

void stepDrop(Drop &d) {
    bool mist=d.life<.6;
    d.vz-=Gravity*Step*(mist?.2:1);
    double drag=mist?.88:1-.00006/std::max(d.size,.002);
    d.vx*=drag; d.vy*=drag; d.vz*=mist?.92:drag;
    double nx=d.x+d.vx*Step,ny=d.y+d.vy*Step,nz=d.z+d.vz*Step;
    int ox=int(std::floor(d.x)),oy=int(std::floor(d.y)),tx=int(std::floor(nx)),ty=int(std::floor(ny));
    if(solid(tx,ty)) {
        // A drop already inside a solid tile (a closing door, a moving
        // pushwall) crosses no face, so there is nothing to stain.
        if(solid(ox,oy)) { d.active=false; return; }
        // Which face was crossed: the x boundary if moving into that column
        // alone is blocked, otherwise the y boundary.
        bool vertical=tx!=ox && solid(tx,oy);
        if(tx==ox) vertical=false;
        else if(ty!=oy && !solid(tx,oy)) vertical=false;
        double t,plane;
        int key;
        if(vertical) {
            plane=d.vx>0?tx:tx+1; t=(plane-d.x)/(nx-d.x);
            double u=d.y+(ny-d.y)*t;
            key=faceKey(0,d.vx>0?1:-1,int(plane),int(std::floor(u)));
            double hitZ=d.z+(nz-d.z)*t,speed=std::hypot(d.vx,d.vy);
            if(!mist || random01()<.25) addWall(key,u,hitZ,(mist?.008:.012)+d.size*(1.6+std::min(speed,4.0)*.5),!mist && d.size>.006?.06+speed*.05:0);
        } else {
            plane=d.vy>0?ty:ty+1; t=(plane-d.y)/(ny-d.y);
            double u=d.x+(nx-d.x)*t;
            key=faceKey(1,d.vy>0?1:-1,int(plane),int(std::floor(u)));
            double hitZ=d.z+(nz-d.z)*t,speed=std::hypot(d.vx,d.vy);
            if(!mist || random01()<.25) addWall(key,u,hitZ,(mist?.008:.012)+d.size*(1.6+std::min(speed,4.0)*.5),!mist && d.size>.006?.06+speed*.05:0);
        }
        d.active=false; return;
    }
    if(nz<=0) {
        if(!mist) {
            double across=std::hypot(d.vx,d.vy),down=std::fabs(d.vz),stretch=std::clamp(1+.5*across/std::max(down,.3),1.0,2.8);
            double radius=.01+d.size*(1.5+std::min(down+across*.5,4.0)*.5);
            double ux=across>1e-6?d.vx/across:0,uy=across>1e-6?d.vy/across:0,shift=radius*(stretch-1)*.5;
            addFloor(nx+ux*shift,ny+uy*shift,radius,false,ux,uy,stretch,0);
            if(d.size>.006 && across>.8) for(int k=int(random01()*3);k>=0;--k) {
                double ahead=radius*stretch*range(1.1,2.2),side=range(-1,1)*radius*.6;
                addFloor(nx+ux*ahead-uy*side,ny+uy*ahead+ux*side,radius*range(.18,.32),false,ux,uy,1.3,0);
            }
        } else if(random01()<.3) addFloor(nx,ny,.006+d.size,false,0,0,1,0);
        d.active=false; return;
    }
    if(nz>=1) { nz=1; d.vz=-std::fabs(d.vz)*.2; }
    d.x=nx; d.y=ny; d.z=nz;
    d.age+=Step;
    if(d.age>=d.life) d.active=false;
}

void stepChunk(Chunk &c) {
    if(c.resting) return;
    c.vz-=Gravity*Step;
    double nx=c.x+c.vx*Step,ny=c.y+c.vy*Step,nz=c.z+c.vz*Step;
    // Walls: reflect the crossing component, keep a wet mark for flesh.
    if(solid(int(std::floor(nx)),int(std::floor(c.y)))) {
        if(!c.bone) addWall(faceKey(0,c.vx>0?1:-1,int(c.vx>0?std::floor(nx):std::floor(nx)+1),int(std::floor(c.y))),c.y,std::clamp(c.z,.02,.98),c.size*1.3,std::fabs(c.vx)>1?.05:0);
        c.vx*=-(c.bone?.4:.15); nx=c.x;
    }
    if(solid(int(std::floor(nx)),int(std::floor(ny)))) {
        if(!c.bone) addWall(faceKey(1,c.vy>0?1:-1,int(c.vy>0?std::floor(ny):std::floor(ny)+1),int(std::floor(nx))),nx,std::clamp(c.z,.02,.98),c.size*1.3,std::fabs(c.vy)>1?.05:0);
        c.vy*=-(c.bone?.4:.15); ny=c.y;
    }
    if(nz>=1) { nz=1; c.vz=-std::fabs(c.vz)*.2; }
    if(nz<=0) {
        nz=0;
        double impact=-c.vz;
        if(!c.bone && impact>.6) addFloor(nx,ny,c.size*range(.9,1.4),false,c.vx,c.vy,1.4,0);
        // Restitution and ground friction per tissue.
        c.vz=impact*(c.bone?.35:.12); c.vx*=c.bone?.7:.35; c.vy*=c.bone?.7:.35;
        if(c.vz<.25 && std::hypot(c.vx,c.vy)<.12) { c.resting=true; c.vx=c.vy=c.vz=0; }
    }
    c.x=nx; c.y=ny; c.z=nz;
    // Airborne flesh sheds drops behind it.
    if(!c.bone && nz>0 && (c.trail+=Step)>.05) { c.trail=0; spawn(nx,ny,nz,c.vx*.2,c.vy*.2,c.vz*.2,range(.002,.004),2,c.tone); }
}

// A gore sound at (x,y) tiles, panned by direction from the player's view
// with an equal-power law, falling off from 2.5 to 18 tiles.
void goreSound(int kind,double x,double y,double strength) {
    double dx=x-player->x/double(TILEGLOBAL),dy=y-player->y/double(TILEGLOBAL),d=std::hypot(dx,dy);
    double level=(d<=2.5?1:std::max(0.0,(18-d)/15.5))*std::clamp(strength,0.0,1.0);
    if(level<=.02) return;
    double a=player->angle*(M_PI/180),pan=d>1e-3?std::clamp((dx*std::sin(a)+dy*std::cos(a))/d,-1.0,1.0):0;
    double theta=(pan+1)*M_PI/4;
    Wolf_NativeGoreSound(kind,int(256*level*std::cos(theta)*1.414),int(256*level*std::sin(theta)*1.414));
}
void stepSurfaces() {
    double t=now();
    for(size_t i=0;i<walls.size();++i) {
        Wall &w=walls[i];
        if(!w.active || w.drip<=0) continue;
        int count=rivulets(w);
        if(w.landed==(1<<count)-1) continue;
        for(int k=0;k<count;++k) {
            if(w.landed&(1<<k)) continue;
            Rivulet r=rivulet(w,int(i),k);
            if(w.z-w.radius*.2-run(r,t-w.born)>0) { if(t-w.born>r.delay+r.tau*4) w.landed|=uint8_t(1<<k); continue; }
            w.landed|=uint8_t(1<<k);
            double x,y,nx,ny; facePoint(w.key,w.u+r.offset,x,y,nx,ny);
            addFloor(x+nx*.03,y+ny*.03,.015+r.width*1.4,false,nx,ny,1.5,0);
        }
    }
    // Heartbeat jets: a 0.1 s stroke per beat gushing
    // out of the exit wound and arcing up, pressure decaying over ~1.3 s while the
    // heart slows and the wound sinks with the falling body.
    for(size_t i=0;i<spurts.size();) {
        Spurt &s=spurts[i]; double age=t-s.born;
        if(age>3) { s=spurts.back(); spurts.pop_back(); continue; }
        if(t>=s.beat) {
            double pressure=std::exp(-age/1.3),speed=1.3+3.2*pressure,z=std::max(.12,s.wound-age*.15);
            if(pressure>.2 && t-spurtHeard>.08) { spurtHeard=t; goreSound(gore_sound::Spurt,s.x,s.y,pressure); }
            for(int k=0;k<int(6+14*pressure);++k) {
                double yaw=range(-.07,.07)+std::sin(age*6)*.15,v=speed*range(.8,1.08);
                double cx=s.dx*std::cos(yaw)-s.dy*std::sin(yaw),cy=s.dx*std::sin(yaw)+s.dy*std::cos(yaw);
                spawn(s.x+s.dx*.12,s.y+s.dy*.12,z,cx*v,cy*v,v*range(.35,.7),range(.004,.009),3,uint8_t(random01()*3));
            }
            s.beat=t+.35+age*.12;
        }
        ++i;
    }
    for(size_t i=0;i<pools.size();) {
        if(t>=pools[i].at) { addFloor(pools[i].x,pools[i].y,pools[i].radius,true,0,0,1,range(6,10)); pools[i]=pools.back(); pools.pop_back(); }
        else ++i;
    }
}

void reset() {
    for(auto &d:drops) d.active=false;
    for(auto &f:floors) f.active=false;
    for(auto &w:walls) w.active=false;
    for(auto &c:chunks) c.active=false;
    faceHead.fill(-1); pools.clear(); spurts.clear(); walkers.clear(); flinches.clear();
    dropCursor=floorCursor=wallCursor=chunkCursor=0; accumulated=0;
}
bool active() { return viewsize>=20 && !demoplayback && !demorecord; }
// The game step's own refresh is never shown while display passes run, so
// no blood is drawn into it.
bool unseen() { return Wolf_RenderUnseen(); }

// Screen row for height z (0 floor, 1 ceiling) at a projected wall height.
inline double rowAt(double z,double height) { return viewheight*.5-(z-.5)*height*.25; }

uint8_t nearestColor(int r,int g,int b) {
    int best=0,error=INT_MAX;
    for(int i=0;i<256;++i) {
        int dr=gamepal[i].r-r,dg=gamepal[i].g-g,db=gamepal[i].b-b,d=2*dr*dr+4*dg*dg+3*db*db;
        if(d<error) { best=i; error=d; }
    }
    return uint8_t(best);
}
// Flesh: lit, mid, shadow; bone: lit, shadow.
struct ChunkColors { uint8_t flesh[3][3],bone[2]; };
const ChunkColors &chunkColors() {
    static ChunkColors c=[] {
        // Wolf's palette has no muted pinks (they match tan skin), so raw tissue
        // uses its deep-red ramp, a step darker than fresh blood.
        static const int flesh[3][3][3]={{{160,14,10},{122,6,4},{84,2,2}},{{150,10,8},{112,4,4},{76,0,0}},{{140,8,6},{100,2,2},{68,0,0}}};
        ChunkColors out{};
        for(int t=0;t<3;++t) for(int k=0;k<3;++k) out.flesh[t][k]=nearestColor(flesh[t][k][0],flesh[t][k][1],flesh[t][k][2]);
        out.bone[0]=nearestColor(214,204,182); out.bone[1]=nearestColor(150,138,116);
        return out;
    }();
    return c;
}
// A fragment as a small lit ellipsoid; resting ones lie flattened on the floor.
void drawChunk(byte *buffer,unsigned pitch,const Chunk &k,double vx,double vy,double c,double s) {
    double gx=k.x-vx,gy=k.y-vy,nx=gx*c-gy*s,ny=gy*c+gx*s;
    if(nx<MINDIST/double(TILEGLOBAL)) return;
    double height=heightnumerator/(nx*256);
    int x=int(centerx+ny*double(scale)/nx);
    if(x<-8 || x>=viewwidth+8) return;
    double ppt=height/4,rx=std::max(.6,k.size*ppt),ry=rx*(k.resting?.45:.8);
    double cy=rowAt(k.z+(k.resting?0:k.size*.5),height);
    const ChunkColors &cc=chunkColors();
    for(int px=std::max(0,int(x-rx));px<=std::min(viewwidth-1,int(x+rx));++px) {
        if(height<wallheight[px]) continue;
        for(int py=std::max(0,int(cy-ry));py<=std::min(viewheight-1,int(cy+ry));++py) {
            double u=(px+.5-x)/rx,v=(py+.5-cy)/ry,d=u*u+v*v;
            if(d>1) continue;
            double light=-.6*u-.8*v; // key light from above-left
            buffer[size_t(py)*pitch+px]=k.bone?cc.bone[light>-.1?0:1]:cc.flesh[k.tone][light>.25?0:light>-.35?1:2];
        }
    }
}

constexpr double TileMeters=1.9;
struct Foot { int kind; double length,width,scale; }; // metres; scale: leg length against a man's
bool footOf(const objtype *ob,Foot &f) {
    switch(ob->obclass) {
    case playerobj: case guardobj: case officerobj: case ssobj: case mutantobj: f={gore::PrintBoot,.30,.11,1}; return true;
    case bossobj: case gretelobj: case schabbobj: case giftobj: case fatobj: case fakeobj: case realhitlerobj: f={gore::PrintBoot,.32,.12,1.1}; return true;
    case dogobj: f={gore::PrintPaw,.075,.065,.55}; return true;
    case mechahitlerobj: f={gore::PrintPad,.45,.38,1.5}; return true;
    default: return false;
    }
}
double stepLength(double metersPerSecond,double scale) {
    double v=metersPerSecond/scale,cadence=std::clamp(1.8+.32*v,1.8,4.6);
    return std::clamp(v/cadence,.45,2.4)*scale;
}
// Wet blood under a foot: thickness times wetness.
double wetBlood(double x,double y,double footRadius,double t) {
    double best=0;
    for(const Floor &f:floors) {
        if(!f.active) continue;
        double grown=f.grow>0?std::max(.08,1-std::exp(-3*(t-f.born)/f.grow)):1,radius=f.radius*grown;
        double dx=x-f.x,dy=y-f.y,reach=radius*f.stretch+footRadius;
        if(dx*dx+dy*dy>reach*reach) continue;
        double along=(dx*f.c+dy*f.s)/(radius*f.stretch*.85+footRadius),across=(dy*f.c-dx*f.s)/(radius*.85+footRadius);
        if(along*along+across*across>1) continue;
        double thickness=f.pool?1.0:f.print?.12*f.load:.35*std::min(1.0,radius*TileMeters/.15);
        double density=f.pool?200:f.print?40:100,dry=20+70*density/255;
        best=std::max(best,thickness*(1-(t-f.born)/dry));
    }
    return best;
}
void addPrint(double x,double y,double c,double s,const Foot &f,bool left,double load) {
    double length=f.length/TileMeters,width=f.width/TileMeters;
    if(solid(int(x),int(y)) || clearance(x,y,length*.5)<width*.5) return;
    Floor &d=floors[floorCursor++%floors.size()];
    d={x,y,width*.5,c,s,length/width,now(),0,0,uint8_t(random01()*3),false,true};
    d.print=uint8_t(1+f.kind); d.left=left; d.load=std::clamp(load,0.0,1.0);
}
void stepWalkers() {
    double t=now(),dt=lastWalk<0?0:std::min(t-lastWalk,.25); lastWalk=t;
    if(dt<=0 || !player) return;
    for(auto &entry:walkers) entry.second.seen=false;
    const float drying=float(std::exp(-dt/60));
    for(objtype *ob=player;ob;ob=ob->next) {
        Foot f;
        if(!ob->state || !footOf(ob,f)) continue;
        if(ob==player?gamestate.health<=0:!(ob->flags&FL_SHOOTABLE)) continue;
        double x=ob->x/double(TILEGLOBAL),y=ob->y/double(TILEGLOBAL);
        Walker &w=walkers[ob];
        if(w.obclass!=ob->obclass) { w=Walker{}; w.x=x; w.y=y; w.obclass=ob->obclass; }
        w.seen=true;
        double dx=x-w.x,dy=y-w.y,moved=std::hypot(dx,dy);
        w.x=x; w.y=y;
        for(float &l:w.load) l*=drying;
        if(moved>1) { w.travelled=0; continue; }
        w.speed+=(moved*TileMeters/dt-w.speed)*std::min(1.0,dt/.25);
        if(moved<1e-5) {
            // Standing in blood (a pool spreading under the feet) wets both
            // soles; checked every quarter second while the body stays put.
            w.standing+=dt;
            if(w.standing<.25) continue;
            w.standing=0;
            double wet=wetBlood(x,y,(f.width*.5+.055*f.scale)/TileMeters,t);
            if(wet>0) for(float &l:w.load) l=float(l+(1-l)*wet);
            continue;
        }
        w.standing=0;
        w.travelled+=moved*TileMeters;
        double step=stepLength(w.speed,f.scale);
        if(w.travelled<step) continue;
        w.travelled=std::fmod(w.travelled-step,step);
        w.foot^=1;
        bool left=w.foot==1;
        double c=dx/moved,s=dy/moved,v=w.speed/f.scale;
        double lateral=(left?1:-1)*(.055-.035*std::min(v/6,1.0))*f.scale*(f.width/.11)/TileMeters;
        // Wolf's y grows southward, so the left of travel is (s,-c).
        double fx=x+s*lateral,fy=y-c*lateral,turn=left?-.12:.12;
        double pc=c*std::cos(turn)-s*std::sin(turn),ps=s*std::cos(turn)+c*std::sin(turn);
        float &load=w.load[w.foot];
        double wet=wetBlood(fx,fy,f.width/TileMeters*.5,t);
        if(wet>0) load=float(load+(1-load)*wet);
        if(wet<.5 && load>.035f) { addPrint(fx,fy,pc,ps,f,left,load); load*=.78f; }
    }
    for(auto it=walkers.begin();it!=walkers.end();) if(it->second.seen) ++it; else it=walkers.erase(it);
}
double response(double t) { constexpr double Peak=.06; return t<=0?0:(t/Peak)*std::exp(1-t/Peak); }
}

void Wolf_GoreBeginFrame() {
    static bool initialized=false;
    if(!initialized) {
        faceHead.fill(-1); initialized=true;
        // Build the palette ramps and stain masks now, once, so the first
        // blood of a fight doesn't stall a frame.
        ramps(); gore::poolMask(0); gore::splatMask(0); gore::printMask(0);
    }
    columnKey.assign(size_t(std::max(viewwidth,0)),-1); columnU.resize(size_t(std::max(viewwidth,0)));
    if(gamestate.mapon!=levelMap || gamestate.episode!=levelEpisode) { reset(); levelMap=gamestate.mapon; levelEpisode=gamestate.episode; }
    double t=now();
    if(lastTime<0 || t-lastTime>.25) lastTime=t-Step; // resume after menus and pauses without a burst
    accumulated+=t-lastTime; lastTime=t;
    for(int steps=0;accumulated>=Step && steps<14;++steps) {
        accumulated-=Step;
        for(auto &d:drops) if(d.active) stepDrop(d);
        for(auto &c:chunks) if(c.active) stepChunk(c);
    }
    accumulated=std::min(accumulated,Step);
    stepSurfaces();
    stepWalkers();
}

void Wolf_GoreWallHit(int column,int orientation,int32_t plane,int32_t along,int step) {
    if(column<0 || column>=int(columnKey.size())) return;
    // A moving pushwall sits between tile lines and carries no stains.
    if(plane&(TILEGLOBAL-1)) { columnKey[column]=-1; return; }
    columnKey[column]=faceKey(orientation,step,plane>>TILESHIFT,along>>TILESHIFT);
    columnU[column]=along/double(TILEGLOBAL);
}

void Wolf_GoreHit(void *pointer,unsigned damage) {
    if(!active() || !damage) return;
    const auto *target=static_cast<const objtype *>(pointer);
    if(!target || target==player || target->obclass==mechahitlerobj || target->obclass==ghostobj || target->hitpoints<=0) return;
    bool death=int(damage)>=target->hitpoints,melee=gamestate.weapon==wp_knife;
    double x=target->x/double(TILEGLOBAL),y=target->y/double(TILEGLOBAL);
    double dx=x-player->x/double(TILEGLOBAL),dy=y-player->y/double(TILEGLOBAL),distance=std::hypot(dx,dy);
    if(distance<1e-3) { dx=1; dy=0; distance=1; }
    dx/=distance; dy/=distance;
    double k=std::clamp(damage/12.0,.5,4.0)*(death?1.6:1),wound=range(.42,.62);
    seed^=uint32_t(target->x)*2654435761u^uint32_t(target->y);
    // Exit spray away from the shooter, low-skewed speeds in a flat cone.
    goreSound(death?(damage>=60?gore_sound::Gib:gore_sound::Kill):damage>=30?gore_sound::Heavy:gore_sound::Hit,x,y,death?1:std::min(1.0,.45+damage/30.0));
    int exit=int((melee?6:18)*k);
    for(int i=0;i<exit;++i) {
        double yaw=range(-.4,.4),r=random01(),speed=(melee?.5:.9)+(melee?1.0:1.6)*k*.4*r*r;
        double cx=dx*std::cos(yaw)-dy*std::sin(yaw),cy=dx*std::sin(yaw)+dy*std::cos(yaw);
        spawn(x+dx*.12,y+dy*.12,wound,cx*speed,cy*speed,range(-.4,.5),range(.003,.009),3,uint8_t(random01()*3));
    }
    // Back spatter and a short-lived mist toward the shooter.
    for(int i=0;i<int(5*k);++i) {
        double yaw=range(-.7,.7),speed=range(.3,.9),cx=-dx*std::cos(yaw)+dy*std::sin(yaw),cy=-dy*std::cos(yaw)-dx*std::sin(yaw);
        spawn(x-dx*.08,y-dy*.08,wound,cx*speed,cy*speed,range(-.2,.4),range(.003,.006),3,uint8_t(random01()*3));
    }
    for(int i=0;i<int(10*k);++i) {
        double a=range(0,6.2832),speed=range(.2,.7);
        spawn(x,y,wound,std::cos(a)*speed,std::sin(a)*speed,range(-.1,.3),range(.002,.004),range(.25,.5),uint8_t(random01()*3));
    }
    // Wound drips straight down.
    for(int i=0;i<int(2+k);++i) spawn(x+range(-.04,.04),y+range(-.04,.04),wound-.05,range(-.05,.05),range(-.05,.05),0,range(.004,.007),3,0);
    if(death) {
        pools.push_back({x,y,range(.26,.42),now()+range(.5,.9)});
        if(spurts.size()<32) spurts.push_back({x,y,dx,dy,wound,now(),now()+.06});
    }
    else {
        // Hit reaction: impulse on the body's mass plus the reflex flinch.
        double mass=target->obclass==dogobj?35:target->obclass==mechahitlerobj?500:target->obclass>=bossobj && target->obclass!=mutantobj?150:80;
        double meters=std::min(.27,.06*std::sqrt(100/mass)+.003*std::min(damage,200u)*100/mass);
        Flinch &f=flinches[target];
        if(f.obclass!=target->obclass) f=Flinch{};
        f.obclass=target->obclass;
        f.hits[f.next++%f.hits.size()]={now(),dx,dy,meters/TileMeters};
    }
    // Tissue fragments from the exit wound; bone splinters only from
    // high-energy (close, heavy) hits. Dogs and men alike; the knife cuts
    // but does not fragment.
    if(melee) return;
    int tissue=death?int(2+damage/7.0):damage>=15?1+damage/25:0,bone=death && damage>=20?int(1+damage/25.0):damage>=40?1:0;
    for(int i=0;i<tissue+bone;++i) {
        Chunk &c=chunks[chunkCursor++%chunks.size()];
        bool isBone=i>=tissue;
        double yaw=range(-.5,.5),speed=range(.8,2.2)*(isBone?1.2:1)*std::min(1.6,.7+damage/40.0);
        double cx=dx*std::cos(yaw)-dy*std::sin(yaw),cy=dx*std::sin(yaw)+dy*std::cos(yaw);
        c={x+dx*.1,y+dy*.1,wound+range(-.08,.08),cx*speed,cy*speed,range(.3,1.4),isBone?range(.012,.022):range(.016,.034),0,uint8_t(random01()*3),isBone,false,true};
    }
}

// Presentation offset of a flinching actor, in global units.
void Wolf_GoreFlinch(const void *actor,int32_t *x,int32_t *y) {
    *x=*y=0;
    if(flinches.empty()) return;
    auto found=flinches.find(static_cast<const objtype *>(actor));
    if(found==flinches.end()) return;
    const objtype *ob=found->first; const Flinch &f=found->second;
    double t=now(),ox=0,oy=0,latest=0;
    for(const Impulse &i:f.hits) if(i.amplitude>0) { double k=i.amplitude*response(t-i.start); ox+=i.dx*k; oy+=i.dy*k; latest=std::max(latest,i.start); }
    if(ob->obclass!=f.obclass || !(ob->flags&FL_SHOOTABLE) || t-latest>1) { flinches.erase(found); return; }
    double length=std::hypot(ox,oy),limit=.12/TileMeters;
    if(length>limit) { ox*=limit/length; oy*=limit/length; }
    *x=int32_t(ox*TILEGLOBAL); *y=int32_t(oy*TILEGLOBAL);
}

namespace {
// Screen box of a floor area within reach of (rx,ry), relative to the eye.
struct FloorBox { int x0=0,x1=-1,y0=0,y1=-1; double rx=0,ry=0; };
// Rows per band when the floor pass is spread across cores: each
// band applies every item in order to its own rows, the same result as one
// pass over the whole screen.
constexpr int BandRows=32;
}
void Wolf_GoreDrawSurfaces(void *pixels,unsigned pitch) {
    if(!active() || unseen() || columnKey.empty()) return;
    auto *buffer=static_cast<byte *>(pixels);
    double t=now(),c=viewcos/double(TILEGLOBAL),s=viewsin/double(TILEGLOBAL),vx=viewx/double(TILEGLOBAL),vy=viewy/double(TILEGLOBAL);
    double focal=double(scale),center=viewheight*.5;
    // A stain's drips depend only on the stain and the time: found once per
    // frame, in rivulet order, then filtered per column as before.
    struct Drips { Rivulet runs[3]; double length[3]; int count=0; };
    static std::array<Drips,std::tuple_size<decltype(walls)>::value> drips;
    for(int i=0;i<int(walls.size());++i) {
        const Wall &w=walls[i]; Drips &d=drips[i]; d.count=0;
        if(!w.active) continue;
        for(int k=0,n=rivulets(w);k<n;++k) {
            Rivulet r=rivulet(w,i,k);
            double l=run(r,t-w.born);
            if(l<.006) continue;
            d.runs[d.count]=r; d.length[d.count]=l; ++d.count;
        }
    }
    // Walls: every column remembers the exact face and position it hit.
    // Columns write only their own pixels, so they are drawn in parallel.
    constexpr int Strips=64;
    parallel::forEach(Strips,[&](size_t strip) {
        for(int x=int(strip*viewwidth/Strips);x<int((strip+1)*viewwidth/Strips);++x) {
            int key=columnKey[x];
            if(key<0) continue;
            int along=key%64;
            double u=columnU[x],height=wallheight[x],half=height/8;
            if(half<1) continue;
            for(int neighbor=-1;neighbor<=1;++neighbor) {
                int other=key+neighbor;
                if(along+neighbor<0 || along+neighbor>=64) continue;
                for(int i=faceHead[other];i>=0;i=walls[i].next) {
                    const Wall &w=walls[i];
                    double du=u-w.u,age=t-w.born;
                    const Rivulet *runs[3]; double length[3]; int count=0; double lowest=w.z-w.radius;
                    for(int k=0;k<drips[i].count;++k) {
                        const Rivulet &r=drips[i].runs[k];
                        if(std::fabs(du-r.offset)>r.width*1.3) continue;
                        double l=drips[i].length[k];
                        runs[count]=&r; length[count]=l; ++count; lowest=std::min(lowest,w.z-w.radius*.2-l-.01);
                    }
                    bool inside=std::fabs(du)<=w.radius;
                    if(!inside && !count) continue;
                    int y0=std::max(0,int(std::floor(rowAt(w.z+w.radius,height)))),y1=std::min(viewheight-1,int(std::ceil(rowAt(std::max(lowest,0.0),height))));
                    const uint8_t *mask=gore::splatMask(w.shape);
                    int mx=std::clamp(int((du/w.radius*.5+.5)*MaskSize),0,MaskSize-1);
                    for(int y=y0;y<=y1;++y) {
                        double z=.5-(y+.5-center)/(2*half),ly=(w.z-z)/w.radius*.5+.5;
                        if(z<0) break;
                        int density=0;
                        if(inside && ly>=0 && ly<1) density=mask[int(ly*MaskSize)*MaskSize+mx];
                        for(int k=0;k<count && !density;++k) {
                            double down=w.z-w.radius*.2-z;
                            if(down<0 || down>length[k]) continue;
                            bool bead=down>length[k]-.012;
                            double width=runs[k]->width*(bead?1.25:1-.45*down/std::max(length[k],.01));
                            if(std::fabs(du-runs[k]->offset)<width) density=bead?225:165;
                        }
                        if(density) buffer[size_t(y)*pitch+x]=shade(density,age,w.tone);
                    }
                }
            }
        }
    });
    // Floors: each stain's screen box, inverse-projected per pixel and
    // limited to rows below that column's wall.
    auto floorBox=[&](double rx,double ry,double reach,FloorBox &box) {
        if(rx*c-ry*s<-reach || rx*rx+ry*ry>40*40) return false;
        double minX=1e30,maxX=-1e30,minY=1e30,maxY=-1e30;
        for(int corner=0;corner<4;++corner) {
            double gx=rx+((corner&1)?reach:-reach),gy=ry+((corner&2)?reach:-reach);
            double nx=std::max(gx*c-gy*s,MINDIST/double(TILEGLOBAL)),ny=gy*c+gx*s;
            double sx=centerx+ny*focal/nx,sy=center+(heightnumerator/(nx*256))/8;
            minX=std::min(minX,sx); maxX=std::max(maxX,sx); minY=std::min(minY,sy); maxY=std::max(maxY,sy);
        }
        box={std::max(0,int(std::floor(minX))-1),std::min(viewwidth-1,int(std::ceil(maxX))+1),
             std::max(int(center)+1,int(std::floor(minY))-1),std::min(viewheight-1,int(std::ceil(maxY))+1),rx,ry};
        return box.x0<=box.x1 && box.y0<=box.y1;
    };
    // visit(x,y,wx,wy) receives each visible floor pixel of the box in rows
    // top..bottom, with its offset from the box's point.
    auto floorPixels=[&](const FloorBox &box,int top,int bottom,auto visit) {
        for(int y=std::max(box.y0,top);y<=std::min(box.y1,bottom);++y) {
            double half=y+.5-center;
            if(half<=0) continue;
            double nx=heightnumerator/(8*half*256);
            for(int x=box.x0;x<=box.x1;++x) {
                if(y<center+(wallheight[x]>>3)) continue;
                double ny=(x+.5-centerx)*nx/focal;
                visit(x,y,nx*c+ny*s-box.rx,-nx*s+ny*c-box.ry);
            }
        }
    };
    // Soft contact shadows under actors (their presentation copies): a dark
    // core with a dithered penumbra, drawn before the blood that lies on top.
    static uint8_t darker[2][256]; static bool darkened=false;
    if(!darkened) {
        const float factor[2]={.72f,.52f};
        for(int k=0;k<2;++k) for(int i=0;i<256;++i) {
            int best=i,error=INT_MAX;
            int tr=int(gamepal[i].r*factor[k]),tg=int(gamepal[i].g*factor[k]),tb=int(gamepal[i].b*factor[k]);
            for(int j=0;j<256;++j) {
                int dr=gamepal[j].r-tr,dg=gamepal[j].g-tg,db=gamepal[j].b-tb,d=2*dr*dr+4*dg*dg+3*db*db;
                if(d<error) { best=j; error=d; }
            }
            darker[k][i]=uint8_t(best);
        }
        darkened=true;
    }
    static const uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    constexpr double Reach=.34;
    static std::vector<FloorBox> shadows; shadows.clear();
    const auto *self=static_cast<const objtype *>(Wolf_RenderPlayer());
    for(const objtype *ob=self?self->next:nullptr;ob;ob=ob->next) {
        FloorBox box;
        if(ob->state && floorBox(ob->x/double(TILEGLOBAL)-vx,ob->y/double(TILEGLOBAL)-vy,Reach,box)) shadows.push_back(box);
    }
    // Each stain's size, colors and mask for this frame, worked out once.
    struct Stain { FloorBox box; const Floor *f; const uint8_t *mask; double inverse,major; uint8_t colors[16]; int threshold,film; bool flip; };
    static std::vector<Stain> stains; stains.clear();
    for(const Floor &f:floors) {
        if(!f.active) continue;
        double age=t-f.born,grown=f.grow>0?std::max(.08,1-std::exp(-3*age/f.grow)):1;
        double radius=f.radius*grown;
        Stain k{};
        if(!floorBox(f.x-vx,f.y-vy,radius*f.stretch,k.box)) continue;
        k.f=&f;
        k.mask=f.print?gore::printMask(f.print-1):f.pool?gore::poolMask(f.shape):gore::splatMask(f.shape);
        k.inverse=.5/radius; k.major=k.inverse/f.stretch;
        for(int level=0;level<16;++level) k.colors[level]=shade(level*16+8,age,f.tone);
        // A footprint is a thin film: a nearly clean sole prints only its
        // hardest-pressed parts. Wolf's y runs south, so a right sole's inner
        // side (upper mask rows) lies at negative local y.
        k.threshold=f.print?int(235*(1-f.load)):0; k.film=f.print?int(70+140*f.load):255;
        k.flip=f.print && !f.left;
        stains.push_back(k);
    }
    parallel::forEach(size_t((viewheight+BandRows-1)/BandRows),[&](size_t band) {
        int top=int(band)*BandRows,bottom=std::min(viewheight-1,top+BandRows-1);
        for(const FloorBox &box:shadows)
            floorPixels(box,top,bottom,[&](int x,int y,double wx,double wy) {
                double d=std::hypot(wx,wy)/Reach;
                if(d>=1) return;
                byte &pixel=buffer[size_t(y)*pitch+x];
                if(d<.5) pixel=darker[1][pixel];
                else if((d-.5)/.5*16<=bayer[y&3][x&3]) pixel=darker[d<.75 && bayer[y&3][x&3]&1?1:0][pixel];
            });
        for(const Stain &k:stains) {
            const Floor &f=*k.f;
            floorPixels(k.box,top,bottom,[&](int x,int y,double wx,double wy) {
                double lx=(wx*f.c+wy*f.s)*k.major+.5,ly=(wy*f.c-wx*f.s)*k.inverse+.5;
                if(lx<0 || ly<0 || lx>=1 || ly>=1) return;
                if(k.flip) ly=std::nextafter(1.0,0.0)-ly;
                int density=k.mask[int(ly*MaskSize)*MaskSize+int(lx*MaskSize)];
                if(f.print) { if(density<=k.threshold) return; density=std::max(1,k.film*density/255); }
                if(density) buffer[size_t(y)*pitch+x]=k.colors[density>>4];
            });
        }
    });
    for(const Chunk &k:chunks) if(k.active && k.resting) drawChunk(buffer,pitch,k,vx,vy,c,s);
}

void Wolf_GoreDraw(void *pixels,unsigned pitch) {
    if(!active() || unseen()) return;
    auto *buffer=static_cast<byte *>(pixels);
    const Ramps &r=ramps();
    double c=viewcos/double(TILEGLOBAL),s=viewsin/double(TILEGLOBAL),vx=viewx/double(TILEGLOBAL),vy=viewy/double(TILEGLOBAL);
    for(const Drop &d:drops) {
        if(!d.active) continue;
        double gx=d.x-vx,gy=d.y-vy,nx=gx*c-gy*s,ny=gy*c+gx*s;
        if(nx<MINDIST/double(TILEGLOBAL)) continue;
        double height=heightnumerator/(nx*256);
        int x=int(centerx+ny*double(scale)/nx);
        if(x<0 || x>=viewwidth || height<wallheight[x]) continue;
        // Falling drops read as short streaks above their leading edge.
        double pixelsPerTile=height/4,radius=std::max(.5,d.size*pixelsPerTile),streak=std::min(std::max(0.0,-d.vz)*.03,.04)*pixelsPerTile;
        int y=int(rowAt(d.z,height)),half=int(radius);
        bool mist=d.life<.6;
        uint8_t color=r.wet[std::min(8,d.tone+(mist?0:1))];
        for(int px=std::max(0,x-half);px<=std::min(viewwidth-1,x+half);++px) {
            if(height<wallheight[px]) continue;
            for(int py=std::max(0,y-half-int(streak));py<=std::min(viewheight-1,y+half);++py) buffer[size_t(py)*pitch+px]=color;
        }
    }
    for(const Chunk &k:chunks) if(k.active && !k.resting) drawChunk(buffer,pitch,k,vx,vy,c,s);
}
