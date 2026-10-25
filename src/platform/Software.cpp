// SPDX-License-Identifier: GPL-2.0-only
#include "WolfNative.hpp"
#include "Parallel.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
Wolf_Surface *Wolf_CreateRGBSurface(int,int w,int h,int bits,Uint32,Uint32,Uint32,Uint32) {
    if(w<=0 || h<=0 || w>4096 || h>2560 || (bits!=8 && bits!=32)) return nullptr;
    auto *s=new(std::nothrow) Wolf_Surface{w,h,w*(bits/8),nullptr,nullptr};
    if(!s)return nullptr;
    s->format=new(std::nothrow) Wolf_PixelFormat{bits/8,nullptr};
    s->pixels=calloc(size_t(h),s->pitch);
    if(!s->format || !s->pixels){delete s->format;free(s->pixels);delete s;return nullptr;}
    return s;
}
void Wolf_FreeSurface(Wolf_Surface *s) { if(s) { delete s->format->palette; delete s->format; free(s->pixels); delete s; } }
int Wolf_LockSurface(Wolf_Surface *) { return 0; } void Wolf_UnlockSurface(Wolf_Surface *) {}
Wolf_Palette *Wolf_AllocPalette(int count) { return count==256?new(std::nothrow) Wolf_Palette:nullptr; }
void Wolf_FreePalette(Wolf_Palette *p) { delete p; }
int Wolf_SetPaletteColors(Wolf_Palette *p,const Wolf_Color *c,int first,int count) { if(!p || !c || first<0 || first>256 || count<0 || count>256-first) return -1; std::copy_n(c,count,p->colors+first); return 0; }
int Wolf_SetSurfacePalette(Wolf_Surface *s,Wolf_Palette *p) { if(!s || !s->format || !p) return -1; if(!s->format->palette) s->format->palette=new(std::nothrow) Wolf_Palette; if(!s->format->palette)return -1;*s->format->palette=*p; return 0; }
Uint32 Wolf_MapRGB(Wolf_PixelFormat *,Uint8 r,Uint8 g,Uint8 b) { return uint32_t(r)|uint32_t(g)<<8|uint32_t(b)<<16|0xff000000u; }
int Wolf_FillRect(Wolf_Surface *s,const void *,Uint32 color) {
    if(!s || !s->format || !s->pixels) return -1;
    if(s->format->BytesPerPixel==1) memset(s->pixels,color,size_t(s->h)*s->pitch);
    else std::fill_n(static_cast<Uint32 *>(s->pixels),size_t(s->w)*s->h,color);
    return 0;
}
Wolf_Surface *Wolf_PresentSurface=nullptr;
Wolf_IndexFrame Wolf_PendingFrame;
bool wolfViewFrame=false;
int Wolf_BlitSurface(Wolf_Surface *a,const void *,Wolf_Surface *b,void *) {
    if(!a || !b || !a->format || !b->format || !a->pixels || !b->pixels || a->w!=b->w || a->h!=b->h) return -1;
    if(a==b)return 0;
    if(a->format->BytesPerPixel==b->format->BytesPerPixel) { memcpy(b->pixels,a->pixels,size_t(a->h)*a->pitch); return 0; }
    if(a->format->BytesPerPixel!=1 || !a->format->palette || b->format->BytesPerPixel!=4) return -1;
    auto *src=static_cast<const Uint8 *>(a->pixels); auto *dest=static_cast<Uint32 *>(b->pixels);
    const bool present=b==Wolf_PresentSurface;
    // Display colors once per blit.
    Uint32 colors[256];
    for(int i=0;i<256;++i) {
        auto c=a->format->palette->colors[i];
        colors[i]=Wolf_MapRGB(b->format,c.r,c.g,c.b);
    }
    if(present) {
        // Only the indices are copied; the shader applies the colors.
        Wolf_IndexFrame &f=Wolf_PendingFrame;
        f.indices.resize(size_t(a->w)*a->h); f.width=a->w; f.height=a->h;
        memcpy(f.colors,colors,sizeof(colors)); f.fresh=true; f.view=wolfViewFrame;
        constexpr int Rows=128;
        parallel::forEach(size_t((a->h+Rows-1)/Rows),[&](size_t band) {
            for(int y=int(band)*Rows;y<std::min(a->h,int(band+1)*Rows);++y) memcpy(f.indices.data()+size_t(y)*a->w,src+size_t(y)*a->pitch,size_t(a->w));
        });
        return 0;
    }
    // Bands of rows across the cores; every pixel is independent.
    constexpr int Band=64;
    parallel::forEach(size_t((a->h+Band-1)/Band),[&](size_t band) {
        size_t begin=band*Band*size_t(a->w),end=std::min(size_t(a->h),(band+1)*Band)*size_t(a->w);
        for(size_t i=begin;i<end;++i) dest[i]=colors[src[i]];
    });
    return 0;
}
int Wolf_PixelFormatEnumToMasks(int,int *bits,Uint32 *r,Uint32 *g,Uint32 *b,Uint32 *a) { *bits=32; *r=0xff; *g=0xff00; *b=0xff0000; *a=0xff000000; return 1; }
int Wolf_SetHint(const char *,const char *) { return 1; }
int Wolf_SaveBMP(Wolf_Surface *s,const char *path) {
    if(!s || !s->pixels || !s->format || !path || (s->format->BytesPerPixel==1 && !s->format->palette)) return -1;
    FILE *f=fopen(path,"wb"); if(!f) return -1;
    uint32_t stride=(s->w*3+3)&~3,bytes=stride*s->h;
    Uint8 header[54]={'B','M'};
    auto put=[&](int p,uint32_t v) { for(int i=0;i<4;++i) header[p+i]=v>>(8*i); };
    put(2,54+bytes); put(10,54); put(14,40); put(18,s->w); put(22,s->h); header[26]=1; header[28]=24; put(34,bytes);
    bool ok=fwrite(header,1,54,f)==54; std::vector<Uint8> row(stride);
    for(int y=s->h-1;y>=0;--y) {
        for(int x=0;x<s->w;++x) {
            uint32_t color;
            if(s->format->BytesPerPixel==1) { auto c=s->format->palette->colors[static_cast<Uint8 *>(s->pixels)[y*s->pitch+x]]; color=Wolf_MapRGB(nullptr,c.r,c.g,c.b); }
            else color=static_cast<Uint32 *>(s->pixels)[y*s->w+x];
            row[x*3]=color>>16; row[x*3+1]=color>>8; row[x*3+2]=color;
        }
        ok&=fwrite(row.data(),1,stride,f)==stride;
    }
    ok&=fclose(f)==0; return ok?0:-1;
}
Wolf_mutex *Wolf_CreateMutex() { return new(std::nothrow) Wolf_mutex; }
void Wolf_DestroyMutex(Wolf_mutex *m) { delete m; }
int Wolf_LockMutex(Wolf_mutex *m) { if(m) m->lock(); return 0; }
int Wolf_UnlockMutex(Wolf_mutex *m) { if(m) m->unlock(); return 0; }
int Wolf_BuildAudioCVT(Wolf_AudioCVT *c,Uint16 source,int sourceChannels,int sourceRate,Uint16 target,int targetChannels,int targetRate) {
    if(!c || source!=AUDIO_U8 || target!=AUDIO_S16SYS || sourceChannels!=1 || targetChannels!=2 || sourceRate<1000 || sourceRate>192000 || targetRate<8000 || targetRate>192000) return -1;
    c->sourceRate=sourceRate; c->targetRate=targetRate; c->len_mult=int(std::ceil(targetRate/double(sourceRate)))*4+4; return 1;
}
int Wolf_ConvertAudio(Wolf_AudioCVT *c) {
    if(!c || c->len<0 || !c->buf || c->sourceRate<=0 || c->targetRate<=0 || c->len_mult<=0) return -1;
    c->len_cvt=0;
    if(!c->len) { c->len_cvt=0;return 0; }
    uint64_t frameCount=(uint64_t(c->len)*unsigned(c->targetRate)+unsigned(c->sourceRate)-1)/unsigned(c->sourceRate);
    if(frameCount>unsigned(std::numeric_limits<int>::max()/4) || frameCount*4>uint64_t(c->len)*unsigned(c->len_mult)) return -1;
    size_t frames=size_t(frameCount);
    std::vector<Uint8> original;
    try { original.assign(c->buf,c->buf+c->len); }catch(const std::bad_alloc &){return -1;}
    // The 7 kHz 8-bit recordings are upsampled by linear interpolation: holding
    // each sample until the next leaves stair steps whose images above 3.5 kHz
    // sound as a harsh crackle. DC offset is removed and 1 ms / 4 ms edge fades
    // keep starts and stops from clicking.
    double mean=0; for(Uint8 s:original) mean+=s; mean/=double(original.size());
    auto *pcm=reinterpret_cast<int16_t *>(c->buf);
    double step=double(c->sourceRate)/c->targetRate; size_t fadeIn=size_t(c->targetRate*.001),fadeOut=size_t(c->targetRate*.004);
    for(size_t i=0;i<frames;++i) {
        double position=i*step; size_t at=std::min(size_t(position),original.size()-1),next=std::min(at+1,original.size()-1);
        double fraction=std::min(1.0,position-double(at));
        double value=(original[at]*(1-fraction)+original[next]*fraction-mean)*256;
        if(i<fadeIn) value*=double(i)/fadeIn;
        if(frames-i<=fadeOut) value*=double(frames-i-1)/fadeOut;
        pcm[i*2]=pcm[i*2+1]=int16_t(std::clamp(std::lround(value),-32768L,32767L));
    }
    c->len_cvt=int(frames*4); return 0;
}
