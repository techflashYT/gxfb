/*
 * GX-accelerated Framebuffer Demo
 *
 * Based on but extremely far-removed from devkitPro wii-examples graphics/gx/gxSprites/source/gxsprites.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>

#define DEFAULT_FIFO_SIZE	(256*1024)
#define FB_WIDTH 640
#define FB_HEIGHT 480

static void *xfb = NULL;
static void *rgbFb = NULL;
static void *efb = (void *)0xc8000000;
static GXRModeObj *rmode;
static GXTexObj texObj;

//#define MODE_RGB565
#define MODE_RGBA8888

// benchmark FPS or not
#define BENCH_MODE

#ifdef MODE_RGB565
typedef u16 pixel_t;
#define PIX_COLOR_WHITE 0xffff
#define PIX_COLOR_BLUE  0x001f
#elif defined(MODE_RGBA8888)
typedef u32 pixel_t;
#define PIX_COLOR_WHITE 0xffffffff
#define PIX_COLOR_BLUE  0xff0000ff
#endif

//
// framebuffer functions
//

static void rgbFbToEFB(void) {
	int y;
	void *src, *dst;

	src = rgbFb;
	dst = efb;
	for (y = 0; y < FB_HEIGHT; y++) {
		memcpy(dst, src, FB_WIDTH * sizeof(pixel_t));
		src += FB_WIDTH * sizeof(pixel_t);
		dst += 1024 * sizeof(pixel_t);
	}
}

static void drawPix(int x, int y, pixel_t pix) {
	((pixel_t *)rgbFb)[(y * FB_WIDTH) + x] = pix;
}

static void clearFb(pixel_t pix) {
	int x, y;
	for (x = 0; x < FB_WIDTH; x++)
		for (y = 0; y < FB_HEIGHT; y++)
			drawPix(x, y, pix);
}

static void drawSquare(int thickness, int width, int x, int y, pixel_t pix) {
	int i, j;

	for (i = x; i <= x + width; i++) {
		for (j = y; j <= y + thickness; j++) // top
			drawPix(i, j, pix);
		for (j = y + (width - thickness); j <= y + width; j++) // bottom
			drawPix(i, j, pix);
	}

	for (i = y; i <= y + width; i++) {
		for (j = x; j <= x + thickness; j++) // left
			drawPix(j, i, pix);
		for (j = x + (width - thickness); j <= x + width; j++) // right
			drawPix(j, i, pix);
	}
}


//
// timing functions (for benchmark)
//

#ifdef BENCH_MODE
	#ifdef HW_RVL
	#define ticksPerUsec (243 / 4)
	#elif defined(HW_DOL)
	#define ticksPerUsec (162 / 4)
	#else
	#error "What in the world are you building for"
	#endif

	static inline u64 mftb(void) {
		u32 hi, hi2, lo;
		hi = hi2 = lo = 0;

		do {
			asm volatile("mftbu %0" : "=r"(hi));
			asm volatile("mftb  %0" : "=r"(lo));
			asm volatile("mftbu %0" : "=r"(hi2));
		/* avoid rollover */
		} while (hi != hi2);

		return (u64)(((u64)hi << 32) | lo);
	}

	bool hasElapsed(u64 startTB, u32 usecSince) {
		u64 tb, ticks;
		tb = mftb();
		ticks = (u64)ticksPerUsec * usecSince;
		return (tb >= (startTB + ticks));
	}
#endif
int main(int argc, char *argv[]) {
	f32 yscale;
	u32 xfbHeight;
	Mtx44 perspective;
	Mtx GXmodelView2D;
	void *gx_fifo = NULL;
	GXColor background = {0, 0, 0, 0xff};
	int x = 400, y = 200, dx = 4, dy = 4;
#ifdef BENCH_MODE
	u64 frames = 0, lastTb = 0;
#endif

	// basic VI setup
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();
#ifdef BENCH_MODE
	SYS_STDIO_Report(true);
#endif

	// setup the FIFO and then init the GX API
	gx_fifo = memalign(32, DEFAULT_FIFO_SIZE);
	memset(gx_fifo, 0, DEFAULT_FIFO_SIZE);
	GX_Init(gx_fifo, DEFAULT_FIFO_SIZE);

	// clears the bg to color and clears the Z buffer
	GX_SetCopyClear(background, 0x00ffffff);

	// other GX setup
	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
	GX_SetCopyFilter(GX_FALSE, rmode->sample_pattern, GX_FALSE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering, GX_DISABLE);

#ifdef MODE_RGB565
	GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
#elif defined(MODE_RGBA8888)
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
#endif


	GX_SetCullMode(GX_CULL_NONE);
	GX_CopyDisp(xfb, GX_TRUE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	// setup the vertex descriptor
	// tells GX to expect direct data
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);


	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);


	GX_InvalidateTexAll();

	// allocate the RGB FB
	rgbFb = memalign(32, FB_WIDTH * FB_HEIGHT * sizeof(pixel_t));
	clearFb(PIX_COLOR_BLUE); // blue
	drawSquare(4, 50, x, y, PIX_COLOR_WHITE);

	rgbFbToEFB();

	// create and load the texture
	guOrtho(perspective, 0, FB_HEIGHT - 1, 0, FB_WIDTH - 1, 0, 300);
	GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);

	// render every frame
	while (true) {
		clearFb(PIX_COLOR_BLUE); // blue

		//check for collision with the screen boundaries
		x += dx;
		y += dy;
		if(x < 1 || x > (FB_WIDTH - 50))
			dx = -dx;

		if(y < 1 || y > (FB_HEIGHT - 50))
			dy = -dy;
		drawSquare(4, 50, x, y, PIX_COLOR_WHITE);
		rgbFbToEFB();
		GX_CopyDisp(xfb, GX_FALSE);
		GX_DrawDone();

		VIDEO_SetNextFramebuffer(xfb);
		VIDEO_Flush();

#ifdef BENCH_MODE
		frames++;
		if (hasElapsed(lastTb, 1000 * 1000)) {
			char str[64];
			lastTb = mftb();
			sprintf(str, "Frames in past second: %llu\r\n", frames);
			printf("%s", str);
			//usb_sendbuffer(0, str, strlen(str));
			usb_sendbuffer(1, str, strlen(str));
			frames = 0;
		}
#else
		VIDEO_WaitVSync();
#endif
	}
	return 0;
}

