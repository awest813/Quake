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
 * in_dc.c -- Sega Dreamcast input via the Maple bus.
 *
 * The first Maple controller is polled every frame.  Buttons are diffed against
 * the previous frame and turned into Key_Event()s; the analog stick and the
 * analog triggers drive movement through IN_Move().  Defaults are chosen so the
 * menus and basic movement work out of the box, while everything stays
 * rebindable from the console.
 */

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "quakedef.h"
#include "client.h"
#include "cvar.h"
#include "keys.h"
#include "sys.h"

cvar_t _windowed_mouse = { "_windowed_mouse", "0", true };

/* Analog stick dead zone (stick range is -128..127). */
#define DC_STICK_DEADZONE 24
/* Trigger threshold for treating an analog trigger as a digital button. */
#define DC_TRIG_THRESHOLD 64

static uint32_t old_buttons = 0;

/* Map of (Maple button bit -> Quake key).  Order is irrelevant. */
static const struct {
    uint32_t cont;
    int key;
} dc_buttonmap[] = {
    { CONT_DPAD_UP,    K_UPARROW },
    { CONT_DPAD_DOWN,  K_DOWNARROW },
    { CONT_DPAD_LEFT,  K_LEFTARROW },
    { CONT_DPAD_RIGHT, K_RIGHTARROW },
    { CONT_A,          K_ENTER },	/* menu accept / bindable (e.g. +jump) */
    { CONT_B,          K_SPACE },	/* +jump by default bind */
    { CONT_X,          K_CTRL },	/* +attack */
    { CONT_Y,          K_SHIFT },	/* +speed (run) */
    { CONT_START,      K_ESCAPE },	/* menu */
};

#define DC_NUM_BUTTONS (sizeof(dc_buttonmap) / sizeof(dc_buttonmap[0]))

void
IN_Init(void)
{
    old_buttons = 0;
}

void
IN_Shutdown(void)
{
}

void
IN_Commands(void)
{
}

/*
================
Sys_SendKeyEvents -- poll the controller and emit key up/down transitions
================
*/
void
Sys_SendKeyEvents(void)
{
    maple_device_t *dev;
    cont_state_t *st;
    uint32_t buttons, changed;
    unsigned i;

    dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev)
	return;

    st = (cont_state_t *)maple_dev_status(dev);
    if (!st)
	return;

    buttons = st->buttons;

    /* Treat the analog triggers as two extra digital buttons. */
    if (st->ltrig > DC_TRIG_THRESHOLD)
	buttons |= CONT_C;	/* left trigger -> reuse spare bit */
    if (st->rtrig > DC_TRIG_THRESHOLD)
	buttons |= CONT_Z;	/* right trigger -> reuse spare bit */

    changed = buttons ^ old_buttons;

    for (i = 0; i < DC_NUM_BUTTONS; i++) {
	uint32_t bit = dc_buttonmap[i].cont;
	if (changed & bit)
	    Key_Event(dc_buttonmap[i].key, (buttons & bit) != 0);
    }

    /* Triggers as rebindable AUX keys. */
    if (changed & CONT_C)
	Key_Event(K_AUX1, (buttons & CONT_C) != 0);	/* left trigger */
    if (changed & CONT_Z)
	Key_Event(K_AUX2, (buttons & CONT_Z) != 0);	/* right trigger */

    old_buttons = buttons;
}

/*
================
IN_Move -- analog stick drives strafing/forward movement
================
*/
void
IN_Move(usercmd_t *cmd)
{
    maple_device_t *dev;
    cont_state_t *st;
    int jx, jy;

    dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev)
	return;

    st = (cont_state_t *)maple_dev_status(dev);
    if (!st)
	return;

    jx = st->joyx;
    jy = st->joyy;

    if (jx > DC_STICK_DEADZONE || jx < -DC_STICK_DEADZONE)
	cmd->sidemove += cl_sidespeed.value * (jx / 128.0f);
    if (jy > DC_STICK_DEADZONE || jy < -DC_STICK_DEADZONE)
	cmd->forwardmove -= cl_forwardspeed.value * (jy / 128.0f);
}

/*
===========
IN_ModeChanged
===========
*/
void
IN_ModeChanged(void)
{
}
