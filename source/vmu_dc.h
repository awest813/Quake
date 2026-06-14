/*
 * vmu_dc.h -- VMU file packaging for the Dreamcast target.
 *
 * The VMU filesystem is flat (no subdirectories) and expects VMS headers on
 * each file.  These helpers stage writes through /ram, wrap payloads with
 * vmu_pkg_build on close, and unwrap packages on open.
 */

#ifndef VMU_DC_H
#define VMU_DC_H

#include <stdio.h>

#include "qtypes.h"

#ifdef DREAMCAST

qboolean DC_IsVMUPath(const char *path);
FILE *DC_FOpen(const char *path, const char *mode);
void DC_FClose(FILE *file);

#else

static inline qboolean DC_IsVMUPath(const char *path)
{
    (void)path;
    return false;
}

#define DC_FOpen(path, mode) fopen(path, mode)
#define DC_FClose(file)      fclose(file)

#endif

#endif /* VMU_DC_H */
