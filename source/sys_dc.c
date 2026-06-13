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
 * sys_dc.c -- Sega Dreamcast / KallistiOS system layer.
 *
 * Replaces sys_unix.c for the Dreamcast target.  KallistiOS provides a newlib
 * POSIX layer (open/read/stat/mkdir/gettimeofday), so the file routines are
 * close to the Unix ones; the differences are that there is no command line,
 * no stdin, and no signal/mman, and that KOS must be initialised before main().
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

#include <kos.h>
#include <dc/maple.h>

#include "common.h"
#include "sys.h"
#include "zone.h"
#include "quakedef.h"
#include "client.h"
#include "host.h"

qboolean isDedicated;

/*
 * Initialise KOS.  INIT_DEFAULT brings up threads, the maple bus (controllers)
 * and the GD-ROM filesystem mounted at /cd, which is where the game data lives.
 */
KOS_INIT_FLAGS(INIT_DEFAULT);

static qboolean nostdout = false;

/*
 * ===========================================================================
 * General Routines
 * ===========================================================================
 */

void
Sys_Printf(const char *fmt, ...)
{
    va_list argptr;
    char text[MAX_PRINTMSG];
    uint8_t *p;

    va_start(argptr, fmt);
    vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    if (nostdout)
	return;

    /* dbgio routes to the serial port / dc-tool console. */
    for (p = (uint8_t *)text; *p; p++) {
	if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
	    printf("[%02x]", *p);
	else
	    putc(*p, stdout);
    }
}

void
Sys_Quit(void)
{
    Host_Shutdown();
    fflush(stdout);
    arch_exit();
}

void
Sys_Init(void)
{
    Sys_SetFPCW();
}

void
Sys_Error(const char *error, ...)
{
    va_list argptr;
    char string[MAX_PRINTMSG];

    va_start(argptr, error);
    vsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);
    fprintf(stderr, "Error: %s\n", string);

    Host_Shutdown();
    arch_exit();
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int
Sys_FileTime(const char *path)
{
    struct stat buf;

    if (stat(path, &buf) == -1)
	return -1;

    return buf.st_mtime;
}

void
Sys_mkdir(const char *path)
{
    /* On DC writable storage is the VMU; /cd is read-only.  Best-effort. */
    mkdir(path, 0777);
}

double
Sys_DoubleTime(void)
{
    static uint64_t secbase;
    uint64_t now = timer_us_gettime64();

    if (!secbase) {
	secbase = now;
	return 0.0;
    }

    return (double)(now - secbase) / 1000000.0;
}

char *
Sys_ConsoleInput(void)
{
    /* No console input on the Dreamcast. */
    return NULL;
}

void
Sys_Sleep(void)
{
    thd_sleep(1);
}

void
Sys_DebugLog(const char *file, const char *fmt, ...)
{
    va_list argptr;
    static char data[MAX_PRINTMSG];
    int fd;

    va_start(argptr, fmt);
    vsnprintf(data, sizeof(data), fmt, argptr);
    va_end(argptr);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
	return;
    write(fd, data, strlen(data));
    close(fd);
}

#ifndef USE_X86_ASM
void Sys_HighFPPrecision(void) {}
void Sys_LowFPPrecision(void) {}
void Sys_SetFPCW(void) {}
#endif

void
Sys_MakeCodeWriteable(void *start_addr, void *end_addr)
{
    /* Flat memory model, nothing to do. */
}

/*
 * ===========================================================================
 * Main
 * ===========================================================================
 */

int
main(int argc, char *argv[])
{
    double time, oldtime, newtime;
    quakeparms_t parms;

    /*
     * The Dreamcast has no command line, so synthesise a fixed argv.  The heap
     * size is decided by Memory_GetSize() (capped at 8MB for DC), so there is
     * no need to pass -mem here.
     */
    static const char *dc_argv[] = { "quake", NULL };

    memset(&parms, 0, sizeof(parms));

    COM_InitArgv(1, dc_argv);
    parms.argc = com_argc;
    parms.argv = com_argv;
    parms.basedir = "/cd";
    parms.memsize = Memory_GetSize();
    parms.membase = malloc(parms.memsize);
    if (!parms.membase)
	Sys_Error("Allocation of %d byte heap failed", parms.memsize);

    printf("Quake -- TyrQuake Version %s (Dreamcast)\n", stringify(TYR_VERSION));

    Sys_Init();
    Host_Init(&parms);

    oldtime = Sys_DoubleTime() - 0.1;
    while (1) {
	newtime = Sys_DoubleTime();
	time = newtime - oldtime;

	if (time > sys_ticrate.value * 2)
	    oldtime = newtime;
	else
	    oldtime += time;

	Host_Frame(time);
    }

    return 0;
}
