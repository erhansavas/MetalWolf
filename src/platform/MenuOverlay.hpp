// SPDX-License-Identifier: GPL-2.0-only
// Native menu panel (2026-10-10), drawn into the game's own Metal frame with
// no Core Animation layers: the page is rasterized once when it changes and
// composited in the same render pass as the game image. The game owns its
// menu state and input, and passes the current page here every frame.
#pragma once
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

struct NativeMenuTheme {
    NSColor *panel,*border,*rule,*title,*body,*hint,*selector,*selectorBorder,*shade;
    NSColor *disabled,*normal,*selected;
    // Wolfenstein 3D tones: the teal-green of its sliding doors for the
    // panel, the gold of its treasure for rules and the focused row.
    static NativeMenuTheme wolf() {
        return {[NSColor colorWithRed:.0 green:.17 blue:.16 alpha:.97],[NSColor colorWithRed:.0 green:.42 blue:.38 alpha:.95],
                [NSColor colorWithRed:.84 green:.66 blue:.14 alpha:.85],[NSColor colorWithRed:.98 green:.86 blue:.36 alpha:1],
                [NSColor colorWithWhite:.86 alpha:1],[NSColor colorWithRed:.40 green:.60 blue:.56 alpha:1],
                [NSColor colorWithRed:.0 green:.36 blue:.33 alpha:.80],[NSColor colorWithRed:.88 green:.72 blue:.20 alpha:.90],
                [NSColor colorWithRed:.0 green:.05 blue:.05 alpha:.55],
                [NSColor colorWithRed:.28 green:.40 blue:.38 alpha:1],[NSColor colorWithWhite:.80 alpha:1],[NSColor colorWithRed:1 green:.90 blue:.42 alpha:1]};
    }
};

// Quitting from the menu: the window fades out, easing in, on the calling
// (main) thread while the game's quit sound plays.
inline void NativeFadeOutWindow(NSWindow *window,double seconds) {
    if(!window || !window.visible || seconds<=0) return;
    double start=NSProcessInfo.processInfo.systemUptime;
    for(double t=0;t<1;) {
        t=std::min(1.0,(NSProcessInfo.processInfo.systemUptime-start)/seconds);
        window.alphaValue=CGFloat(1-t*t);
        [CATransaction flush];
        if(t<1) [NSThread sleepForTimeInterval:1.0/120];
    }
}

struct NativeMenuPage {
    std::string identity;
    NSString *heading=nil;     // shown when there is no logo
    id logo=nil;               // optional title artwork (CGImage)
    NSString *body=nil;        // notice text; rows may follow
    struct Row { NSString *label; int status; int index; };   // status 0 = disabled
    std::vector<Row> rows;
    int selected=-1;           // Row::index of the selection
    NSString *hint=nil;
};

class NativeMenuOverlay {
public:
    enum { RowLimit=22 };
    void build(const NativeMenuTheme &value) { theme=value; }
    // format: the drawable's pixel format; sRGB targets get sRGB textures.
    bool setupMetal(id<MTLDevice> metalDevice,id<MTLLibrary> library,MTLPixelFormat format,bool encodedTarget) {
        device=metalDevice; srgbTarget=!encodedTarget;
        MTLRenderPipelineDescriptor *desc=[MTLRenderPipelineDescriptor new];
        desc.vertexFunction=[library newFunctionWithName:@"wolf_vertex"];
        desc.colorAttachments[0].pixelFormat=format;
        desc.colorAttachments[0].blendingEnabled=YES;
        desc.colorAttachments[0].sourceRGBBlendFactor=desc.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor=desc.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
        NSError *error=nil;
        desc.fragmentFunction=[library newFunctionWithName:@"menu_panel_fragment"];
        panelPipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
        desc.fragmentFunction=[library newFunctionWithName:@"menu_shade_fragment"];
        shadePipeline=[device newRenderPipelineStateWithDescriptor:desc error:&error];
        if(!panelPipeline || !shadePipeline) fprintf(stderr,"Menu pipelines unavailable: %s\n",error.localizedDescription.UTF8String);
        return panelPipeline && shadePipeline;
    }
    bool shown() const { return visible; }
    // True while the open fade still needs frames.
    bool animating() const { return visible && opacity()<1; }
    void close() { visible=false; signature.clear(); }
    // view: the content view bounds in points (y up). Returns true when the
    // panel's pixels changed and a new frame should be presented.
    bool present(NSRect view,CGFloat topInset,CGFloat scale,const NativeMenuPage &page) {
        if(!visible) openedAt=NSProcessInfo.processInfo.systemUptime;
        visible=true;
        id art=page.logo;
        CGFloat logoHeight=art?logoWidth*CGImageGetHeight((__bridge CGImageRef)art)/CGImageGetWidth((__bridge CGImageRef)art):0;
        NSDictionary *bodyAttributes=@{NSFontAttributeName:[NSFont systemFontOfSize:16],NSForegroundColorAttributeName:theme.body};
        CGFloat header=art?logoHeight+60:88;
        CGFloat bodyHeight=page.body?std::ceil([page.body boundingRectWithSize:NSMakeSize(width-64,CGFLOAT_MAX) options:NSStringDrawingUsesLineFragmentOrigin attributes:bodyAttributes].size.height)+16:0;
        int count=int(std::min<size_t>(page.rows.size(),RowLimit));
        CGFloat rowsHeight=count?count*rowPitch-(rowPitch-rowHeight):0;
        CGFloat height=header+bodyHeight+rowsHeight+64;
        CGFloat available=std::max(1.0,view.size.height-topInset-32);
        CGFloat fit=std::min({1.0,std::max(1.0,view.size.width-32)/width,available/height});
        // Panel rectangle in view points, snapped to whole pixels.
        CGFloat pixels=fit*scale;
        CGFloat pixelWidth=std::ceil(width*pixels),pixelHeight=std::ceil(height*pixels);
        CGFloat left=std::round((NSMidX(view)*scale-pixelWidth/2)),bottom=std::round(((NSMidY(view)-topInset/2)*scale-pixelHeight/2));
        frame=CGRectMake(left/scale,bottom/scale,pixelWidth/scale,pixelHeight/scale); unit=fit; viewWidth=view.size.width; viewHeight=view.size.height; backing=scale;
        rowTop=header+bodyHeight; rows=count;
        for(int i=0;i<count;++i) indices[i]=page.rows[i].index;

        std::string key=page.identity+"|"+std::to_string(page.selected)+"|"+std::to_string(int(pixelWidth))+"x"+std::to_string(int(pixelHeight));
        key+=page.body?page.body.UTF8String:""; key+="|"; key+=page.heading?page.heading.UTF8String:""; key+="|"; key+=page.hint?page.hint.UTF8String:"";
        key+=art?"|logo":"";
        for(int i=0;i<count;++i) { key+="|"; key+=std::to_string(page.rows[i].status); key+=page.rows[i].label.UTF8String; }
        if(key==signature) return animating();
        signature=key;
        raster(page,size_t(pixelWidth),size_t(pixelHeight),pixels,height,header,bodyHeight,logoHeight,bodyAttributes);
        return true;
    }
    // Composites the shade and the panel into the current render pass.
    void encode(id<MTLRenderCommandEncoder> enc,double drawableWidth,double drawableHeight) {
        if(!visible || !texture || !panelPipeline || !shadePipeline) return;
        float alpha=float(opacity());
        CGFloat r=0,g=0,b=0,a=0;
        [[theme.shade colorUsingColorSpace:NSColorSpace.sRGBColorSpace] getRed:&r green:&g blue:&b alpha:&a];
        auto linear=[&](CGFloat c) { return srgbTarget?(c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4)):c; };
        float shade[4]={float(linear(r)*a)*alpha,float(linear(g)*a)*alpha,float(linear(b)*a)*alpha,float(a)*alpha};
        [enc setViewport:(MTLViewport){0,0,drawableWidth,drawableHeight,0,1}];
        [enc setRenderPipelineState:shadePipeline];
        [enc setFragmentBytes:shade length:sizeof(shade) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        double sx=drawableWidth/(viewWidth*backing);
        double x=frame.origin.x*backing*sx,y=(viewHeight-frame.origin.y-frame.size.height)*backing*sx;
        [enc setViewport:(MTLViewport){x,y,double(texture.width),double(texture.height),0,1}];
        [enc setRenderPipelineState:panelPipeline];
        [enc setFragmentTexture:texture atIndex:0];
        [enc setFragmentBytes:&alpha length:sizeof(alpha) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    // The Row::index under a point in view coordinates (points, y up), or -1.
    int hit(CGPoint point) const {
        if(!visible || !CGRectContainsPoint(frame,point)) return -1;
        CGFloat localX=(point.x-frame.origin.x)/unit,fromTop=(frame.origin.y+frame.size.height-point.y)/unit;
        if(localX<32 || localX>width-32) return -1;
        for(int i=0;i<rows;++i) {
            CGFloat top=rowTop+i*rowPitch;
            if(fromTop>=top && fromTop<top+rowHeight) return indices[i];
        }
        return -1;
    }
private:
    static constexpr CGFloat width=500,rowPitch=44,rowHeight=40,logoWidth=210;
    double opacity() const {
        double t=std::clamp((NSProcessInfo.processInfo.systemUptime-openedAt)/.12,0.0,1.0);
        return 1-(1-t)*(1-t);
    }
    void raster(const NativeMenuPage &page,size_t w,size_t h,CGFloat pixels,CGFloat height,CGFloat header,CGFloat bodyHeight,CGFloat logoHeight,NSDictionary *bodyAttributes) {
        bitmap.assign(w*h*4,0);
        CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context=CGBitmapContextCreate(bitmap.data(),w,h,8,w*4,space,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if(!context) return;
        CGContextScaleCTM(context,pixels,pixels);
        NSGraphicsContext *previous=NSGraphicsContext.currentContext;
        NSGraphicsContext.currentContext=[NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
        auto fill=[&](CGRect r,CGFloat radius,NSColor *color,NSColor *border) {
            NSBezierPath *path=[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius];
            [color setFill]; [path fill];
            if(border) { NSBezierPath *edge=[NSBezierPath bezierPathWithRoundedRect:CGRectInset(r,.5,.5) xRadius:radius yRadius:radius]; edge.lineWidth=1; [border setStroke]; [edge stroke]; }
        };
        NSMutableParagraphStyle *center=[NSMutableParagraphStyle new]; center.alignment=NSTextAlignmentCenter; center.lineBreakMode=NSLineBreakByTruncatingTail;
        fill(CGRectMake(0,0,width,height),4,theme.panel,theme.border);
        id art=page.logo;
        if(art) {
            CGContextSetInterpolationQuality(context,kCGInterpolationNone);
            CGContextDrawImage(context,CGRectMake((width-logoWidth)/2,height-28-logoHeight,logoWidth,logoHeight),(__bridge CGImageRef)art);
        } else {
            NSFont *titleFont=[NSFont fontWithName:@"AvenirNextCondensed-Heavy" size:27]?:[NSFont systemFontOfSize:27 weight:NSFontWeightBold];
            [(page.heading?:@"").uppercaseString drawInRect:CGRectMake(32,height-62,width-64,34) withAttributes:@{NSFontAttributeName:titleFont,NSForegroundColorAttributeName:theme.title,NSParagraphStyleAttributeName:center}];
        }
        [theme.rule setFill]; NSRectFill(CGRectMake(32,height-header+16,width-64,1));
        if(page.body) [page.body drawWithRect:CGRectMake(32,height-header-bodyHeight,width-64,bodyHeight) options:NSStringDrawingUsesLineFragmentOrigin attributes:bodyAttributes];
        NSFont *rowFont=[NSFont fontWithName:@"AvenirNextCondensed-DemiBold" size:20]?:[NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];
        NSColor *colors[3]={theme.disabled,theme.normal,theme.selected};
        for(int i=0;i<rows;++i) {
            const auto &row=page.rows[i];
            CGRect r=CGRectMake(32,height-header-bodyHeight-rowHeight-i*rowPitch,width-64,rowHeight);
            bool selected=row.index==page.selected;
            if(selected) fill(r,2,theme.selector,theme.selectorBorder);
            [row.label.uppercaseString drawInRect:CGRectMake(r.origin.x+12,r.origin.y+7,width-88,27)
                                   withAttributes:@{NSFontAttributeName:rowFont,NSForegroundColorAttributeName:colors[row.status?(selected?2:1):0],NSParagraphStyleAttributeName:center}];
        }
        NSFont *hintFont=[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
        [(page.hint?:@"") drawInRect:CGRectMake(32,24,width-64,20) withAttributes:@{NSFontAttributeName:hintFont,NSForegroundColorAttributeName:theme.hint,NSParagraphStyleAttributeName:center}];
        NSGraphicsContext.currentContext=previous;
        CGContextRelease(context);
        // Three textures in turn: a frame still in flight keeps reading its own.
        __strong id<MTLTexture> &slot=textures[next++%3];
        if(!slot || slot.width!=w || slot.height!=h) {
            MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:srgbTarget?MTLPixelFormatRGBA8Unorm_sRGB:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
            d.storageMode=MTLStorageModeShared; d.usage=MTLTextureUsageShaderRead;
            slot=[device newTextureWithDescriptor:d];
        }
        [slot replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:bitmap.data() bytesPerRow:w*4];
        texture=slot;
    }
    NativeMenuTheme theme{};
    id<MTLDevice> device=nil;
    id<MTLRenderPipelineState> panelPipeline=nil,shadePipeline=nil;
    id<MTLTexture> textures[3]={},texture=nil;
    std::vector<uint8_t> bitmap;
    unsigned next=0;
    bool srgbTarget=false,visible=false;
    double openedAt=0;
    CGRect frame{}; CGFloat unit=1,viewWidth=1,viewHeight=0,backing=1,rowTop=0;
    int indices[RowLimit]={},rows=0;
    std::string signature;
};
