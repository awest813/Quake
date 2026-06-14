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
 * vmu_dc.c -- VMU save/config packaging (KallistiOS vmu_pkg).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kos.h>
#include <dc/vmu_pkg.h>

#include "console.h"
#include "quakedef.h"
#include "sys.h"
#include "vmu_dc.h"

/* One-frame 32x32 4bpp icon (brown/orange Quake tile in the centre). */
static const uint16_t dc_vmu_icon_pal[16] = {
    0x0000, 0xFFFF, 0xFD60, 0xFB40, 0xF920, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static uint8_t dc_vmu_icon_bits[512];

static struct {
    FILE *fp;
    char vmu[MAX_OSPATH];
    char ram[64];
} dc_stage;

static unsigned dc_read_serial;
static qboolean dc_icon_ready;

static void
DC_InitVMUIcon(void)
{
    int y, x;
    uint8_t n;

    if (dc_icon_ready)
	return;

    memset(dc_vmu_icon_bits, 0, sizeof(dc_vmu_icon_bits));
    for (y = 10; y < 22; y++) {
	for (x = 10; x < 22; x++) {
	    n = (uint8_t)((y > 11 && y < 20 && x > 11 && x < 20) ? 0x33 : 0x22);
	    dc_vmu_icon_bits[(y * 32 + x) / 2] |= (x & 1) ? n : (n << 4);
	}
    }
    dc_icon_ready = true;
}

qboolean
DC_IsVMUPath(const char *path)
{
    return path && !strncmp(path, "/vmu/", 5);
}

static int
DC_PackageAndWrite(const char *vmu_path, const void *data, int len)
{
    vmu_pkg_t pkg;
    uint8_t *out;
    int out_size;
    FILE *f;
    const char *base;

    if (len <= 0)
	return 0;

    DC_InitVMUIcon();

    memset(&pkg, 0, sizeof(pkg));
    base = strrchr(vmu_path, '/');
    base = base ? base + 1 : vmu_path;

    snprintf(pkg.desc_short, sizeof(pkg.desc_short), "%.16s", base);
    snprintf(pkg.desc_long, sizeof(pkg.desc_long), "Quake %.32s", base);
    strncpy(pkg.app_id, "TyrQuake\\DC", sizeof(pkg.app_id) - 1);
    pkg.icon_cnt = 1;
    pkg.icon_anim_speed = 0;
    pkg.eyecatch_type = VMUPKG_EC_NONE;
    memcpy(pkg.icon_pal, dc_vmu_icon_pal, sizeof(pkg.icon_pal));
    pkg.icon_data = dc_vmu_icon_bits;
    pkg.data = (const uint8_t *)data;
    pkg.data_len = len;

    if (vmu_pkg_build(&pkg, &out, &out_size) < 0)
	return -1;

    unlink(vmu_path);
    f = fopen(vmu_path, "wb");
    if (!f) {
	free(out);
	return -1;
    }
    fwrite(out, 1, out_size, f);
    fclose(f);
    free(out);
    return 0;
}

static FILE *
DC_OpenVMURead(const char *path)
{
    FILE *f, *out;
    long sz;
    uint8_t *raw;
    vmu_pkg_t pkg;
    const uint8_t *payload;
    int payload_len;
    char rampath[64];

    f = fopen(path, "rb");
    if (!f)
	return NULL;

    if (fseek(f, 0, SEEK_END) != 0) {
	fclose(f);
	return NULL;
    }
    sz = ftell(f);
    if (sz <= 0) {
	fclose(f);
	return NULL;
    }
    rewind(f);

    raw = malloc(sz);
    if (!raw) {
	fclose(f);
	return NULL;
    }
    if (fread(raw, 1, sz, f) != (size_t)sz) {
	free(raw);
	fclose(f);
	return NULL;
    }
    fclose(f);

    payload = raw;
    payload_len = (int)sz;
    if (vmu_pkg_parse(raw, sz, &pkg) == 0) {
	payload = pkg.data;
	payload_len = pkg.data_len;
    }

    snprintf(rampath, sizeof(rampath), "/ram/qdr%u", dc_read_serial++);
    out = fopen(rampath, "wb");
    if (!out) {
	free(raw);
	return NULL;
    }
    fwrite(payload, 1, payload_len, out);
    fclose(out);
    free(raw);

    return fopen(rampath, "rb");
}

FILE *
DC_FOpen(const char *path, const char *mode)
{
    if (!path || !mode)
	return NULL;

    if (!DC_IsVMUPath(path))
	return fopen(path, mode);

    if (mode[0] == 'r')
	return DC_OpenVMURead(path);

    if (mode[0] == 'w' || mode[0] == 'a') {
	if (dc_stage.fp)
	    return NULL;
	snprintf(dc_stage.ram, sizeof(dc_stage.ram), "/ram/qdc");
	strncpy(dc_stage.vmu, path, sizeof(dc_stage.vmu) - 1);
	dc_stage.vmu[sizeof(dc_stage.vmu) - 1] = 0;
	dc_stage.fp = fopen(dc_stage.ram, (mode[0] == 'a') ? "ab" : "wb");
	return dc_stage.fp;
    }

    return fopen(path, mode);
}

void
DC_FClose(FILE *file)
{
    FILE *f;
    long len;
    uint8_t *buf;

    if (!file)
	return;

    if (file == dc_stage.fp) {
	fclose(dc_stage.fp);
	dc_stage.fp = NULL;

	f = fopen(dc_stage.ram, "rb");
	if (!f)
	    return;
	if (fseek(f, 0, SEEK_END) != 0) {
	    fclose(f);
	    unlink(dc_stage.ram);
	    return;
	}
	len = ftell(f);
	rewind(f);
	if (len > 0) {
	    buf = malloc(len);
	    if (buf) {
		if (fread(buf, 1, len, f) == (size_t)len) {
		    if (DC_PackageAndWrite(dc_stage.vmu, buf, (int)len) < 0)
			Con_Printf("VMU: failed to write %s\n", dc_stage.vmu);
		}
		free(buf);
	    }
	}
	fclose(f);
	unlink(dc_stage.ram);
	return;
    }

    fclose(file);
}
