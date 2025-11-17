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
static void *tiledFb = NULL;
static GXRModeObj *rmode;
static GXTexObj texObj;

//#define MODE_RGB565
#define MODE_RGBA8888

// modified version of https://github.com/Mefiresu/RSDKv5-Decompilation/blob/dev/wii-port/RSDKv5/RSDK/Graphics/GX/GXRenderDevice.cpp#L285
#ifdef MODE_RGB565
static inline void fbToTiledTexture(void *dst, const void *src, u32 width, u32 height) {
	GX_RedirectWriteGatherPipe(dst);
	const u32 *src32 = (const u32 *)src;
	const u32 *tmp_src32;
	int x, y;
	u32 width_2;

	for (y = 0; y < height >> 2; y++) {
		tmp_src32 = src32;
		for (x = 0; x < width >> 2; x++) {
			width_2 = width / 2;
			wgPipe->U32 = src32[0x000];
			wgPipe->U32 = src32[0x001];
			wgPipe->U32 = src32[width_2 + 0x000];
			wgPipe->U32 = src32[width_2 + 0x001];
			wgPipe->U32 = src32[width + 0x000]; // width / 2 * 2
			wgPipe->U32 = src32[width + 0x001]; // width / 2 * 2
			wgPipe->U32 = src32[width_2*3 + 0x000];
			wgPipe->U32 = src32[width_2*3 + 0x001];

			src32 += 2;
		}

		src32 = tmp_src32 + width * 2;
	}
	GX_RestoreWriteGatherPipe();
}
#elif defined(MODE_RGBA8888)
static void fbToTiledTexture(void *dst, const u32 *src, u32 width, u32 height) {
	u32 *src_row, blocks_x, blocks_y, *src_block, *row_ptr, bx, by, yy, xx, p;
	u8 r, g, b, a;
	u16 ar, gb;
	GX_RedirectWriteGatherPipe(dst);

	src_row = src;

	// How many 4×4 tiles per row?
	blocks_x = width  >> 2;
	blocks_y = height >> 2;

	for (by = 0; by < blocks_y; by++) {
		src_block = src_row;

		for (bx = 0; bx < blocks_x; bx++) {
			// Write AR plane
			for (yy = 0; yy < 4; yy++) {
				row_ptr = src_block + yy * width;
				for (xx = 0; xx < 4; xx++) {
					p = row_ptr[xx];    // RR GG BB AA
					a =  p & 0xff;
					r = (p >> 24) & 0xFF;
					ar = (a << 8) | r;
					wgPipe->U16 = ar;
				}
			}

			// Write GB plane
			for (yy = 0; yy < 4; yy++) {
				row_ptr = src_block + yy * width;
				for (xx = 0; xx < 4; xx++) {
					p = row_ptr[xx];
					g = (p >>  16) & 0xFF;
					b = (p >>  8)  & 0xFF;
					gb = (g << 8) | b;
					wgPipe->U16 = gb;
				}
			}

			src_block += 4;
		}

		src_row += width * 4;
	}

	GX_RestoreWriteGatherPipe();
}

#endif


static void drawFb(void) {
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);			// Draw A Quad
		GX_Position2f32(0, 0);				// Top Left
		GX_TexCoord2f32(0, 0);
		GX_Position2f32(FB_WIDTH - 1, 0);		// Top Right
		GX_TexCoord2f32(1, 0);
		GX_Position2f32(FB_WIDTH - 1, FB_HEIGHT - 1);	// Bottom Right
		GX_TexCoord2f32(1, 1);
		GX_Position2f32(0, FB_HEIGHT - 1);		// Bottom Left
		GX_TexCoord2f32(0, 1);
	GX_End();						// Done Drawing The Quad

}

// benchmark FPS or not
#define BENCH_MODE

#ifdef MODE_RGB565
typedef u16 pixel_t;
#define PIX_COLOR_WHITE 0xffff
#define PIX_COLOR_BLUE  0x001f
#elif defined(MODE_RGBA8888)
typedef u32 pixel_t;
#define PIX_COLOR_WHITE 0xffffffff
#define PIX_COLOR_BLUE  0x0000ffff
#endif

//
// framebuffer functions
//

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
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

	if (rmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);


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
	tiledFb = memalign(32, FB_WIDTH * FB_HEIGHT * sizeof(pixel_t));
	clearFb(PIX_COLOR_BLUE); // blue
	drawSquare(4, 50, x, y, PIX_COLOR_WHITE);

	// convert the fb to the tiled format that GX expects
	fbToTiledTexture(tiledFb, rgbFb, FB_WIDTH, FB_HEIGHT);
	DCFlushRange(tiledFb, FB_WIDTH * FB_HEIGHT * sizeof(pixel_t));

	// create and load the texture
#ifdef MODE_RGB565
	GX_InitTexObj(&texObj, tiledFb, FB_WIDTH, FB_HEIGHT, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
#elif defined(MODE_RGBA8888)
	GX_InitTexObj(&texObj, tiledFb, FB_WIDTH, FB_HEIGHT, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
#endif
	GX_LoadTexObj(&texObj, GX_TEXMAP0);

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

		// convert the fb to the tiled format that GX expects
		DCFlushRange(rgbFb, FB_WIDTH * FB_HEIGHT * sizeof(pixel_t));
		fbToTiledTexture(tiledFb, rgbFb, FB_WIDTH, FB_HEIGHT);
		DCFlushRange(tiledFb, FB_WIDTH * FB_HEIGHT * sizeof(pixel_t));


		GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
		GX_InvVtxCache();
		GX_InvalidateTexAll();

		GX_ClearVtxDesc();
		GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
		GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

		guMtxIdentity(GXmodelView2D);
		guMtxTransApply (GXmodelView2D, GXmodelView2D, 0.0F, 0.0F, -5.0F);
		GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);

		drawFb();

		GX_DrawDone();

		GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
		GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
		GX_SetAlphaUpdate(GX_TRUE);
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(xfb, GX_TRUE);

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

