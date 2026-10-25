// Native macOS backend: modified 2026-10-01; native platform adapter. GPL-2.0.
// ID_VL.C

#include <string.h>
#include "wl_def.h"

// Uncomment the following line, if you get destination out of bounds
// assertion errors and want to ignore them during debugging
//#define IGNORE_BAD_DEST

#ifdef IGNORE_BAD_DEST
#undef assert
#define assert(x) if(!(x)) return
#define assert_ret(x) if(!(x)) return 0
#else
#define assert_ret(x) assert(x)
#endif

boolean fullscreen = false;
boolean aspect = true; // [FG] aspect ratio correction
unsigned screenWidth = 640;
unsigned screenHeight = 400;
int screenBits = -1;      // use "best" color depth according to libSDL

Wolf_Window *window;
static Wolf_Renderer *renderer;
static Wolf_Texture *texture;

Wolf_Surface *screen = NULL;
unsigned screenPitch;

Wolf_Surface *screenBuffer = NULL;
unsigned bufferPitch;

Wolf_Surface *curSurface = NULL;
unsigned curPitch;

unsigned scaleFactor;

boolean	 screenfaded;
unsigned bordercolor;

Wolf_Color palette1[256], palette2[256];
Wolf_Color curpal[256];


#define CASSERT(x) extern int ASSERT_COMPILE[((x) != 0) * 2 - 1];
#define RGB(r, g, b) {(r)*255/63, (g)*255/63, (b)*255/63, 255}

Wolf_Color gamepal[]={
#ifdef SPEAR
    #include "sodpal.inc"
#else
    #include "wolfpal.inc"
#endif
};

CASSERT(lengthof(gamepal) == 256)

//===========================================================================


/*
=======================
=
= VL_Shutdown
=
=======================
*/

void	VL_Shutdown (void)
{
	//VL_SetTextMode ();
}


/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

static void VL_AllocateNativeFrame(bool initial)
{
    int pixel_format = Wolf_GetWindowPixelFormat(window);
    // [FG] aspect ratio correction
    int actualHeight = aspect ? (6 * screenHeight / 5) : screenHeight;
    // [FG] create renderer

    if(!renderer)renderer = Wolf_CreateRenderer(window, -1, Wolf_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        renderer = Wolf_CreateRenderer(window, -1, Wolf_RENDERER_SOFTWARE);
    }
    if (!renderer)
    {
        printf("Unable to create renderer: %s\n",
               Wolf_GetError());
        exit(1);
    }
    // [FG] aspect ratio correction
    Wolf_RenderSetLogicalSize(renderer, screenWidth, actualHeight);

    // [FG] create texture

    Wolf_SetHint(Wolf_HINT_RENDER_SCALE_QUALITY, "nearest");
    delete texture;texture=nullptr;
    texture = Wolf_CreateTexture(renderer,
                                pixel_format,
                                Wolf_TEXTUREACCESS_STREAMING,
                                screenWidth, screenHeight);
    if (!texture)
    {
        printf("Unable to create texture: %s\n",
               Wolf_GetError());
        exit(1);
    }

   // [FG] create intermediate RGBA frame buffer

    unsigned int rmask, gmask, bmask, amask;
    Wolf_PixelFormatEnumToMasks(pixel_format, &screenBits,
                               &rmask, &gmask, &bmask, &amask);

    Wolf_FreeSurface(screen);Wolf_FreeSurface(screenBuffer);
    Wolf_PresentSurface = NULL;
    screen = Wolf_CreateRGBSurface(0,
                                  screenWidth, screenHeight, screenBits,
                                  rmask, gmask, bmask, amask);
    if (!screen)
    {
        printf("Unable to set %ux%ux%i screen surface: %s\n",
               screenWidth, screenHeight, screenBits, Wolf_GetError());
        exit(1);
    }
    Wolf_FillRect(screen, NULL, 0);
    Wolf_PresentSurface = screen;

    if(initial)memcpy(curpal, gamepal, sizeof(Wolf_Color) * 256);

    // [FG] create paletted frame buffer

    screenBuffer = Wolf_CreateRGBSurface(0,
                                        screenWidth, screenHeight, 8,
                                        0, 0, 0, 0);
    if (!screenBuffer)
    {
        printf("Unable to create screen buffer surface: %s\n",
               Wolf_GetError());
        exit(1);
    }

    Wolf_Palette *sdlpal = Wolf_AllocPalette(256);
    if (!sdlpal || Wolf_SetPaletteColors(sdlpal, curpal, 0, 256) < 0)
    {
        printf("Unable to set palette colors: %s\n",
               Wolf_GetError());
        exit(1);
    }
    if (Wolf_SetSurfacePalette(screenBuffer, sdlpal) < 0)
    {
        printf("Unable to set surface palette: %s\n",
               Wolf_GetError());
        exit(1);
    }
    Wolf_FreePalette(sdlpal);

    screenPitch = screen->pitch;
    bufferPitch = screenBuffer->pitch;

    curSurface = screenBuffer;
    curPitch = bufferPitch;

    scaleFactor = screenWidth/320;
    if(screenHeight/200 < scaleFactor) scaleFactor = screenHeight/200;

    free(pixelangle);free(wallheight);
    pixelangle = (short *) malloc(screenWidth * sizeof(short));
    CHECKMALLOCRESULT(pixelangle);
    wallheight = (int *) malloc(screenWidth * sizeof(int));
    CHECKMALLOCRESULT(wallheight);
}

void	VL_SetVGAPlaneMode (void)
{
    const char *title;
#ifdef SPEAR
    title = "Spear of Destiny";
#else
    title = "Wolfenstein 3D";
#endif

    // [FG] create rendering window

    window = Wolf_CreateWindow(title,
                              Wolf_WINDOWPOS_CENTERED, Wolf_WINDOWPOS_CENTERED,
                              screenWidth, screenHeight,
                              Wolf_WINDOW_ALLOW_HIGHDPI | Wolf_WINDOW_RESIZABLE |
                              (fullscreen ? Wolf_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window)
    {
        printf("Unable to create %ux%u window: %s\n",
               screenWidth, screenHeight, Wolf_GetError());
        exit(1);
    }
    if(Wolf_NativeRenderSize(&screenWidth,&screenHeight))aspect=false;
    Wolf_SetWindowMinimumSize(window,320,240);
    VL_AllocateNativeFrame(true);
}

/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/

/*
=================
=
= VL_ConvertPalette
=
=================
*/

void VL_ConvertPalette(byte *srcpal, Wolf_Color *destpal, int numColors)
{
    for(int i=0; i<numColors; i++)
    {
        destpal[i].r = *srcpal++ * 255 / 63;
        destpal[i].g = *srcpal++ * 255 / 63;
        destpal[i].b = *srcpal++ * 255 / 63;
    }
}

/*
=================
=
= VL_FillPalette
=
=================
*/

void VL_FillPalette (int red, int green, int blue)
{
    int i;
    Wolf_Color pal[256];

    for(i=0; i<256; i++)
    {
        pal[i].r = red;
        pal[i].g = green;
        pal[i].b = blue;
    }

    VL_SetPalette(pal, true);
}

//===========================================================================

/*
=================
=
= VL_GetColor
=
=================
*/

void VL_GetColor	(int color, int *red, int *green, int *blue)
{
    Wolf_Color *col = &curpal[color];
    *red = col->r;
    *green = col->g;
    *blue = col->b;
}

//===========================================================================

/*
 =================
 =
 = VL_Flip
 = For SDL2
 =
 =================
 */

// IOANCH: major thanks to http://sandervanderburg.blogspot.ro/2014/05/rendering-8-bit-palettized-surfaces-in.html
void VL_Flip()
{
   Wolf_UpdateTexture(texture, NULL, screen->pixels, screen->pitch);

   Wolf_RenderClear(renderer);
   Wolf_RenderCopy(renderer, texture, NULL, NULL);
   Wolf_RenderPresent(renderer);
}

//===========================================================================

/*
=================
=
= VL_SetPalette
=
=================
*/

void VL_SetPalette (Wolf_Color *palette, bool forceupdate)
{
    memcpy(curpal, palette, sizeof(Wolf_Color) * 256);

    Wolf_Palette *pal = Wolf_AllocPalette(256);
    Wolf_SetPaletteColors(pal, curpal, 0, 256);
    Wolf_SetSurfacePalette(curSurface, pal);
    Wolf_FreePalette(pal);

    if(forceupdate)
    {
        Wolf_BlitSurface(curSurface, NULL, screen, NULL);
        VL_Flip();
    }
}


//===========================================================================

/*
=================
=
= VL_GetPalette
=
=================
*/

void VL_GetPalette (Wolf_Color *palette)
{
    memcpy(palette, curpal, sizeof(Wolf_Color) * 256);
}


//===========================================================================

/*
=================
=
= VL_FadeOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
	int		    i,j,orig,delta;
	Wolf_Color   *origptr, *newptr;

    red = red * 255 / 63;
    green = green * 255 / 63;
    blue = blue * 255 / 63;

	if (wolfMenuFrozen)
        steps = 0;      // native panel: no fade
    else
	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(Wolf_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[start];
		newptr = &palette2[start];
		for (j=start;j<=end;j++)
		{
			orig = origptr->r;
			delta = red-orig;
			newptr->r = orig + delta * i / steps;
			orig = origptr->g;
			delta = green-orig;
			newptr->g = orig + delta * i / steps;
			orig = origptr->b;
			delta = blue-orig;
			newptr->b = orig + delta * i / steps;
			origptr++;
			newptr++;
		}

		VL_SetPalette (palette2, true);
	}

//
// final color
//
	VL_FillPalette (red,green,blue);

	screenfaded = true;
}


/*
=================
=
= VL_FadeIn
=
=================
*/

void VL_FadeIn (int start, int end, Wolf_Color *palette, int steps)
{
	int i,j,delta;

	if (wolfMenuFrozen)
        steps = 0;      // native panel: no fade
    else
	VL_WaitVBL(1);
	VL_GetPalette(palette1);
	memcpy(palette2, palette1, sizeof(Wolf_Color) * 256);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=start;j<=end;j++)
		{
			delta = palette[j].r-palette1[j].r;
			palette2[j].r = palette1[j].r + delta * i / steps;
			delta = palette[j].g-palette1[j].g;
			palette2[j].g = palette1[j].g + delta * i / steps;
			delta = palette[j].b-palette1[j].b;
			palette2[j].b = palette1[j].b + delta * i / steps;
		}

		VL_SetPalette(palette2, true);
	}

//
// final color
//
	VL_SetPalette (palette, true);
	screenfaded = false;
}

/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

byte *VL_LockSurface(Wolf_Surface *surface)
{
    if(Wolf_MUSTLOCK(surface))
    {
        if(Wolf_LockSurface(surface) < 0)
            return NULL;
    }
    return (byte *) surface->pixels;
}

void VL_UnlockSurface(Wolf_Surface *surface)
{
    if(Wolf_MUSTLOCK(surface))
    {
        Wolf_UnlockSurface(surface);
    }
}

/*
=================
=
= VL_Plot
=
=================
*/

void VL_Plot (int x, int y, int color)
{
    byte *ptr;

    assert(x >= 0 && (unsigned) x < screenWidth
            && y >= 0 && (unsigned) y < screenHeight
            && "VL_Plot: Pixel out of bounds!");

    ptr = VL_LockSurface(curSurface);
    if(ptr == NULL) return;

    ptr[y * curPitch + x] = color;

    VL_UnlockSurface(curSurface);
}

/*
=================
=
= VL_GetPixel
=
=================
*/

byte VL_GetPixel (int x, int y)
{
    byte *ptr;
    byte col;

    assert_ret(x >= 0 && (unsigned) x < screenWidth
            && y >= 0 && (unsigned) y < screenHeight
            && "VL_GetPixel: Pixel out of bounds!");

    ptr = VL_LockSurface(curSurface);
    if(ptr == NULL) return 0;

    col = ((byte *) curSurface->pixels)[y * curPitch + x];

    VL_UnlockSurface(curSurface);

    return col;
}


/*
=================
=
= VL_Hlin
=
=================
*/

void VL_Hlin (unsigned x, unsigned y, unsigned width, int color)
{
    byte *ptr;

    assert(x >= 0 && x + width <= screenWidth
            && y >= 0 && y < screenHeight
            && "VL_Hlin: Destination rectangle out of bounds!");

    ptr = VL_LockSurface(curSurface);
    if(ptr == NULL) return;

    memset(ptr + y * curPitch + x, color, width);

    VL_UnlockSurface(curSurface);
}


/*
=================
=
= VL_Vlin
=
=================
*/

void VL_Vlin (int x, int y, int height, int color)
{
	byte *ptr;

	assert(x >= 0 && (unsigned) x < screenWidth
			&& y >= 0 && (unsigned) y + height <= screenHeight
			&& "VL_Vlin: Destination rectangle out of bounds!");

	ptr = VL_LockSurface(curSurface);
	if(ptr == NULL) return;

	ptr += y * curPitch + x;

	while (height--)
	{
		*ptr = color;
		ptr += curPitch;
	}

	VL_UnlockSurface(curSurface);
}


/*
=================
=
= VL_Bar
=
=================
*/

void VL_BarScaledCoord (int scx, int scy, int scwidth, int scheight, int color)
{
	byte *ptr;

	assert(scx >= 0 && (unsigned) scx + scwidth <= screenWidth
			&& scy >= 0 && (unsigned) scy + scheight <= screenHeight
			&& "VL_BarScaledCoord: Destination rectangle out of bounds!");

	ptr = VL_LockSurface(curSurface);
	if(ptr == NULL) return;

	ptr += scy * curPitch + scx;

	while (scheight--)
	{
		memset(ptr, color, scwidth);
		ptr += curPitch;
	}
	VL_UnlockSurface(curSurface);
}

/*
============================================================================

							MEMORY OPS

============================================================================
*/

/*
=================
=
= VL_MemToLatch
=
=================
*/

void VL_MemToLatch(byte *source, int width, int height,
    Wolf_Surface *destSurface, int x, int y)
{
    byte *ptr;
    int xsrc, ysrc, pitch;

    assert(x >= 0 && (unsigned) x + width <= screenWidth
            && y >= 0 && (unsigned) y + height <= screenHeight
            && "VL_MemToLatch: Destination rectangle out of bounds!");

    ptr = VL_LockSurface(destSurface);
    if(ptr == NULL) return;

    pitch = destSurface->pitch;
    ptr += y * pitch + x;
    for(ysrc = 0; ysrc < height; ysrc++)
    {
        for(xsrc = 0; xsrc < width; xsrc++)
        {
            ptr[ysrc * pitch + xsrc] = source[(ysrc * (width >> 2) + (xsrc >> 2))
                + (xsrc & 3) * (width >> 2) * height];
        }
    }
    VL_UnlockSurface(destSurface);
}

//===========================================================================


/*
=================
=
= VL_MemToScreenScaledCoord
=
= Draws a block of data to the screen with scaling according to scaleFactor.
=
=================
*/

void VL_MemToScreenScaledCoord (byte *source, int width, int height, int destx, int desty)
{
    byte *ptr;
    int i, j, sci, scj;
    unsigned m, n;

    assert(destx >= 0 && destx + width * scaleFactor <= screenWidth
            && desty >= 0 && desty + height * scaleFactor <= screenHeight
            && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    ptr = VL_LockSurface(curSurface);
    if(ptr == NULL) return;

    for(j = 0, scj = 0; j < height; j++, scj += scaleFactor)
    {
        for(i = 0, sci = 0; i < width; i++, sci += scaleFactor)
        {
            byte col = source[(j * (width >> 2) + (i >> 2)) + (i & 3) * (width >> 2) * height];
            for(m = 0; m < scaleFactor; m++)
            {
                for(n = 0; n < scaleFactor; n++)
                {
                    ptr[(scj + m + desty) * curPitch + sci + n + destx] = col;
                }
            }
        }
    }
    VL_UnlockSurface(curSurface);
}

/*
=================
=
= VL_MemToScreenScaledCoord
=
= Draws a part of a block of data to the screen.
= The block has the size origwidth*origheight.
= The part at (srcx, srcy) has the size width*height
= and will be painted to (destx, desty) with scaling according to scaleFactor.
=
=================
*/

void VL_MemToScreenScaledCoord (byte *source, int origwidth, int origheight, int srcx, int srcy,
                                int destx, int desty, int width, int height)
{
    byte *ptr;
    int i, j, sci, scj;
    unsigned m, n;

    assert(destx >= 0 && destx + width * scaleFactor <= screenWidth
            && desty >= 0 && desty + height * scaleFactor <= screenHeight
            && "VL_MemToScreenScaledCoord: Destination rectangle out of bounds!");

    ptr = VL_LockSurface(curSurface);
    if(ptr == NULL) return;

    for(j = 0, scj = 0; j < height; j++, scj += scaleFactor)
    {
        for(i = 0, sci = 0; i < width; i++, sci += scaleFactor)
        {
            byte col = source[((j + srcy) * (origwidth >> 2) + ((i + srcx) >>2 ))
                + ((i + srcx) & 3) * (origwidth >> 2) * origheight];

            for(m = 0; m < scaleFactor; m++)
            {
                for(n = 0; n < scaleFactor; n++)
                {
                    ptr[(scj + m + desty) * curPitch + sci + n + destx] = col;
                }
            }
        }
    }
    VL_UnlockSurface(curSurface);
}

//==========================================================================

/*
=================
=
= VL_LatchToScreen
=
=================
*/

void VL_LatchToScreenScaledCoord(Wolf_Surface *source, int xsrc, int ysrc,
    int width, int height, int scxdest, int scydest)
{
	assert(scxdest >= 0 && scxdest + width * scaleFactor <= screenWidth
			&& scydest >= 0 && scydest + height * scaleFactor <= screenHeight
			&& "VL_LatchToScreenScaledCoord: Destination rectangle out of bounds!");

    {
        byte *src, *dest;
        unsigned srcPitch;
        int i, j, sci, scj;
        unsigned m, n;

        src = VL_LockSurface(source);
        if(src == NULL) return;

        srcPitch = source->pitch;

        dest = VL_LockSurface(curSurface);
        if(dest == NULL) return;

        for(j = 0, scj = 0; j < height; j++, scj += scaleFactor)
        {
            for(i = 0, sci = 0; i < width; i++, sci += scaleFactor)
            {
                byte col = src[(ysrc + j)*srcPitch + xsrc + i];
                for(m = 0; m < scaleFactor; m++)
                {
                    for(n = 0; n < scaleFactor; n++)
                    {
                        dest[(scydest + scj + m) * curPitch + scxdest + sci + n] = col;
                    }
                }
            }
        }
        VL_UnlockSurface(curSurface);
        VL_UnlockSurface(source);
    }
}

//===========================================================================

/*
=================
=
= VL_ScreenToScreen
=
=================
*/

void VL_ScreenToScreen (Wolf_Surface *source, Wolf_Surface *dest)
{
    Wolf_BlitSurface(source, NULL, dest, NULL);
}

// Reallocate only at the start of a new game render, after the previous CPU frame
// has finished. The Metal adapter drains submitted textures before replacement.
void VL_NativeResize()
{
    unsigned width=screenWidth,height=screenHeight;
    if(!Wolf_NativeRenderSize(&width,&height) || (width==screenWidth && height==screenHeight))return;
    screenWidth=width;screenHeight=height;aspect=false;
    VL_AllocateNativeFrame(false);NewViewSize(viewsize);
    if(viewsize!=21)DrawPlayScreen();
}
