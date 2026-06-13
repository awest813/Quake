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
 * entry lookup table and present it.  The Dreamcast runs in a 640x480 RGB565
 * mode and the 320x240 image is pixel-doubled to fill the screen; offloading
 * the upscale to the PVR with a textured quad is a later optimisation.
 */

#include <kos.h>
#include <dc/video.h>

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

    /* 640x480 16-bit; the 320x240 image is doubled into it. */
    vid_set_mode(DM_640x480, PM_RGB565);

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
}

void
VID_Update(vrect_t *rects)
{
    int x, y;
    const uint8_t *src = vid.buffer;
    uint16_t *vram = vram_s;	/* KOS 16-bit framebuffer pointer */

    /* Full-frame 8->565 conversion with 2x pixel doubling (320x240 -> 640x480). */
    for (y = 0; y < BASEHEIGHT; y++) {
	uint16_t *d0 = vram + (y * 2) * 640;
	uint16_t *d1 = d0 + 640;
	const uint8_t *s = src + y * BASEWIDTH;

	for (x = 0; x < BASEWIDTH; x++) {
	    uint16_t c = d_8to16table[s[x]];
	    d0[0] = d0[1] = c;
	    d1[0] = d1[1] = c;
	    d0 += 2;
	    d1 += 2;
	}
    }

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
