// SPDX-License-Identifier: GPL-2.0-only
#import <AudioToolbox/AudioToolbox.h>
#include "WolfNative.hpp"
#include "GoreSound.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
namespace {
AudioUnit output=nullptr;
std::recursive_mutex mixerMutex;
struct Voice { WolfMix_Chunk *chunk=nullptr; unsigned offset=0; Uint8 left=255,right=255; uint64_t order=0; int group=0; };
std::array<Voice,MIX_CHANNELS> voices;
std::array<bool,MIX_CHANNELS> pendingFinished{};
uint64_t serial=0;
int reserved=0,rate=44100;
bool suspended=false;
void (*music)(void *,Uint8 *,int)=nullptr; void *musicContext=nullptr;
void (*postMix)(void *,Uint8 *,int)=nullptr; void *postContext=nullptr;
void (*finished)(int)=nullptr;
// Native menu cues (2026-10-10): short sounds synthesized at the output rate,
// mixed on voices of their own so quick steps overlap without cutting off.
struct Cue { const std::vector<int16_t> *pcm=nullptr; size_t offset=0; };
std::array<Cue,4> cues;
unsigned nextCue=0;
std::array<std::vector<int16_t>,WOLF_CUE_COUNT> cueTones;
int cueRate=0;
// Gore events (GoreSound.cpp) at the output rate, each with its own pan.
struct GoreCue { const std::vector<int16_t> *pcm=nullptr; size_t offset=0; int left=0,right=0; };
std::array<GoreCue,8> goreCues;
unsigned nextGore=0;
std::array<std::vector<int16_t>,gore_sound::Kinds*gore_sound::Variants> goreTones;
std::array<int,gore_sound::Kinds> lastGore;
uint32_t goreSeed=0x2545f491u;
int goreRate=0;
// Every cue is mechanical rather than melodic: a short noise burst rung
// through a damped resonator (the click) over a low thump that falls in pitch
// (the body). Steps and value changes are dry switch clicks; select is a
// two-stage latch that lands on a heavier thump; back is one softer thump;
// deny is two dull, muffled knocks. Entering play racks a bolt (back, then
// home) onto a deep thump; quitting is one low thump that rings down.
std::vector<int16_t> synthesizeCue(int kind,int sampleRate) {
    struct Hit { double at,ring,ringDecay,ringLevel,body,bodyDecay,bodyLevel; };
    std::vector<Hit> hits; double length;
    switch(kind) {
        case WOLF_CUE_MOVE: hits={{0,2400,.006,.2,190,.012,.1}}; length=.05; break;
        case WOLF_CUE_ADJUST: hits={{0,1800,.007,.2,160,.014,.12}}; length=.06; break;
        case WOLF_CUE_SELECT: hits={{0,1500,.008,.18,0,0,0},{.032,950,.02,.22,95,.07,.26}}; length=.22; break;
        case WOLF_CUE_BACK: hits={{0,1100,.012,.16,120,.05,.2}}; length=.16; break;
        case WOLF_CUE_START: hits={{0,2000,.006,.2,0,0,0},{.09,1300,.012,.24,0,0,0},{.12,700,.03,.18,70,.18,.32}}; length=.55; break;
        case WOLF_CUE_QUIT: hits={{0,600,.04,.14,60,.35,.34}}; length=.8; break;
        default: hits={{0,420,.02,.16,85,.04,.26},{.085,400,.02,.16,80,.05,.26}}; length=.24; break;
    }
    std::vector<float> mix(size_t(length*sampleRate));
    uint32_t seed=0x9e3779b9u+uint32_t(kind);
    for(const Hit &hit:hits) {
        size_t start=size_t(hit.at*sampleRate);
        std::vector<double> ring(mix.size()-start);
        double r=std::exp(-1/(hit.ringDecay*sampleRate)),c=2*r*std::cos(2*M_PI*hit.ring/sampleRate),y1=0,y2=0,peak=1e-9;
        size_t burst=size_t(.0015*sampleRate);
        for(size_t i=0;i<ring.size();++i) {
            seed=seed*1664525u+1013904223u;
            double x=i<burst?(double(seed>>8)/8388608.0-1)*(1-double(i)/burst):0;
            double y=x+c*y1-r*r*y2; y2=y1; y1=y; ring[i]=y; peak=std::max(peak,std::fabs(y));
        }
        double phase=0;
        for(size_t i=0;i<ring.size();++i) {
            double t=double(i)/sampleRate,body=0;
            if(hit.bodyLevel>0) {
                phase+=2*M_PI*hit.body*(1+.6*std::exp(-t/.012))/sampleRate;
                body=hit.bodyLevel*std::min(1.0,t/.001)*std::exp(-t/hit.bodyDecay)*std::sin(phase);
            }
            mix[start+i]+=float(hit.ringLevel*ring[i]/peak+body);
        }
    }
    size_t fade=std::min(mix.size(),size_t(.005*sampleRate));
    for(size_t i=0;i<fade;++i) mix[mix.size()-1-i]*=float(i)/fade;
    std::vector<int16_t> pcm(mix.size());
    for(size_t i=0;i<mix.size();++i) pcm[i]=int16_t(std::lround(std::clamp(mix[i],-1.f,1.f)*32767));
    return pcm;
}
// The engine's completion hook touches positional-sound state owned by the
// game thread. Deliver it there, before a completed channel can be reused.
void deliverFinished() {
    std::array<bool,MIX_CHANNELS> completed{};
    void (*hook)(int)=nullptr;
    {
        std::lock_guard<std::recursive_mutex> lock(mixerMutex);
        completed=pendingFinished;pendingFinished={};hook=finished;
    }
    if(hook)for(int channel=0;channel<MIX_CHANNELS;++channel)if(completed[channel])hook(channel);
}
void resumeOutput() { if(output && !suspended)AudioOutputUnitStart(output); }
OSStatus render(void *,AudioUnitRenderActionFlags *,const AudioTimeStamp *,UInt32,UInt32 frames,AudioBufferList *data) {
    if(!data)return kAudio_ParamError;
    for(unsigned b=0;b<data->mNumberBuffers;++b)if(data->mBuffers[b].mData)memset(data->mBuffers[b].mData,0,data->mBuffers[b].mDataByteSize);
    if(data->mNumberBuffers!=1 || data->mBuffers[0].mNumberChannels!=2 || !data->mBuffers[0].mData || frames>data->mBuffers[0].mDataByteSize/4) return kAudio_ParamError;
    auto *pcm=static_cast<int16_t *>(data->mBuffers[0].mData);
    // Hooks may take the engine's audio mutex; never hold mixerMutex around them.
    if(music) music(musicContext,reinterpret_cast<Uint8 *>(pcm),frames*4);
    // Voices sum at full precision and clip at the output limit, as the original
    // mixer does.
    static int32_t sum[2*4096];
    for(unsigned start=0;start<frames;start+=4096) {
        unsigned count=std::min(frames-start,4096u); int16_t *out=pcm+start*2;
        for(unsigned i=0;i<count*2;++i) sum[i]=out[i];
        {
            std::lock_guard<std::recursive_mutex> lock(mixerMutex);
            for(int channel=0;channel<MIX_CHANNELS;++channel) {
                auto &v=voices[channel]; if(!v.chunk) continue;
                const auto *sample=reinterpret_cast<const int16_t *>(v.chunk->abuf);
                for(unsigned i=0;i<count;++i) {
                    if(v.offset>v.chunk->alen || v.chunk->alen-v.offset<4) { v.chunk=nullptr; pendingFinished[channel]=true; break; }
                    for(unsigned side=0;side<2;++side) sum[i*2+side]+=int(sample[v.offset/2+side])*(side?v.right:v.left)*v.chunk->volume/(255*MIX_MAX_VOLUME);
                    v.offset+=4;
                }
                if(v.chunk && v.offset==v.chunk->alen) { v.chunk=nullptr;pendingFinished[channel]=true; }
            }
            for(auto &cue:goreCues) {
                if(!cue.pcm) continue;
                for(unsigned i=0;i<count && cue.offset<cue.pcm->size();++i,++cue.offset) { int x=(*cue.pcm)[cue.offset]; sum[i*2]+=x*cue.left>>8; sum[i*2+1]+=x*cue.right>>8; }
                if(cue.offset>=cue.pcm->size()) cue.pcm=nullptr;
            }
            for(auto &cue:cues) {
                if(!cue.pcm) continue;
                for(unsigned i=0;i<count && cue.offset<cue.pcm->size();++i,++cue.offset) { int x=(*cue.pcm)[cue.offset]; sum[i*2]+=x; sum[i*2+1]+=x; }
                if(cue.offset>=cue.pcm->size()) cue.pcm=nullptr;
            }
        }
        // Overlapping full-scale sounds and music bend smoothly into the output
        // limit above 3/4 of full scale instead of clipping hard (crackle).
        for(unsigned i=0;i<count*2;++i) {
            int x=sum[i],m=std::abs(x);
            if(m>24576) { double over=double(m-24576)/8192; m=24576+int(8191*over/(1+over)); x=x<0?-m:m; }
            out[i]=int16_t(std::clamp(x,-32768,32767));
        }
    }
    if(postMix) postMix(postContext,reinterpret_cast<Uint8 *>(pcm),frames*4);
    return noErr;
}
}
void Wolf_NativeAudioShutdown() {
    if(output) { AudioOutputUnitStop(output); AudioUnitUninitialize(output); AudioComponentInstanceDispose(output); output=nullptr; }
    music=nullptr;musicContext=nullptr;postMix=nullptr;postContext=nullptr;finished=nullptr;
    std::lock_guard<std::recursive_mutex> lock(mixerMutex);voices={};pendingFinished={};reserved=0;serial=0;cues={};goreCues={};
}
void Wolf_NativeMenuCue(int kind) {
    if(!output || suspended || kind<0 || kind>=WOLF_CUE_COUNT) return;
    std::lock_guard<std::recursive_mutex> lock(mixerMutex);
    if(cueRate!=rate) { cues={}; for(int k=0;k<WOLF_CUE_COUNT;++k) cueTones[size_t(k)]=synthesizeCue(k,rate); cueRate=rate; }
    // Entering play replaces the ordinary click that announced the choice.
    const auto *start=&cueTones[WOLF_CUE_START];
    if(kind==WOLF_CUE_START) cues={};
    else if(kind==WOLF_CUE_SELECT) for(const auto &cue:cues) if(cue.pcm==start) return;
    cues[nextCue++%cues.size()]={&cueTones[size_t(kind)],0};
}
// left/right: 0..256 gains from the caller's distance and direction.
void Wolf_NativeGoreSound(int kind,int left,int right) {
    if(!output || suspended || kind<0 || kind>=gore_sound::Kinds || (left<=0 && right<=0)) return;
    std::lock_guard<std::recursive_mutex> lock(mixerMutex);
    if(goreRate!=rate) {
        goreCues={}; lastGore.fill(-1);
        for(int k=0;k<gore_sound::Kinds;++k) for(int v=0;v<gore_sound::Variants;++v) {
            std::vector<float> pcm=gore_sound::build(gore_sound::Kind(k),v,nullptr,0,0);
            auto &tone=goreTones[size_t(k*gore_sound::Variants+v)];
            double step=double(gore_sound::Rate)/rate; tone.assign(size_t(pcm.size()/step),0);
            for(size_t i=0;i<tone.size();++i) {
                double p=i*step; size_t j=size_t(p); double f=p-j;
                float x=j+1<pcm.size()?pcm[j]*float(1-f)+pcm[j+1]*float(f):pcm[j];
                tone[i]=int16_t(std::lround(std::clamp(x,-1.f,1.f)*32767*.8f));
            }
        }
        goreRate=rate;
    }
    goreSeed=goreSeed*1664525u+1013904223u;
    int &last=lastGore[size_t(kind)],variant=int((goreSeed>>8)%unsigned(gore_sound::Variants-1)); if(variant>=last) ++variant;
    last=variant;
    goreCues[nextGore++%goreCues.size()]={&goreTones[size_t(kind*gore_sound::Variants+variant)],0,std::min(left,256),std::min(right,256)};
}
int WolfMix_OpenAudioDevice(int requested,Uint16 format,int channels,int,const char *,int) {
    if(format!=AUDIO_S16SYS || channels!=2 || requested<8000 || requested>192000) return -1;
    if(output)Wolf_NativeAudioShutdown();
    rate=requested;
    AudioComponentDescription description{kAudioUnitType_Output,kAudioUnitSubType_DefaultOutput,kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(nullptr,&description);
    if(!component || AudioComponentInstanceNew(component,&output)) return -1;
    AudioStreamBasicDescription stream{}; stream.mSampleRate=rate; stream.mFormatID=kAudioFormatLinearPCM;
    stream.mFormatFlags=kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked; stream.mBytesPerPacket=4; stream.mFramesPerPacket=1; stream.mBytesPerFrame=4; stream.mChannelsPerFrame=2; stream.mBitsPerChannel=16;
    AURenderCallbackStruct callback{render,nullptr};
    if(AudioUnitSetProperty(output,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&stream,sizeof(stream)) ||
       AudioUnitSetProperty(output,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof(callback)) || AudioUnitInitialize(output) || (!suspended && AudioOutputUnitStart(output))) { Wolf_NativeAudioShutdown(); return -1; }
    return 0;
}
void Wolf_NativeAudioSuspend(bool value) { suspended=value;if(output) { if(suspended) AudioOutputUnitStop(output); else resumeOutput(); } }
int WolfMix_QuerySpec(int *r,Uint16 *format,int *channels) { if(r)*r=rate;if(format)*format=AUDIO_S16SYS;if(channels)*channels=2; return output!=nullptr; }
int WolfMix_ReserveChannels(int count) { std::lock_guard<std::recursive_mutex> lock(mixerMutex);reserved=std::clamp(count,0,MIX_CHANNELS); return reserved; }
int WolfMix_GroupChannels(int first,int last,int group) { std::lock_guard<std::recursive_mutex> lock(mixerMutex);first=std::clamp(first,0,MIX_CHANNELS);last=std::clamp(last,-1,MIX_CHANNELS-1);for(int i=first;i<=last;++i) voices[i].group=group; return std::max(0,last-first+1); }
int WolfMix_GroupAvailable(int group) { deliverFinished();std::lock_guard<std::recursive_mutex> lock(mixerMutex); for(int i=reserved;i<MIX_CHANNELS;++i) if((group<0 || voices[i].group==group) && !voices[i].chunk) return i; return -1; }
int WolfMix_GroupOldest(int group) { deliverFinished();std::lock_guard<std::recursive_mutex> lock(mixerMutex); int oldest=-1; for(int i=reserved;i<MIX_CHANNELS;++i) if(voices[i].chunk && (group<0 || voices[i].group==group) && (oldest<0 || voices[i].order<voices[oldest].order)) oldest=i; return oldest; }
int WolfMix_SetPanning(int channel,Uint8 l,Uint8 r) { deliverFinished();std::lock_guard<std::recursive_mutex> lock(mixerMutex); if(channel<0 || channel>=MIX_CHANNELS) return 0; voices[channel].left=l; voices[channel].right=r; return 1; }
int WolfMix_PlayChannel(int channel,WolfMix_Chunk *chunk,int loops) {
    if(channel<0) channel=WolfMix_GroupAvailable(1);
    if(channel<0 || channel>=MIX_CHANNELS || !chunk || !chunk->abuf || chunk->alen<4 || chunk->alen%4 || loops) return -1;
    deliverFinished();
    std::lock_guard<std::recursive_mutex> lock(mixerMutex);
    // Replacing a voice that is still audible ends it. The notification clears
    // the old sound's position before the caller records the new one; a render
    // completion may already be pending for the same voice.
    if((pendingFinished[channel] || voices[channel].chunk) && finished)finished(channel);
    pendingFinished[channel]=false;voices[channel].chunk=chunk; voices[channel].offset=0; voices[channel].order=++serial; return channel;
}
int WolfMix_HaltChannel(int channel) {
    deliverFinished();
    std::array<bool,MIX_CHANNELS> stopped{};
    { std::lock_guard<std::recursive_mutex> lock(mixerMutex); for(int i=0;i<MIX_CHANNELS;++i) if(channel<0 || i==channel) { stopped[i]=voices[i].chunk!=nullptr || pendingFinished[i]; voices[i].chunk=nullptr;pendingFinished[i]=false; } }
    for(int i=0;i<MIX_CHANNELS;++i) if(stopped[i] && finished) finished(i); return 0;
}
const char *WolfMix_GetError() { return "Native AudioUnit operation failed"; }
void WolfMix_HookMusic(void (*hook)(void *,Uint8 *,int),void *user) { if(output) AudioOutputUnitStop(output); music=hook; musicContext=user; resumeOutput(); }
void WolfMix_SetPostMix(void (*hook)(void *,Uint8 *,int),void *user) { if(output) AudioOutputUnitStop(output); postMix=hook; postContext=user; resumeOutput(); }
void WolfMix_ChannelFinished(void (*hook)(int)) { deliverFinished();std::lock_guard<std::recursive_mutex> lock(mixerMutex);finished=hook; }
