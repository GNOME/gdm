/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

#ifndef _GDM_DISPLAY_H
#define _GDM_DISPLAY_H

#include <X11/Xlib.h> /* for Display */
#include <X11/Xmd.h> /* for CARD32 */
#include <netinet/in.h> /* for in_addr */

typedef struct _GdmDisplay GdmDisplay;

#include "gdm-net.h" /* for GdmConnection */

/* DO NOTE USE 1, that's used as error if x connection fails usually */
/* Note that there is no reason why these were a power of two, and note
 * that they have to fit in 256 */
/* These are the exit codes */
#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_SUSPEND 17	/* Suspend (don't use, use the interrupt) */
#define DISPLAY_CHOSEN 20	/* successful chooser session,
				   restart display */
#define DISPLAY_RUN_CHOOSER 30	/* Run chooser */
#define DISPLAY_XFAILED 64	/* X failed */
#define DISPLAY_GREETERFAILED 65 /* greeter failed (crashed) */
#define DISPLAY_RESTARTGREETER 127 /* Restart greeter */
#define DISPLAY_RESTARTGDM 128	/* Restart GDM */

enum {
	DISPLAY_UNBORN /* Not yet started */,
	DISPLAY_ALIVE /* Yay! we're alive (non-XDMCP) */,
	XDMCP_PENDING /* Pending XDMCP display */,
	XDMCP_MANAGED /* Managed XDMCP display */,
	DISPLAY_DEAD /* Left for dead */,
	DISPLAY_CONFIG /* in process of being configured */
};

#define TYPE_STATIC 1		/* X server defined in GDM configuration */
#define TYPE_XDMCP 2		/* Remote display/Xserver */
#define TYPE_FLEXI 3		/* Local Flexi X server */
#define TYPE_FLEXI_XNEST 4	/* Local Flexi Nested server */
#define TYPE_XDMCP_PROXY 5	/* Proxy X server for XDMCP */

#define SERVER_IS_LOCAL(d) ((d)->type == TYPE_STATIC || \
			    (d)->type == TYPE_FLEXI || \
			    (d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_FLEXI(d) ((d)->type == TYPE_FLEXI || \
			    (d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_PROXY(d) ((d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_XDMCP(d) ((d)->type == TYPE_XDMCP || \
			    (d)->type == TYPE_XDMCP_PROXY)

/* Use this to get the right authfile name */
#define GDM_AUTHFILE(display) \
	(display->authfile_gdm != NULL ? display->authfile_gdm : display->authfile)

/* Values between GDM_LOGOUT_ACTION_CUSTOM_CMD_FIRST and
   GDM_LOGOUT_ACTION_CUSTOM_CMD_LAST are reserved and should not be used */
typedef enum {
	GDM_LOGOUT_ACTION_NONE = 0,
	GDM_LOGOUT_ACTION_HALT,
	GDM_LOGOUT_ACTION_REBOOT,
	GDM_LOGOUT_ACTION_SUSPEND,
	GDM_LOGOUT_ACTION_CUSTOM_CMD_FIRST,
	GDM_LOGOUT_ACTION_CUSTOM_CMD_LAST,
	GDM_LOGOUT_ACTION_LAST
} GdmLogoutAction;

struct _GdmDisplay
{
	/* ALL DISPLAY TYPES */

	guint8 type;
	Display *dsp;

	gchar *name;     /* value of DISPLAY */
	gchar *hostname; /* remote hostname */

	guint8 dispstat;
	guint16 dispnum;

	gboolean logged_in; /* TRUE if someone is logged in */
	char *login;

	gboolean attached;  /* Display is physically attached to the machine. */

	gboolean handled;
	gboolean tcp_disallowed;
	int priority;

	gboolean timed_login_ok;

	gboolean try_different_greeter;
	char *theme_name;

	time_t managetime; /* time the display was managed */

	/* loop check stuff */
	time_t last_start_time;
	time_t last_loop_start_time;
	gint retry_count;
	int sleep_before_run;

	gchar *cookie;
	gchar *bcookie;

	gchar *authfile;     /* authfile for the server */
	gchar *authfile_gdm; /* authfile readable by gdm user
				if necessary */
	GSList *auths;
	GSList *local_auths;
	gchar *userauth;
	gboolean authfb;
	time_t last_auth_touch;

	int screenx;
	int screeny;
	int screenwidth; /* Note 0 means use the gdk size */
	int screenheight;
	int lrh_offsetx; /* lower right hand corner x offset */
	int lrh_offsety; /* lower right hand corner y offset */

	pid_t slavepid;
	pid_t greetpid;
	pid_t sesspid;
	int last_sess_status; /* status returned by last session */

	/* Notification connection */
	int master_notify_fd;  /* write part of the connection */
	int slave_notify_fd; /* read part of the connection */
	/* The xsession-errors connection */
	int xsession_errors_fd; /* write to the file */
	int session_output_fd; /* read from the session */
	int xsession_errors_bytes;
#define MAX_XSESSION_ERRORS_BYTES (80*2500)  /* maximum number of bytes in
						the ~/.xsession-errors file */
	char *xsession_errors_filename; /* if NULL then there is no .xsession-errors
					   file */

	/* chooser stuff */
	pid_t chooserpid;
	gboolean use_chooser; /* run chooser instead of greeter */
	gchar *chosen_hostname; /* locally chosen hostname if not NULL,
				   "-query chosen_hostname" is appened to server command line */
	int chooser_output_fd; /* from the chooser */
	char *chooser_last_line;
	guint indirect_id;

	gboolean is_emergency_server;
	gboolean failsafe_xserver;

        gchar *xserver_session_args;

	/* Only set in the main daemon as that's the only place that cares */
	GdmLogoutAction logout_action;


	/* XDMCP TYPE */

	time_t acctime;

	int xdmcp_dispnum;
	CARD32 sessionid;

	struct sockaddr_storage addr;
	struct sockaddr_storage *addrs; /* array of addresses */
	int addr_count; /* number of addresses in array */
	/* Note that the above may in fact be empty even though
	   addr is set, these are just extra addresses
	   (it could also contain addr for all we know) */


	/* ALL LOCAL TYPE (static, flexi) */

	int vt;
	pid_t servpid;
	guint8 servstat;
	gchar *command;
	time_t starttime;
	/* order in the Xservers file for sessreg, -1 if unset yet */
	int x_servers_order;

	gboolean wait_for_go;

	/* STATIC TYPE */

	gboolean removeconf; /* used to mark "dynamic" static displays for removal */
	gboolean busy_display; /* only needed on static displays since flexi try another */
	time_t last_x_failed;
	int x_faileds;


	/* FLEXI TYPE */

	char *preset_user;
	uid_t server_uid;
	GdmConnection *socket_conn;


	/* PROXY/Parented TYPE (flexi-xnest or xdmcp proxy) */

	char *parent_disp;
	Display *parent_dsp;


	/* XDMCP PROXY TYPE */

	char *parent_auth_file;


	/* FLEXI XNEST TYPE */
	char *parent_temp_auth_file;
};

GdmDisplay *gdm_display_alloc    (gint id, const gchar *command);
gboolean    gdm_display_manage   (GdmDisplay *d);
void        gdm_display_dispose  (GdmDisplay *d);
void        gdm_display_unmanage (GdmDisplay *d);
GdmDisplay *gdm_display_lookup   (pid_t pid);

#endif /* _GDM_DISPLAY_H */

