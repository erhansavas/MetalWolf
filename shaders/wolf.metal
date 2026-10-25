// SPDX-License-Identifier: GPL-2.0-only
#include <metal_stdlib>
using namespace metal;
// The 8-bit frame is presented on an sRGB target; colors arrive as packed
// RGBA8 sRGB and are resolved per pixel.
struct Raster { float4 position [[position]]; float2 uv; };
vertex Raster wolf_vertex(uint v [[vertex_id]]) {
    const float2 p[] = {float2(-1,-1),float2(3,-1),float2(-1,3)};
    Raster r; r.position=float4(p[v],0,1); r.uv=float2((p[v].x+1)*.5,(1-p[v].y)*.5); return r;
}
static float3 srgb_decode(float3 c) {
    return select(pow((c+.055)/1.055,2.4),c/12.92,c<=.04045);
}
// Linear color of a palette index.
static float3 wolf_color(uint index, constant uint *colors) {
    uint c=colors[index];
    return srgb_decode(float3(c&255u,(c>>8)&255u,(c>>16)&255u)/255.0);
}
fragment float4 wolf_fragment(Raster in [[stage_in]], texture2d<uint> original [[texture(0)]], constant uint &filtered [[buffer(0)]],
                              constant uint *colors [[buffer(1)]]) {
    float2 size=float2(original.get_width(),original.get_height());
    // Adaptive resolution: a frame rendered below the drawable's size is
    // filtered (in linear light) instead of repeating whole texels.
    if(filtered) {
        float2 t=clamp(in.uv*size-.5,float2(0),size-1);
        uint2 a=uint2(t),b=min(a+1,uint2(size)-1);
        float2 f=t-float2(a);
        float3 c00=wolf_color(original.read(a).r,colors),c10=wolf_color(original.read(uint2(b.x,a.y)).r,colors);
        float3 c01=wolf_color(original.read(uint2(a.x,b.y)).r,colors),c11=wolf_color(original.read(b).r,colors);
        return float4(mix(mix(c00,c10,f.x),mix(c01,c11,f.x),f.y),1);
    }
    uint2 p=uint2(clamp(in.uv,float2(0),float2(.999999))*size);
    return float4(wolf_color(original.read(p).r,colors),1);
}
// Native menu panel: premultiplied, rasterized at drawable resolution.
fragment float4 menu_panel_fragment(Raster in [[stage_in]], texture2d<float> panel [[texture(0)]], constant float &opacity [[buffer(0)]]) {
    uint2 p=uint2(clamp(in.uv,float2(0),float2(.999999))*float2(panel.get_width(),panel.get_height()));
    return panel.read(p)*opacity;
}
fragment float4 menu_shade_fragment(Raster in [[stage_in]], constant float4 &shade [[buffer(0)]]) { return shade; }
// The status card: the status bar image with rounded corners (shape: width,
// height, corner radius in pixels).
fragment float4 wolf_status_fragment(Raster in [[stage_in]], texture2d<uint> status [[texture(0)]],
                                     constant uint *colors [[buffer(1)]], constant float4 &shape [[buffer(2)]]) {
    uint2 p=uint2(clamp(in.uv,float2(0),float2(.999999))*float2(status.get_width(),status.get_height()));
    float2 edge=abs((in.uv-.5)*shape.xy)-(shape.xy*.5-shape.z);
    float distance=length(max(edge,0.0))+min(max(edge.x,edge.y),0.0)-shape.z;
    return float4(wolf_color(status.read(p).r,colors),1-smoothstep(-.5,.5,distance));
}
