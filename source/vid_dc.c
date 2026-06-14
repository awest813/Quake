/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
 * vid_dc.c -- Sega Dreamcast video driver.
 *
 * The software rasteriser draws into an 8-bit palettised offscreen buffer
 * (vid.buffer).  Each frame we convert that buffer to RGB565 through a 256
 * entry lookup table, upload it to a PVR texture, and draw a single textured
 * quad scaled to 640x480 so the hardware handles the 2x upscale.
 */

#include <kos.h>
#include <dc/video.h>
#include <dc/pvr.h>

#include "quakedef.h"
#include "d_local.h"
#include "console.h"
#include "cvar.h"
#include "sys.h"
#include "vid.h"
#include "host.h"
#include "port.h"

int32_t spacing_x_res = 0;

int vid_modenum = VID_MODE_NONE;

viddef_t vid;			/* global video state */
uint16_t d_8to16table[256];

int32_t VGA_width, VGA_height, VGA_rowubytes, VGA_bufferrowubytes = 0;
uint8_t *VGA_pagebase;

/* 320x240 8-bit offscreen the rasteriser renders into. */
static uint8_t dc_framebuffer[BASEWIDTH * BASEHEIGHT];

/* PVR texture is the next power-of-two above 320x240 (512x256 RGB565). */
#define DC_TEX_W	512
#define DC_TEX_H	256

static pvr_ptr_t dc_pvr_texture;
static uint16_t *dc_rgb565;
static pvr_poly_hdr_t dc_pvr_hdr;
static qboolean dc_pvr_ready;

static void
DC_PVR_Init(void)
{
    pvr_poly_cxt_t cxt;

    if (pvr_init_defaults() < 0)
	Sys_Error("PVR init failed\n");

    pvr_set_bg_color(0.0f, 0.0f, 0.0f);

    dc_pvr_texture = pvr_mem_malloc(DC_TEX_W * DC_TEX_H * 2);
    if (!dc_pvr_texture)
	Sys_Error("Not enough PVR RAM for frame texture\n");

    dc_rgb565 = Hunk_AllocName(DC_TEX_W * DC_TEX_H * 2, "dc_rgb565");
    if (!dc_rgb565)
	Sys_Error("Not enough memory for RGB565 scratch\n");

    pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
		     PVR_TXRFMT_RGB565 | PVR_TXRFMT_NONTWIDDLED,
		     DC_TEX_W, DC_TEX_H, dc_pvr_texture, PVR_FILTER_NONE);
    pvr_poly_compile(&dc_pvr_hdr, &cxt);
    dc_pvr_ready = true;
}

static void
DC_PVR_Present(void)
{
    pvr_vertex_t vert;
    const float umax = (float)BASEWIDTH / (float)DC_TEX_W;
    const float vmax = (float)BASEHEIGHT / (float)DC_TEX_H;

    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_prim(&dc_pvr_hdr, sizeof(dc_pvr_hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.z = 1.0f;
    vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
    vert.oargb = 0;

    vert.x = 0.0f;
    vert.y = 480.0f;
    vert.u = 0.0f;
    vert.v = vmax;
    pvr_prim(&vert, sizeof(vert));

    vert.y = 0.0f;
    vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = 640.0f;
    vert.y = 480.0f;
    vert.u = umax;
    vert.v = vmax;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.y = 0.0f;
    vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    pvr_list_finish();
    pvr_scene_finish();
}

void
VID_SetPalette(const uint8_t *palette)
{
    int i;

    for (i = 0; i < 256; i++) {
	uint8_t r = palette[0];
	uint8_t g = palette[1];
	uint8_t b = palette[2];
	palette += 3;
	d_8to16table[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

void
VID_ShiftPalette(const uint8_t *palette)
{
    VID_SetPalette(palette);
}

void
VID_Init(const uint8_t *palette)
{
    uint8_t *cache;
    int chunk, cachesize;

    vid.width = vid.conwidth = BASEWIDTH;
    vid.height = vid.conheight = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int32_t *)vid.colormap + 2048));

    vid.buffer = vid.conbuffer = dc_framebuffer;
    vid.rowbytes = vid.conrowbytes = BASEWIDTH;
    vid.direct = 0;

    VGA_pagebase = vid.buffer;
    VGA_width = vid.width;
    VGA_height = vid.height;
    VGA_rowubytes = vid.rowbytes;

    vid_set_mode(DM_640x480, PM_RGB565);
    DC_PVR_Init();

    VID_SetPalette(palette);

    /* z buffer and surface cache, exactly as the SDL driver does. */
    chunk = vid.width * vid.height * sizeof(*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes(vid.width, vid.height);
    chunk += cachesize;
    d_pzbuffer = Hunk_HighAllocName(chunk, "video");
    if (d_pzbuffer == NULL)
	Sys_Error("Not enough memory for video mode\n");

    cache = (uint8_t *)d_pzbuffer
	+ vid.width * vid.height * sizeof(*d_pzbuffer);
    D_InitCaches(cache, cachesize);

    spacing_x_res = 0;
}

void
VID_Shutdown(void)
{
    if (dc_pvr_ready) {
	pvr_mem_free(dc_pvr_texture);
	dc_pvr_texture = NULL;
	pvr_shutdown();
	dc_pvr_ready = false;
    }
}

void
VID_Update(vrect_t *rects)
{
    int x, y;
    const uint8_t *src = vid.buffer;

    (void)rects;

    /* 8-bit palettised buffer -> RGB565 in a 512x256 scratch texture. */
    for (y = 0; y < BASEHEIGHT; y++) {
	uint16_t *dst = dc_rgb565 + y * DC_TEX_W;
	const uint8_t *s = src + y * BASEWIDTH;

	for (x = 0; x < BASEWIDTH; x++)
	    dst[x] = d_8to16table[s[x]];
    }

    pvr_txr_load_ex(dc_rgb565, dc_pvr_texture, DC_TEX_W, DC_TEX_H,
		    PVR_TXRLOAD_16BPP | PVR_TXRLOAD_FMT_NOTWIDDLE);
    DC_PVR_Present();
    vid_waitvbl();
}

/*
================
D_BeginDirectRect -- draw the loading/disc icon directly into the offscreen
================
*/
void
D_BeginDirectRect(int x, int y, const uint8_t *pbitmap, int width, int height)
{
    uint8_t *dest;

    if (!vid.buffer)
	return;
    if (x < 0)
	x = vid.width + x - 1;
    dest = vid.buffer + y * vid.rowbytes + x;
    while (height--) {
	memcpy(dest, pbitmap, width);
	dest += vid.rowbytes;
	pbitmap += width;
    }
}

void
D_EndDirectRect(int x, int y, int width, int height)
{
    /* The whole frame is re-presented on the next VID_Update. */
}

void
VID_LockBuffer(void)
{
}

void
VID_UnlockBuffer(void)
{
}

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    return true;
}

qboolean
VID_CheckAdequateMem(int width, int height)
{
    int tbuffersize;

    tbuffersize = width * height * sizeof(*d_pzbuffer);
    tbuffersize += D_SurfaceCacheForRes(width, height);

    if ((host_parms.memsize - tbuffersize + SURFCACHE_SIZE_AT_320X200 +
	 0x10000 * 3) < minimum_memory)
	return false;

    return true;
}
