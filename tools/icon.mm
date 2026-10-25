// SPDX-License-Identifier: GPL-2.0-only
// MetalWolf's app icon, drawn here and nowhere else: an original pixel-art
// wolf's head with yellow eyes on a deep blue tile. Writes the PNGs of a macOS
// .iconset into the folder given on the command line (see the Makefile).
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>

// '#' fur, 'i' inner ear, 'm' muzzle, 'e' eye, 'n' nose; an empty cell next
// to the art is its outline.
static const char *art[16]={
    "................",
    "..#..........#..",
    "..##........##..",
    "..#i#......#i#..",
    "..#ii#....#ii#..",
    "..############..",
    ".##############.",
    ".###ee####ee###.",
    ".##############.",
    "..#####mm#####..",
    "...###mmmm###...",
    "....##mmmm##....",
    ".....#mmmm#.....",
    "......mnnm......",
    ".......mm.......",
    "................",
};
struct Color { double r,g,b,a; };
static const Color top={.11,.24,.62,1},bottom={.02,.05,.20,1},glow={.45,.68,1,.30};
static const Color light={.86,.88,.90,1},mid={.64,.68,.73,1},shade={.43,.47,.53,1},edge={.02,.03,.10,1};
static const Color eye={1,.83,.23,1},nose={.07,.07,.08,1},ear={.78,.52,.52,1},muzzle={.93,.92,.88,1};

static bool filled(int r,int c) { return r>=0 && r<16 && c>=0 && c<16 && art[r][c]!='.'; }
static void fill(CGContextRef g,Color c,CGRect rect) { CGContextSetRGBFillColor(g,c.r,c.g,c.b,c.a); CGContextFillRect(g,rect); }

static void draw(CGContextRef g,double s) {
    CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    // The standard macOS tile: 824 of 1024 with a soft shadow below.
    CGRect tile=CGRectMake(100*s,100*s,824*s,824*s);
    CGPathRef shape=CGPathCreateWithRoundedRect(tile,185*s,185*s,nullptr);
    CGContextSaveGState(g);
    CGColorRef shadow=CGColorCreateSRGB(0,0,0,.45);
    CGContextSetShadowWithColor(g,CGSizeMake(0,-10*s),24*s,shadow);
    CGContextAddPath(g,shape); CGContextSetRGBFillColor(g,bottom.r,bottom.g,bottom.b,1); CGContextFillPath(g);
    CGContextRestoreGState(g); CGColorRelease(shadow);
    CGContextSaveGState(g);
    CGContextAddPath(g,shape); CGContextClip(g);
    CGFloat ramp[]={bottom.r,bottom.g,bottom.b,1,top.r,top.g,top.b,1};
    CGGradientRef gradient=CGGradientCreateWithColorComponents(space,ramp,nullptr,2);
    CGContextDrawLinearGradient(g,gradient,CGPointMake(0,100*s),CGPointMake(0,924*s),0); CGGradientRelease(gradient);
    CGFloat halo[]={glow.r,glow.g,glow.b,glow.a,glow.r,glow.g,glow.b,0};
    gradient=CGGradientCreateWithColorComponents(space,halo,nullptr,2);
    CGContextDrawRadialGradient(g,gradient,CGPointMake(512*s,500*s),0,CGPointMake(512*s,500*s),400*s,0); CGGradientRelease(gradient);
    // The art on whole device pixels, so every cell stays a sharp square.
    double cell=36*s,origin=224*s;
    auto at=[&](int i) { return std::round(origin+i*cell); };
    for(int r=0;r<16;++r) for(int c=0;c<16;++c) {
        CGRect rect=CGRectMake(at(c),at(16-r-1),at(c+1)-at(c),at(16-r)-at(16-r-1));
        char k=art[r][c];
        if(k=='.') {
            bool near=false;
            for(int dr=-1;dr<=1;++dr) for(int dc=-1;dc<=1;++dc) near|=filled(r+dr,c+dc);
            if(near) fill(g,edge,rect);
        } else if(k=='e') fill(g,eye,rect);
        else if(k=='n') fill(g,nose,rect);
        else if(k=='i') fill(g,ear,rect);
        else if(k=='m') fill(g,muzzle,rect);
        else fill(g,!filled(r-1,c-1)?light:!filled(r+1,c+1)?shade:mid,rect);
    }
    CGContextRestoreGState(g);
    // A faint rim of light along the edge of the tile.
    CGContextAddPath(g,shape); CGContextSetRGBStrokeColor(g,1,1,1,.10); CGContextSetLineWidth(g,std::max(1.0,2*s)); CGContextStrokePath(g);
    CGPathRelease(shape); CGColorSpaceRelease(space);
}

int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=2) { fprintf(stderr,"usage: icon <folder.iconset>\n"); return 1; }
    NSString *folder=[NSString stringWithUTF8String:argv[1]];
    [NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:YES attributes:nil error:nil];
    for(int points:{16,32,128,256,512}) for(int scale=1;scale<=2;++scale) {
        int pixels=points*scale;
        CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef g=CGBitmapContextCreate(nullptr,pixels,pixels,8,0,space,kCGImageAlphaPremultipliedLast);
        CGContextSetInterpolationQuality(g,kCGInterpolationNone);
        draw(g,pixels/1024.0);
        CGImageRef image=CGBitmapContextCreateImage(g);
        NSString *name=scale==1?[NSString stringWithFormat:@"icon_%dx%d.png",points,points]:[NSString stringWithFormat:@"icon_%dx%d@2x.png",points,points];
        NSURL *url=[NSURL fileURLWithPath:[folder stringByAppendingPathComponent:name]];
        CGImageDestinationRef out=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,CFSTR("public.png"),1,nullptr);
        CGImageDestinationAddImage(out,image,nullptr);
        bool ok=CGImageDestinationFinalize(out);
        CFRelease(out); CGImageRelease(image); CGContextRelease(g); CGColorSpaceRelease(space);
        if(!ok) { fprintf(stderr,"icon: cannot write %s\n",url.path.UTF8String); return 1; }
    }
    return 0;
} }
