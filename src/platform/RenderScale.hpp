// SPDX-License-Identifier: GPL-2.0-only
// Adaptive render resolution (2026-10-11). A frame's
// CPU work is modelled as a fixed part plus a part that grows with the pixel
// count, both learned from the frames just measured. When frames no longer
// finish within their share of the refresh interval (the heaviest gore,
// thermal throttling) the render scale drops straight to the largest size
// that fits: after a frame that nearly missed its refresh, or two slow frames
// out of three. It climbs back toward native one step at a time, and only
// after a full second of frames that would also have fitted the larger size,
// so it never oscillates. Thermal pressure and Low Power Mode shrink the
// share, so the Mac does less work per frame and cools down. On a display
// faster than 60 Hz a steady 60 frames per second is preferred to giving up
// resolution.
#pragma once
#include <algorithm>
#include <array>

namespace native_render {
class RenderScale {
public:
    // Sixteenths of native size per axis: 16 is native, 8 is half (a quarter
    // of the pixels), never lower.
    static constexpr int Native=16,Lowest=8;
    int sixteenths() const { return size; }
    double scale() const { return size/16.0; }
    // Refresh interval frames are paced to: the display's own, or twice it
    // (never slower than 60 Hz) while a fast display cannot be kept up with.
    double intervalMs() const { return halved?2*refresh:refresh; }
    // Share of the interval a frame may use, by ProcessInfo.ThermalState
    // (nominal, fair, serious, critical); Low Power Mode counts as serious.
    static double budgetShare(int thermal,bool lowPower) {
        static constexpr double share[4]={.72,.66,.55,.42};
        int level=std::clamp(thermal,0,3);
        if(lowPower) level=std::max(level,2);
        return share[level];
    }
    // After a level starts or the view changes size: the next frames carry
    // loading and allocation work, so they are not measured.
    void settle(int frames) { quiet=std::max(quiet,frames); }
    // One rendered frame: workMs is its CPU time, scaledMs the part of it that
    // grows with the pixel count (the software renderer), displayMs the
    // display's refresh interval. Returns true when the size or the pacing
    // interval changed.
    bool frame(double workMs,double scaledMs,double displayMs,int thermal,bool lowPower) {
        if(displayMs<=0) return false;
        if(refresh!=displayMs) { refresh=displayMs; halved=false; calm=0; }
        if(quiet>0) { --quiet; return false; }
        // The automap and other frames without a rendered view say nothing
        // about the renderer's cost.
        if(workMs<=0 || scaledMs<=0) return false;
        double share=budgetShare(thermal,lowPower),budget=intervalMs()*share;
        // A one-off stall outside the renderer (loading, saving) that alone
        // overruns the frame is no reason to give up resolution.
        if(workMs-scaledMs>budget) return false;
        double area=scale()*scale();
        unsigned slot=count++%History;
        fixed[slot]=std::max(0.0,workMs-scaledMs);
        perArea[slot]=std::min(scaledMs,workMs)/area;
        unsigned held=std::min<unsigned>(count,History);
        // Fixed work comes in bursts (70 Hz game tics between display frames),
        // so its peak over the last frames is planned for; the pixel cost of
        // the last few frames reacts to what is on screen now.
        double fixedPeak=*std::max_element(fixed.begin(),fixed.begin()+held),recent=0;
        for(unsigned k=0;k<std::min(held,4u);++k) recent=std::max(recent,perArea[(slot+History-k)%History]);
        auto cost=[&](int sixteenths,double native) { double k=sixteenths/16.0; return fixedPeak+native*k*k; };
        // A lone frame between the budget and the refresh interval is still
        // shown in time; only a near miss or a run of slow frames counts.
        overruns=(overruns<<1|(workMs>budget))&7;
        bool urgent=workMs>intervalMs()*.9,sustained=(overruns&1) && (overruns>>1&1 || overruns>>2&1);
        if(urgent || sustained) {
            overruns=0;
            // A fast display that cannot be kept up with at native size paces
            // at half its rate first; resolution is only given up after that.
            if(!halved && size==Native && refresh*2<=1000/60.0+.01) { halved=true; reset(); return true; }
            int fit=size;
            while(fit>Lowest && cost(fit,recent)>budget) --fit;
            calm=0; calmPeak=0;
            if(fit==size) return false;
            size=fit; quiet=2;
            return true;
        }
        calmPeak=std::max(calmPeak,perArea[slot]);
        int second=int(1000/intervalMs());
        if(size<Native) {
            if(cost(size+1,calmPeak)>budget*.85) { calm=0; calmPeak=0; return false; }
            if(++calm<second) return false;
            ++size; calm=0; calmPeak=0; quiet=2;
            return true;
        }
        // Back to the display's own rate once native frames would fit it, with
        // room to spare, for three seconds.
        if(halved) {
            if(cost(Native,calmPeak)>refresh*share*.85) { calm=0; calmPeak=0; return false; }
            if(++calm<3*second) return false;
            halved=false; reset();
            return true;
        }
        return false;
    }
private:
    static constexpr unsigned History=16;
    // A new pacing interval starts its measurements afresh.
    void reset() { count=0; calm=0; calmPeak=0; quiet=2; overruns=0; }
    std::array<double,History> fixed{},perArea{};
    unsigned count=0;
    int size=Native,calm=0,quiet=0;
    unsigned overruns=0; // the last three frames, newest in bit 0: over budget
    double refresh=0,calmPeak=0;
    bool halved=false;
};
}
