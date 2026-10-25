// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
// ID_VL.H

// wolf compatability

void Quit (const char *error,...);

//===========================================================================

#define CHARWIDTH		2
#define TILEWIDTH		4

//===========================================================================

extern Wolf_Window *window;
extern Wolf_Surface *screen, *screenBuffer, *curSurface;

extern  boolean  fullscreen;
extern  boolean  aspect; // [FG] aspect ratio correction
extern  unsigned screenWidth, screenHeight, screenPitch, bufferPitch, curPitch;
extern  unsigned scaleFactor;
// Native: the 320x200 page is centered on a screen that is not an exact
// multiple of it. Scaled-coordinate calls stay absolute.
inline int VL_PageX() { int x=(int(screenWidth)-int(320*scaleFactor))/2; return x>0?x:0; }
inline int VL_PageY() { int y=(int(screenHeight)-int(200*scaleFactor))/2; return y>0?y:0; }

extern	boolean  screenfaded;
extern	unsigned bordercolor;

extern Wolf_Color gamepal[256];

//===========================================================================

//
// VGA hardware routines
//

#define VL_WaitVBL(a) Wolf_Delay((a)*8)

void VL_SetVGAPlaneMode (void);
void VL_SetTextMode (void);
void VL_Shutdown (void);

void VL_ConvertPalette(byte *srcpal, Wolf_Color *destpal, int numColors);
void VL_FillPalette (int red, int green, int blue);
void VL_GetColor    (int color, int *red, int *green, int *blue);
void VL_SetPalette  (Wolf_Color *palette, bool forceupdate);
void VL_GetPalette  (Wolf_Color *palette);
void VL_FadeOut     (int start, int end, int red, int green, int blue, int steps);
void VL_FadeIn      (int start, int end, Wolf_Color *palette, int steps);

byte *VL_LockSurface(Wolf_Surface *surface);
void VL_UnlockSurface(Wolf_Surface *surface);

byte VL_GetPixel        (int x, int y);
void VL_Plot            (int x, int y, int color);
void VL_Hlin            (unsigned x, unsigned y, unsigned width, int color);
void VL_Vlin            (int x, int y, int height, int color);
void VL_BarScaledCoord  (int scx, int scy, int scwidth, int scheight, int color);
void inline VL_Bar      (int x, int y, int width, int height, int color)
{
    VL_BarScaledCoord(VL_PageX()+scaleFactor*x, VL_PageY()+scaleFactor*y,
        scaleFactor*width, scaleFactor*height, color);
}
void inline VL_ClearScreen(int color)
{
    Wolf_FillRect(curSurface, NULL, color);
}

void VL_MungePic                (byte *source, unsigned width, unsigned height);
void VL_DrawPicBare             (int x, int y, byte *pic, int width, int height);
void VL_MemToLatch              (byte *source, int width, int height,
                                    Wolf_Surface *destSurface, int x, int y);
void VL_ScreenToScreen          (Wolf_Surface *source, Wolf_Surface *dest);
void VL_MemToScreenScaledCoord  (byte *source, int width, int height, int scx, int scy);
void VL_MemToScreenScaledCoord  (byte *source, int origwidth, int origheight, int srcx, int srcy,
                                    int destx, int desty, int width, int height);

void inline VL_MemToScreen (byte *source, int width, int height, int x, int y)
{
    VL_MemToScreenScaledCoord(source, width, height,
        VL_PageX()+scaleFactor*x, VL_PageY()+scaleFactor*y);
}

void VL_MaskedToScreen (byte *source, int width, int height, int x, int y);

void VL_LatchToScreenScaledCoord (Wolf_Surface *source, int xsrc, int ysrc,
    int width, int height, int scxdest, int scydest);

void inline VL_LatchToScreen (Wolf_Surface *source, int xsrc, int ysrc,
    int width, int height, int xdest, int ydest)
{
    VL_LatchToScreenScaledCoord(source,xsrc,ysrc,width,height,
        VL_PageX()+scaleFactor*xdest,VL_PageY()+scaleFactor*ydest);
}
void inline VL_LatchToScreenScaledCoord (Wolf_Surface *source, int scx, int scy)
{
    VL_LatchToScreenScaledCoord(source,0,0,source->w,source->h,scx,scy);
}
void inline VL_LatchToScreen (Wolf_Surface *source, int x, int y)
{
    VL_LatchToScreenScaledCoord(source,0,0,source->w,source->h,
        VL_PageX()+scaleFactor*x,VL_PageY()+scaleFactor*y);
}
