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
 * analog triggers drive movement and look through IN_Move().  Defaults are chosen
 * so the menus and basic movement work out of the box on a single-stick pad.
 */

#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include "quakedef.h"
#include "client.h"
#include "cvar.h"
#include "host.h"
#include "input.h"
#include "keys.h"
#include "mathlib.h"
#include "sys.h"

cvar_t _windowed_mouse = { "_windowed_mouse", "0", true };

/* Analog stick dead zone (stick range is -128..127). */
#define DC_STICK_DEADZONE 24
/* Trigger threshold for treating an analog trigger as strafe input. */
#define DC_TRIG_THRESHOLD 8

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
    { CONT_A,          K_ENTER },	/* A: jump (see IN_DC_ApplyBindings) */
    { CONT_B,          K_SPACE },	/* B: attack */
    { CONT_X,          K_CTRL },	/* X: use */
    { CONT_Y,          K_SHIFT },	/* Y: run */
    { CONT_START,      K_ESCAPE },	/* Start: menu */
};

#define DC_NUM_BUTTONS (sizeof(dc_buttonmap) / sizeof(dc_buttonmap[0]))

void
IN_DC_ApplyBindings(void)
{
    /*
     * Applied after quake.rc so a Dreamcast pad is playable without a mouse.
     * Stick X/Y and triggers are handled in IN_Move(); these cover buttons and
     * the d-pad (strafe / look).
     */
    Key_SetBinding(K_ENTER, "+jump");
    Key_SetBinding(K_SPACE, "+attack");
    Key_SetBinding(K_CTRL, "+use");
    Key_SetBinding(K_SHIFT, "+speed");
    Key_SetBinding(K_ESCAPE, "togglemenu");
    Key_SetBinding(K_UPARROW, "+lookup");
    Key_SetBinding(K_DOWNARROW, "+lookdown");
    Key_SetBinding(K_LEFTARROW, "+moveleft");
    Key_SetBinding(K_RIGHTARROW, "+moveright");
}

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
    changed = buttons ^ old_buttons;

    for (i = 0; i < DC_NUM_BUTTONS; i++) {
	uint32_t bit = dc_buttonmap[i].cont;
	if (changed & bit)
	    Key_Event(dc_buttonmap[i].key, (buttons & bit) != 0);
    }

    old_buttons = buttons;
}

/*
================
IN_Move -- analog stick drives turn/forward; triggers strafe
================
*/
void
IN_Move(usercmd_t *cmd)
{
    maple_device_t *dev;
    cont_state_t *st;
    int jx, jy;
    float speed;

    dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev)
	return;

    st = (cont_state_t *)maple_dev_status(dev);
    if (!st)
	return;

    jx = st->joyx;
    jy = st->joyy;

    if (cls.state == ca_active) {
	if ((in_speed.state & 1) ^ (int)cl_run.value)
	    speed = host_frametime * cl_anglespeedkey.value;
	else
	    speed = host_frametime;

	if (jx > DC_STICK_DEADZONE || jx < -DC_STICK_DEADZONE) {
	    cl.viewangles[YAW] -=
		speed * cl_yawspeed.value * (jx / 128.0f);
	    cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
	}
    }

    if (jy > DC_STICK_DEADZONE || jy < -DC_STICK_DEADZONE)
	cmd->forwardmove -= cl_forwardspeed.value * (jy / 128.0f);

    if (st->ltrig > DC_TRIG_THRESHOLD)
	cmd->sidemove -= cl_sidespeed.value * (st->ltrig / 255.0f);
    if (st->rtrig > DC_TRIG_THRESHOLD)
	cmd->sidemove += cl_sidespeed.value * (st->rtrig / 255.0f);
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
