/* GDM - The Gnome Display Manager
 * Copyright (C) 1998 Martin Kasper Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __GDM_H__
#define __GDM_H__

#define DISPLAY_LOCAL 1		/* Local X server */
#define DISPLAY_XDMCP 2		/* Remote display */

#define SERVER_SUCCESS 0	/* X server default */
#define SERVER_FAILURE 1	/* X server default */
#define SERVER_NOTFOUND 127	/* X server default */
#define SERVER_DEAD 250		/* Server stopped */
#define SERVER_STARTED 251	/* Server started but not ready for connections yet */
#define SERVER_RUNNING 252	/* Server running and ready for connections */
#define SERVER_ABORT 253	/* Server failed badly. Suspending display. */

#define DISPLAY_SUCCESS 0	/* All systems are go */
#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_DEAD 32		/* Display not configured/started yet */
#define DISPLAY_RESERVER 64	/* XIO Error */

#define XDMCP_DEAD 0
#define XDMCP_PENDING 1
#define XDMCP_MANAGED 2

#define GDM_MSGERR 'M'
#define GDM_NOECHO 'K'
#define GDM_PROMPT 'P'
#define GDM_SESS   '.'
#define GDM_LANG   'N'
#define GDM_SSESS  'E'
#define GDM_SLANG  'T'
#define GDM_RESET  '!'
#define GDM_QUIT   'Q'


#define FIELD_SIZE 64
#define PIPE_SIZE 1024

#include <X11/Xlib.h>
#include <X11/Xmd.h>

typedef struct _GdmDisplay GdmDisplay;

struct _GdmDisplay {
    CARD32 sessionid;
    Display *dsp;
    gchar *auth;
    gchar *command;
    gchar *cookie;
    gchar *bcookie;
    gchar *name;
    gint dispstat;
    gint id;
    gint servstat;
    gint type;
    gint dispnum;
    pid_t greetpid;
    pid_t servpid;
    pid_t sesspid;
    pid_t slavepid;
    time_t acctime;
};

#endif /* __GDM_H__ */

/* EOF */
