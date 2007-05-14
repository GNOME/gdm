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

#ifndef GDM_H
#define GDM_H

#define GDM_MAX_PASS 256	/* Define a value for password length. Glibc
				 * leaves MAX_PASS undefined. */

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

/* The dreaded miscellaneous category */
#define PIPE_SIZE 4096

/*
 * For backwards compatibility, do not set values for DEFAULT_WELCOME or 
 * DEFAULT_REMOTEWELCOME.  This will cause these values to always be 
 * read from the config file, and will cause them to return FALSE if
 * no value is set in the config file.  We want the value "FALSE" if
 * the values don't exist in the config file.  The daemon will compare
 * the Welcome/RemoveWelcome value with the default string and 
 * automatically translate the text if the string is the same as the
 * default string.  We set the default values of GDM_KEY_WELCOME and
 * GDM_KEY_REMOTEWELCOME so that the default value is returned when
 * you run GET_CONFIG on these keys.
 */
#define GDM_DEFAULT_WELCOME_MSG "Welcome"
#define GDM_DEFAULT_REMOTE_WELCOME_MSG "Welcome to %n"

#define GDM_SESSION_FAILSAFE_GNOME "GDM_Failsafe.GNOME"
#define GDM_SESSION_FAILSAFE_XTERM "GDM_Failsafe.XTERM"

/* FIXME: will support these builtin types later */
#define GDM_SESSION_DEFAULT "default"
#define GDM_SESSION_CUSTOM "custom"
#define GDM_SESSION_FAILSAFE "failsafe"

#define GDM_STANDARD "Standard"

#define GDM_RESPONSE_CANCEL "GDM_RESPONSE_CANCEL"

#define GDM_CUSTOM_COMMAND_MAX 10 /* maximum number of supported custom commands */

#ifdef sun
#define SDTLOGIN_DIR "/var/dt/sdtlogin"
#endif

/* If id == NULL, then get the first X server */
void		gdm_final_cleanup	(void);

#endif /* GDM_H */

/* EOF */
