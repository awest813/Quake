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
 * snd_dc.c -- Sega Dreamcast (AICA) sound backend.
 *
 * Quake's mixer writes 16-bit stereo samples into shm->buffer.  We keep a ring
 * buffer between the mixer and KOS's snd_stream; SNDDMA_Submit copies the newly
 * mixed samples into the ring and then pumps the stream with snd_stream_poll(),
 * which invokes our callback on the SAME thread -- so no locking is required.
 */

#include <string.h>

#include <kos.h>
#include <dc/sound/stream.h>

#include "console.h"
#include "quakedef.h"
#include "sound.h"
#include "sys.h"

static dma_t the_shm;
static int snd_inited;
static snd_stream_hnd_t stream_hnd = SND_STREAM_INVALID;

#define DC_SND_SPEED    22050
#define DC_SND_BITS     16
#define DC_SND_CHANNELS 2
#define DC_SND_SAMPLES  16384	/* total samples (both channels), power of two */

/* Ring buffer shared with the stream callback. */
static unsigned dc_buflen;
static uint8_t *dc_buf;
static unsigned rpos;
static unsigned wpos;

/* Linear scratch handed back to KOS each callback. */
static uint8_t cbbuf[SND_STREAM_BUFFER_MAX];

static void *
stream_cb(snd_stream_hnd_t hnd, int smp_req, int *smp_recv)
{
    int len = smp_req;
    int off = 0;

    if (len > (int)sizeof(cbbuf))
	len = sizeof(cbbuf);

    while (len > 0) {
	unsigned avail = dc_buflen - rpos;
	unsigned chunk = ((unsigned)len < avail) ? (unsigned)len : avail;

	memcpy(cbbuf + off, dc_buf + rpos, chunk);
	rpos += chunk;
	off += chunk;
	len -= chunk;
	if (rpos >= dc_buflen)
	    rpos = 0;
    }

    *smp_recv = off;
    return cbbuf;
}

qboolean
SNDDMA_Init(void)
{
    snd_inited = 0;

    if (snd_stream_init() < 0) {
	Con_Printf("Couldn't init DC sound stream\n");
	return false;
    }

    stream_hnd = snd_stream_alloc(stream_cb, SND_STREAM_BUFFER_MAX);
    if (stream_hnd == SND_STREAM_INVALID) {
	Con_Printf("Couldn't allocate DC sound stream\n");
	return false;
    }

    shm = &the_shm;
    shm->samplebits = DC_SND_BITS;
    shm->speed = DC_SND_SPEED;
    shm->channels = DC_SND_CHANNELS;
    shm->samples = DC_SND_SAMPLES;
    shm->samplepos = 0;
    shm->submission_chunk = 1;

    dc_buflen = shm->samples * (shm->samplebits / 8);
    shm->buffer = Hunk_AllocName(dc_buflen, "shm->buffer");
    dc_buf = Hunk_AllocName(dc_buflen, "dc_buf");
    if (!shm->buffer || !dc_buf)
	Sys_Error("%s: Failed to allocate sound buffer!", __func__);

    memset(shm->buffer, 0, dc_buflen);
    memset(dc_buf, 0, dc_buflen);

    rpos = wpos = 0;
    snd_blocked = 0;
    snd_inited = 1;

    snd_stream_start(stream_hnd, shm->speed, 1 /* stereo */);

    return true;
}

int
SNDDMA_GetDMAPos(void)
{
    if (!snd_inited)
	return 0;

    shm->samplepos = rpos / (shm->samplebits / 8);
    return shm->samplepos;
}

void
SNDDMA_Shutdown(void)
{
    if (!snd_inited)
	return;

    if (stream_hnd != SND_STREAM_INVALID) {
	snd_stream_stop(stream_hnd);
	snd_stream_destroy(stream_hnd);
	stream_hnd = SND_STREAM_INVALID;
    }
    snd_stream_shutdown();
    snd_inited = 0;
}

/*
==============
SNDDMA_Submit -- copy freshly mixed samples into the ring and pump the stream
===============
*/
void
SNDDMA_Submit(void)
{
    static unsigned old_paintedtime;
    unsigned len;

    if (!snd_inited || snd_blocked)
	return;

    if (paintedtime < old_paintedtime)
	old_paintedtime = 0;

    len = paintedtime - old_paintedtime;
    len *= shm->channels * (shm->samplebits / 8);
    old_paintedtime = paintedtime;

    while (wpos + len > dc_buflen) {
	memcpy(dc_buf + wpos, shm->buffer + wpos, dc_buflen - wpos);
	len -= dc_buflen - wpos;
	wpos = 0;
    }
    if (len) {
	memcpy(dc_buf + wpos, shm->buffer + wpos, len);
	wpos += len;
    }

    /* Drives stream_cb() on this thread -- refills the AICA buffers. */
    snd_stream_poll(stream_hnd);
}

int SNDDMA_LockBuffer(void) { return 0; }
void SNDDMA_UnlockBuffer(void) { }
