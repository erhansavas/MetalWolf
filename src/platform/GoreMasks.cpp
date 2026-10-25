// SPDX-License-Identifier: GPL-2.0-only
// Procedural stain masks for the gore renderer: lobed
// pools with ragged rims, and impact splats with streaks, beads and
// satellites. Built once from a fixed seed, so every run draws the same set.
#include "GoreMasks.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace gore {
namespace {
uint32_t seed=0x1234567u;
float random01() { seed=seed*1664525u+1013904223u; return float(seed>>8)/16777216.0f; }
float range(float low,float high) { return low+(high-low)*random01(); }

std::array<std::array<uint8_t,MaskSize*MaskSize>,PoolShapes> pools;
std::array<std::array<uint8_t,MaskSize*MaskSize>,SplatShapes> splats;
std::array<std::array<uint8_t,MaskSize*MaskSize>,PrintShapes> prints;

float valueNoise(float x,float y,uint32_t salt) {
    int ix=int(std::floor(x)),iy=int(std::floor(y)); float fx=x-ix,fy=y-iy;
    auto at=[&](int a,int b) { return float(hash(uint32_t(a)*73856093u^uint32_t(b)*19349663u^salt)&0xffff)/65535.0f; };
    float sx=fx*fx*(3-2*fx),sy=fy*fy*(3-2*fy);
    float top=at(ix,iy)+(at(ix+1,iy)-at(ix,iy))*sx,bottom=at(ix,iy+1)+(at(ix+1,iy+1)-at(ix,iy+1))*sx;
    return top+(bottom-top)*sy;
}
void disc(std::array<uint8_t,MaskSize*MaskSize> &mask,float cx,float cy,float radius,float density) {
    for(int y=std::max(0,int((cy-radius)*MaskSize));y<std::min(MaskSize,int((cy+radius)*MaskSize)+1);++y)
        for(int x=std::max(0,int((cx-radius)*MaskSize));x<std::min(MaskSize,int((cx+radius)*MaskSize)+1);++x) {
            float dx=(x+.5f)/MaskSize-cx,dy=(y+.5f)/MaskSize-cy;
            if(dx*dx+dy*dy<=radius*radius) mask[y*MaskSize+x]=std::max(mask[y*MaskSize+x],uint8_t(density));
        }
}
// Pools: thick lobed body with a ragged, thinner rim.
void makePool(std::array<uint8_t,MaskSize*MaskSize> &mask,uint32_t salt) {
    float phase[4]; for(float &p:phase) p=range(0,6.2832f);
    for(int y=0;y<MaskSize;++y) for(int x=0;x<MaskSize;++x) {
        float dx=(x+.5f)/MaskSize*2-1,dy=(y+.5f)/MaskSize*2-1;
        float r=std::sqrt(dx*dx+dy*dy),a=std::atan2(dy,dx);
        float edge=.70f+.10f*std::sin(3*a+phase[0])+.07f*std::sin(5*a+phase[1])+.05f*std::sin(9*a+phase[2])
            +.14f*(valueNoise(dx*5+7,dy*5+3,salt)-.5f);
        float depth=(edge-r)/.28f;
        if(depth>0) mask[y*MaskSize+x]=uint8_t(std::clamp(30+depth*225+(valueNoise(dx*11,dy*11,salt^99)-.5f)*40,1.0f,250.0f));
    }
    for(int i=0;i<10;++i) { float a=range(0,6.2832f),d=range(.38f,.47f); disc(mask,.5f+std::cos(a)*d,.5f+std::sin(a)*d,range(.012f,.035f),70); }
}
// Splats: impact core, tapering radial streaks ending in beads, satellites.
void makeSplat(std::array<uint8_t,MaskSize*MaskSize> &mask,uint32_t salt) {
    for(int y=0;y<MaskSize;++y) for(int x=0;x<MaskSize;++x) {
        float dx=(x+.5f)/MaskSize*2-1,dy=(y+.5f)/MaskSize*2-1,r=std::sqrt(dx*dx+dy*dy);
        float a=std::atan2(dy,dx);
        float edge=.52f+.10f*std::sin(2*a+salt%7)+.22f*(valueNoise(dx*3.5f,dy*3.5f,salt)-.5f)+.10f*(valueNoise(dx*9,dy*9,salt^7)-.5f);
        if(r<edge) mask[y*MaskSize+x]=uint8_t(std::clamp(50+(edge-r)/edge*200+(valueNoise(dx*14,dy*14,salt^3)-.5f)*50,1.0f,250.0f));
    }
    int spikes=int(random01()*4);
    for(int i=0;i<spikes;++i) {
        float a=range(0,6.2832f),length=range(.08f,.24f),width=range(.03f,.05f);
        for(float t=0;t<=1;t+=.04f) disc(mask,.5f+std::cos(a)*(.24f+t*length),.5f+std::sin(a)*(.24f+t*length),width*(1-t*.6f),90);
    }
    int beads=3+int(random01()*9);
    for(int i=0;i<beads;++i) { float a=range(0,6.2832f),d=range(.30f,.48f); disc(mask,.5f+std::cos(a)*d,.5f+std::sin(a)*d,range(.012f,.035f),80); }
}
// Footprint soles in mask space: u runs heel (0) to toe (1), a across the
// sole from the outer edge (-1) to the inner edge (+1). Pressure is highest
// under the heel, the ball and the toes; the arch barely touches.
bool inEllipse(float u,float a,float cu,float ca,float ru,float ra) { float x=(u-cu)/ru,y=(a-ca)/ra; return x*x+y*y<=1; }
float edgeDepth(float u,float a,float cu,float ca,float ru,float ra) { float x=(u-cu)/ru,y=(a-ca)/ra; return 1-std::sqrt(x*x+y*y); }
uint8_t contact(float base,float spread,float u,float a,uint32_t salt) {
    return uint8_t(std::clamp(base+(valueNoise(u*23,a*11,salt)-.5f)*spread,1.0f,255.0f));
}
// Lugged boot: heel block, a raised shank that leaves no mark, a forefoot
// with chevron lugs and a solid rim; the medial toe reaches furthest.
uint8_t boot(float u,float a,uint32_t salt) {
    bool heel=inEllipse(u,a,.16f,0,.13f,.70f) || (u>=.16f && u<=.28f && std::fabs(a)<=.70f);
    float s=std::clamp((u-.36f)/.34f,0.0f,1.0f); s=s*s*(3-2*s);
    float half=a>=0?.48f+.47f*s:.62f+.20f*s;
    float reach=a>=0?.985f:.94f;
    if(u>.80f) { float t=(u-.80f)/(reach-.80f); half*=t<1?std::sqrt(1-t*t):0; }
    bool fore=u>=.36f && u<=reach && std::fabs(a)<=half;
    if(!heel && !fore) return 0;
    bool rim=heel?(u<.16f?edgeDepth(u,a,.16f,0,.13f,.70f)<.12f:.70f-std::fabs(a)<.07f || .28f-u<.02f):half-std::fabs(a)<.07f;
    if(rim) return contact(235,30,u,a,salt);
    float chevron=u*11+std::fabs(a)*.9f; chevron-=std::floor(chevron);
    if(chevron<.26f || std::fabs(a)<.05f) return 0;
    float pressure=heel?205:u>.62f && u<.82f?215:u>.86f?195:150;
    return contact(pressure,70,u,a,salt);
}
// Three clawed toes on a broad metatarsal pad (imps, demons, revenants).
uint8_t claw(float u,float a,uint32_t salt) {
    if(inEllipse(u,a,.30f,0,.22f,.58f)) return contact(140+100*edgeDepth(u,a,.30f,0,.22f,.58f),60,u,a,salt);
    for(int i=-1;i<=1;++i) {
        float cu=.63f+(i?0:.07f),ca=.56f*i;
        if(inEllipse(u,a,cu,ca,.11f,.20f)) return contact(205,50,u,a,salt);
        float t=(u-(cu+.10f))/.17f;
        if(t>=0 && t<=1 && std::fabs(a-ca-i*.12f*t)<.07f*(1-t*.7f)) return contact(240,20,u,a,salt);
    }
    return 0;
}
// Cloven hoof: two toes split by a cleft; the hard wall prints, the sole less.
uint8_t hoof(float u,float a,uint32_t salt) {
    if(std::fabs(a)<.07f+.08f*u) return 0;
    float side=a>0?1.f:-1.f,depth=edgeDepth(u,a,.50f,.46f*side,.47f,.48f);
    if(depth<0) return 0;
    if(depth<.16f) return contact(240,20,u,a,salt);
    return contact(u<.2f?165:110,80,u,a,salt);
}
// Broad, cracked load-bearing pad with nails at the front (mancubus,
// arachnotron and spider feet).
uint8_t pad(float u,float a,uint32_t salt) {
    for(int i=0;i<4;++i) { float ca=-.66f+.44f*i; if(inEllipse(u,a,.90f-.04f*std::fabs(ca),ca,.07f,.16f)) return contact(240,20,u,a,salt); }
    float depth=edgeDepth(u,a,.44f,0,.42f,.92f);
    if(depth<0) return 0;
    float crack=valueNoise(u*9,a*6,salt^0x51);
    if(std::fabs(crack-.5f)<.035f) return 0;
    return contact(150+90*std::min(depth*3,1.0f),70,u,a,salt);
}
// Dog paw: four toe pads and claw points ahead of a lobed palm pad.
uint8_t paw(float u,float a,uint32_t salt) {
    if(inEllipse(u,a,.30f,0,.22f,.52f)) return contact(220,40,u,a,salt);
    static const float toes[4][2]={{.66f,-.24f},{.66f,.24f},{.56f,-.68f},{.56f,.68f}};
    for(const auto &t:toes) {
        if(inEllipse(u,a,t[0],t[1],.12f,.20f)) return contact(215,40,u,a,salt);
        if(inEllipse(u,a,t[0]+.17f,t[1]*1.1f,.04f,.06f)) return contact(240,10,u,a,salt);
    }
    return 0;
}
void makePrint(std::array<uint8_t,MaskSize*MaskSize> &mask,int kind,uint32_t salt) {
    for(int y=0;y<MaskSize;++y) for(int x=0;x<MaskSize;++x) {
        float u=(x+.5f)/MaskSize,a=(y+.5f)/MaskSize*2-1;
        mask[y*MaskSize+x]=kind==PrintBoot?boot(u,a,salt):kind==PrintClaw?claw(u,a,salt):kind==PrintHoof?hoof(u,a,salt):kind==PrintPad?pad(u,a,salt):paw(u,a,salt);
    }
}
void buildMasksOnce() {
    for(int i=0;i<PrintShapes;++i) makePrint(prints[i],i,hash(i+307));
    for(int i=0;i<PoolShapes;++i) { pools[i].fill(0); makePool(pools[i],hash(i+11)); }
    for(int i=0;i<SplatShapes;++i) { splats[i].fill(0); makeSplat(splats[i],hash(i+101)); }
}
}

// Masks are read by parallel render workers; build them exactly once.
void buildMasks() { static std::once_flag once; std::call_once(once,buildMasksOnce); }
}
namespace gore {
const uint8_t *poolMask(int shape) { buildMasks(); return pools[shape%PoolShapes].data(); }
const uint8_t *splatMask(int shape) { buildMasks(); return splats[shape%SplatShapes].data(); }
const uint8_t *printMask(int kind) { buildMasks(); return prints[std::clamp(kind,0,PrintShapes-1)].data(); }
uint32_t hash(uint32_t v) { v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; v^=v>>16; return v; }
}
