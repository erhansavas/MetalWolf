// SPDX-License-Identifier: GPL-2.0-only
// Gore sound layers (2026-10-10). Built like film/game gore foley: an event
// is an entry snap, a body thump that carries the weight, the wet squelch,
// bone fracture when it applies, and the debris tail of blood and tissue
// landing. Every layer is seeded per kind and variant, so the four variants
// differ but each is identical on every run. Built once at sound start-up.
#include "GoreSound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gore_sound {
namespace {
constexpr double Pi=3.14159265358979323846;
struct Rng {
    uint32_t s;
    double n() { s=s*1664525u+1013904223u; return double(int32_t(s))/2147483648.0; } // -1..1
    double u() { return .5+.5*n(); }                                               //  0..1
};
using Buffer=std::vector<double>;
size_t at(double seconds) { return size_t(std::max(0.0,seconds)*Rate); }
void mix(Buffer &out,double start,const Buffer &layer,double gain) {
    size_t o=at(start);
    if(out.size()<o+layer.size()) out.resize(o+layer.size(),0);
    for(size_t i=0;i<layer.size();++i) out[o+i]+=layer[i]*gain;
}
// Two-pole resonator ringing at f for about `decay` seconds.
struct Resonator {
    double a1,a2,y1=0,y2=0;
    Resonator(double f,double decay) { double r=std::exp(-1/(decay*Rate)); a1=2*r*std::cos(2*Pi*f/Rate); a2=-r*r; }
    double operator()(double x) { double y=x+a1*y1+a2*y2; y2=y1; y1=y; return y; }
};

// Bullet entry: a 1.5 ms broadband burst ringing a hard 3.5-6 kHz mode.
Buffer tick(Rng &r) {
    Buffer b(at(.012)); Resonator res(3500+2500*r.u(),.0015);
    for(size_t i=0;i<b.size();++i) b[i]=res(i<66?r.n()*(1-i/66.0):0)*.12;
    return b;
}
// The weight of the blow: a low sweep (f0 falling to f1; laptop speakers
// roll off below ~150 Hz, so it stays above that) and a muffled slap.
Buffer thump(Rng &r,double f0,double f1,double decay) {
    Buffer b(at(decay*6)); double phase=0,low=0;
    for(size_t i=0;i<b.size();++i) {
        double t=double(i)/Rate;
        phase+=2*Pi*(f1+(f0-f1)*std::exp(-t/.015))/Rate;
        low+=.12*(r.n()-low);
        b[i]=std::sin(phase)*std::exp(-t/decay)+std::sin(2*phase)*.25*std::exp(-t/(decay*.5))+low*2.4*std::exp(-t/.008);
    }
    return b;
}
// Wet squelch: a slice of DSSLOP resampled by `pitch`, or, without it, low
// resonant noise whose formant sinks as the tissue closes.
Buffer squelch(Rng &r,const float *slop,size_t count,unsigned slopRate,double pitch,double seconds) {
    Buffer b(at(seconds));
    if(count>2400 && slopRate) {
        double step=pitch*slopRate/Rate;
        for(size_t i=0;i<b.size();++i) {
            double p=i*step; size_t k=size_t(p); if(k+1>=count) { b.resize(i); break; }
            double f=p-k,t=double(i)/b.size();
            b[i]=(slop[k]*(1-f)+slop[k+1]*f)*std::min(1.0,i/96.0)*std::pow(1-t,1.6);
        }
        return b;
    }
    double y1=0,y2=0;
    for(size_t i=0;i<b.size();++i) {
        double t=double(i)/b.size(),f=(700-450*t)*pitch,rr=std::exp(-1/(.004*Rate));
        double y=r.n()+2*rr*std::cos(2*Pi*f/Rate)*y1-rr*rr*y2; y2=y1; y1=y;
        b[i]=y*.05*std::min(1.0,i/96.0)*std::pow(1-t,1.6);
    }
    return b;
}
// Fracture: the first break, then smaller ones over ~40 ms as the crack
// runs, each a broadband impulse ringing a stiff 1.6-4.2 kHz resonance.
Buffer crack(Rng &r,int snaps) {
    Buffer b(at(.09)); double when=0,amp=1;
    for(int s=0;s<snaps;++s) {
        double decay=.002+.004*r.u(); Resonator res(1600+2600*r.u(),decay);
        size_t start=at(when);
        for(size_t i=start;i<b.size() && i<start+at(decay*8);++i) b[i]+=res(i-start<48?r.n()*(1-(i-start)/48.0)*amp:0)*.35;
        when+=.004+.012*r.u(); amp*=.5+.2*r.u();
    }
    return b;
}
// Debris tail: blood and fragments landing. Droplet grains (each a tiny
// ringing blip) front-loaded in time over a wet low-passed noise bed.
Buffer splatter(Rng &r,double seconds,double density) {
    Buffer b(at(seconds+.01)); double low=0;
    for(size_t i=0;i<b.size();++i) { double t=double(i)/Rate; low+=.3*(r.n()-low); b[i]=low*.5*std::exp(-t/(seconds*.35)); }
    int grains=int(density*seconds);
    for(int g=0;g<grains;++g) {
        double t=seconds*std::pow(r.u(),1.8),decay=.0006+.002*r.u(),amp=(.3+.7*r.u())*(1-t/seconds*.7);
        Resonator res(1200+4300*r.u(),decay);
        size_t start=at(t);
        for(size_t i=start;i<b.size() && i<start+at(decay*7);++i) b[i]+=res(i-start<12?r.n()*amp:0)*.18;
    }
    return b;
}
// Tearing tissue: band-passed noise sweeping up from 350 Hz as fibres part,
// gated by stick-slip bursts of 3-8 ms.
Buffer rip(Rng &r,double seconds) {
    Buffer b(at(seconds)); double y1=0,y2=0,gate=0,next=0,open=0;
    for(size_t i=0;i<b.size();++i) {
        double t=double(i)/Rate,f=350+1100*(t/seconds),rr=std::exp(-1/(.003*Rate));
        if(t>=next) { open=t+.003+.005*r.u(); next=t+.004+.011*r.u(); }
        gate+=((t<open?1:.15)-gate)*.05;
        double y=r.n()+2*rr*std::cos(2*Pi*f/Rate)*y1-rr*rr*y2; y2=y1; y1=y;
        b[i]=y*.04*gate*std::min(1.0,t/.01)*std::exp(-t/(seconds*.5));
    }
    return b;
}
// A chunk of meat hitting the floor: short heavy thump, wet slap, beads.
Buffer slap(Rng &r) {
    Buffer b=thump(r,240,150,.022),low(at(.05));
    double y=0; for(size_t i=0;i<low.size();++i) { y+=.25*(r.n()-y); low[i]=y*1.6*std::exp(-double(i)/Rate/.012); }
    mix(b,0,low,1); mix(b,.004,splatter(r,.08,160),.35);
    return b;
}
// Arterial spurt: one systolic stroke of pressurized blood, a band-limited
// hiss with a gurgling 30-45 Hz flutter, and the stream's drops landing.
Buffer spurt(Rng &r) {
    Buffer b(at(.16)); double y1=0,y2=0,flutter=30+15*r.u(),f=1500+1500*r.u(),rr=std::exp(-1/(.0015*Rate));
    for(size_t i=0;i<b.size();++i) {
        double t=double(i)/Rate,env=std::min(1.0,t/.005)*std::exp(-t/.05);
        double y=r.n()+2*rr*std::cos(2*Pi*f/Rate)*y1-rr*rr*y2; y2=y1; y1=y;
        b[i]=y*.03*env*(.6+.4*std::sin(2*Pi*flutter*t));
    }
    mix(b,.05,splatter(r,.12,140),.5);
    return b;
}
// A drop landing in blood: the "plop" is the air cavity's resonance
// rising (~500 to ~1300 Hz in 30 ms) after a soft contact tick.
Buffer drip(Rng &r) {
    Buffer b(at(.07)); double phase=0,f0=450+150*r.u();
    for(size_t i=0;i<b.size();++i) {
        double t=double(i)/Rate;
        phase+=2*Pi*(f0+850*(1-std::exp(-t/.012)))/Rate;
        b[i]=std::sin(phase)*std::min(1.0,t/.002)*std::exp(-t/.022)*.6;
    }
    mix(b,0,tick(r),.25);
    return b;
}
}

std::vector<float> build(Kind kind,int variant,const float *slop,size_t count,unsigned slopRate) {
    Rng r{0x9e3779b9u*uint32_t(kind*Variants+variant+1)};
    Buffer out;
    double v=variant;
    switch(kind) {
    case Hit:
        mix(out,0,tick(r),.5); mix(out,.002,squelch(r,slop,count,slopRate,.92+.08*v,.14),.8);
        mix(out,0,thump(r,175,110,.03),.5); mix(out,.02,splatter(r,.12,120),.3); break;
    case Heavy:
        mix(out,0,tick(r),.7); mix(out,.002,squelch(r,slop,count,slopRate,.85+.07*v,.18),.9);
        mix(out,0,thump(r,165,100,.045),.8); mix(out,.025,splatter(r,.22,220),.5);
        if(variant&1) mix(out,.004,crack(r,2),.4); break;
    case Kill:
        mix(out,0,tick(r),.8); mix(out,.002,squelch(r,slop,count,slopRate,.9+.06*v,.2),.95);
        mix(out,0,thump(r,155,95,.06),1); mix(out,.006,crack(r,3+variant),.9);
        mix(out,.03,splatter(r,.35,260),.65); mix(out,.09,squelch(r,slop,count,slopRate,.7,.22),.5); break;
    case Gib:
        mix(out,0,thump(r,140,90,.08),1); mix(out,.01,rip(r,.35+.05*v),.9);
        for(double when:{0.0,.05,.12}) mix(out,when,crack(r,3+variant),.8);
        mix(out,0,squelch(r,slop,count,slopRate,.65,.3),.8); mix(out,.04,splatter(r,.55,320),.85);
        mix(out,.18+.02*v,slap(r),.6); break;
    case Slap: mix(out,0,slap(r),1); break;
    case Spurt: mix(out,0,spurt(r),1); break;
    case Drip: mix(out,0,drip(r),1); break;
    default: break;
    }
    // Normalize, then a gentle saturation for density at full level; 1 ms
    // rise and 10 ms fall keep the edges silent.
    double peak=1e-9; for(double x:out) peak=std::max(peak,std::fabs(x));
    std::vector<float> pcm(out.size());
    const double drive=1.4,norm=std::tanh(drive),fall=.010*Rate;
    for(size_t i=0;i<out.size();++i) {
        double g=std::min(1.0,i/44.0),tail=double(out.size()-1-i);
        if(tail<fall) g*=tail/fall;
        pcm[i]=float(std::tanh(drive*out[i]/peak)/norm*.85*g);
    }
    // Level by kind, measured like the WAD sounds (loudest 50 ms RMS, dBFS)
    // so every variant sits at the same place under the weapons (pistol
    // about -8): a hit stays under the shot that caused it, a kill or gib
    // reads above a hit, and drips are quiet ambience.
    static constexpr double Target[Kinds]={-15,-13,-11.5,-10,-17,-18,-30};
    size_t w=Rate/20; double e=0,best=0;
    for(size_t i=0;i<pcm.size();++i) { e+=double(pcm[i])*pcm[i]; if(i>=w) e-=double(pcm[i-w])*pcm[i-w]; best=std::max(best,e); }
    double level=std::sqrt(std::max(best,1e-18)/double(std::min(w,std::max<size_t>(pcm.size(),1))));
    double gain=std::pow(10.0,Target[kind]/20)/level,top=0;
    for(float x:pcm) top=std::max(top,std::fabs(double(x)));
    gain=std::min(gain,.9/std::max(top,1e-9));
    for(float &x:pcm) x=float(x*gain);
    return pcm;
}
}
