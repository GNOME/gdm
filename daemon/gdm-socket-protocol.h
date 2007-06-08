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

#ifndef _GDM_SOCKET_PROTOCOL_H
#define _GDM_SOCKET_PROTOCOL_H

#include <glib.h>

#define STX 0x2			/* Start of txt */
#define BEL 0x7			/* Bell, used to interrupt login for
				 * say timed login or something similar */

/*
 * Opcodes for the highly sophisticated protocol used for
 * daemon<->greeter communications
 */

/* This will change if there are incompatible
 * protocol changes */
#define GDM_GREETER_PROTOCOL_VERSION "3"

#define GDM_MSG        'D'
#define GDM_NOECHO     'U'
#define GDM_PROMPT     'N'
#define GDM_SESS       'G'
#define GDM_LANG       '&'
#define GDM_SSESS      'C'
#define GDM_SLANG      'R'
#define GDM_SETLANG    'L'
#define GDM_RESET      'A'
#define GDM_QUIT       'P'
/* Well these aren't as nice as above, oh well */
#define GDM_STARTTIMER 's'
#define GDM_STOPTIMER  'S'
#define GDM_SETLOGIN   'l' /* this just sets the login to be this, just for
			      the greeters knowledge */
#define GDM_DISABLE    '-' /* disable the login screen */
#define GDM_ENABLE     '+' /* enable the login screen */
#define GDM_RESETOK    'r' /* reset but don't shake */
#define GDM_NEEDPIC    '#' /* need a user picture?, sent after greeter
			    *  is started */
#define GDM_READPIC    '%' /* Send a user picture in a temp file */
#define GDM_ERRBOX     'e' /* Puts string in the error box */
#define GDM_ERRDLG     'E' /* Puts string up in an error dialog */
#define GDM_NOFOCUS    'f' /* Don't focus the login window (optional) */
#define GDM_FOCUS      'F' /* Allow focus on the login window again (optional) */
#define GDM_SAVEDIE    '!' /* Save wm order and die (and set busy cursor) */
#define GDM_QUERY_CAPSLOCK 'Q' /* Is capslock on? */
#define GDM_ALWAYS_RESTART 'W' /* Retart greeter when the user accepts restarts */

/* Different login interruptions */
#define GDM_INTERRUPT_TIMED_LOGIN 'T'
#define GDM_INTERRUPT_CONFIGURE   'C'
#define GDM_INTERRUPT_SUSPEND     'S'
#define GDM_INTERRUPT_SELECT_USER 'U'
#define GDM_INTERRUPT_LOGIN_SOUND 'L'
#define GDM_INTERRUPT_THEME       'H'
#define GDM_INTERRUPT_CUSTOM_CMD  'M'
#define GDM_INTERRUPT_CANCEL      'X'
#define GDM_INTERRUPT_SELECT_LANG 'O'



/*
 * primitive protocol for controlling the daemon from slave
 * or gdmconfig or whatnot
 */

/* The ones that pass a <slave pid> must be from a valid slave, and
 * the slave will be sent a SIGUSR2.  Nowdays there is a pipe that is
 * used from inside slaves, so those messages may stop being processed
 * by the fifo at some point perhaps.  */
/* The fifo protocol, used only by gdm internally */
#define GDM_SOP_CHOSEN       "CHOSEN" /* <indirect id> <ip addr> */
#define GDM_SOP_CHOSEN_LOCAL "CHOSEN_LOCAL" /* <slave pid> <hostname> */
#define GDM_SOP_XPID         "XPID" /* <slave pid> <xpid> */
#define GDM_SOP_SESSPID      "SESSPID" /* <slave pid> <sesspid> */
#define GDM_SOP_GREETPID     "GREETPID" /* <slave pid> <greetpid> */
#define GDM_SOP_CHOOSERPID   "CHOOSERPID" /* <slave pid> <chooserpid> */
#define GDM_SOP_LOGGED_IN    "LOGGED_IN" /* <slave pid> <logged_in as int> */
#define GDM_SOP_LOGIN        "LOGIN" /* <slave pid> <username> */
#define GDM_SOP_COOKIE       "COOKIE" /* <slave pid> <cookie> */
#define GDM_SOP_AUTHFILE     "AUTHFILE" /* <slave pid> <authfile> */
#define GDM_SOP_QUERYLOGIN   "QUERYLOGIN" /* <slave pid> <username> */
/* if user already logged in somewhere, the ack response will be
   <display>,<migratable>,<display>,<migratable>,... */
#define GDM_SOP_MIGRATE      "MIGRATE" /* <slave pid> <display> */
#define GDM_SOP_DISP_NUM     "DISP_NUM" /* <slave pid> <display as int> */
/* For Linux only currently */
#define GDM_SOP_VT_NUM       "VT_NUM" /* <slave pid> <vt as int> */
#define GDM_SOP_FLEXI_ERR    "FLEXI_ERR" /* <slave pid> <error num> */
	/* 3 = X failed */
	/* 4 = X too busy */
	/* 5 = Nest display can't connect */
#define GDM_SOP_FLEXI_OK     "FLEXI_OK" /* <slave pid> */
#define GDM_SOP_SOFT_RESTART "SOFT_RESTART" /* no arguments */
#define GDM_SOP_START_NEXT_LOCAL "START_NEXT_LOCAL" /* no arguments */
#define GDM_SOP_HUP_ALL_GREETERS "HUP_ALL_GREETERS" /* no arguments */

/* stop waiting for this and go on with your life, useful with
   the --wait-for-go command line option */
#define GDM_SOP_GO "GO" /* no arguments */

/* sometimes we can't do a syslog so we tell the main daemon */
#define GDM_SOP_SYSLOG "SYSLOG" /* <pid> <type> <message> */

/* write out a sessreg (xdm) compatible Xservers file
 * in the ServAuthDir as <name>.Xservers */
#define GDM_SOP_WRITE_X_SERVERS "WRITE_X_SERVERS" /* <slave pid> */

/* All X servers should be restarted rather then regenerated.  Useful
 * if you have updated the X configuration.  Note that this happens
 * only when the user logs out or when we otherwise would have restarted
 * a server, nothing is done by this command. */
#define GDM_SOP_DIRTY_SERVERS "DIRTY_SERVERS"  /* no arguments */

/* restart all servers that people aren't logged in on.  Maybe you may not
 * want to do this on every change of X server config since this may cause
 * flicker on screen and jumping around on the vt.  Perhaps useful to do
 * by asking the user if they want to do that.  Note that this will not
 * kill any logged in sessions. */
#define GDM_SOP_SOFT_RESTART_SERVERS "SOFT_RESTART_SERVERS"  /* no arguments */
/* Suspend the machine if it is even allowed */
#define GDM_SOP_SUSPEND_MACHINE "SUSPEND_MACHINE"  /* no arguments */
#define GDM_SOP_CHOSEN_THEME "CHOSEN_THEME"  /* <slave pid> <theme name> */

/*Execute custom cmd*/
#define GDM_SOP_CUSTOM_CMD "CUSTOM_CMD"  /* <slave pid> <cmd id> */

/* Start a new standard X flexible server */
#define GDM_SOP_FLEXI_XSERVER "FLEXI_XSERVER" /* no arguments */

#define GDM_SOP_SHOW_ERROR_DIALOG "SHOW_ERROR_DIALOG"  /* show the error dialog from daemon */
#define GDM_SOP_SHOW_YESNO_DIALOG "SHOW_YESNO_DIALOG"  /* show the yesno dialog from daemon */
#define GDM_SOP_SHOW_QUESTION_DIALOG "SHOW_QUESTION_DIALOG"  /* show the question dialog from daemon */
#define GDM_SOP_SHOW_ASKBUTTONS_DIALOG "SHOW_ASKBUTTON_DIALOG"  /* show the askbutton dialog from daemon */


/* Ack for a slave message */
/* Note that an extra response can follow an 'ack' */
#define GDM_SLAVE_NOTIFY_ACK 'A'
/* Update this key */
#define GDM_SLAVE_NOTIFY_KEY '!'
/* notify a command */
#define GDM_SLAVE_NOTIFY_COMMAND '#'
/* send the response */
#define GDM_SLAVE_NOTIFY_RESPONSE 'R'
/* send the error dialog response */
#define GDM_SLAVE_NOTIFY_ERROR_RESPONSE 'E'
/* send the yesno dialog response */
#define GDM_SLAVE_NOTIFY_YESNO_RESPONSE 'Y'
/* send the askbuttons dialog response */
#define GDM_SLAVE_NOTIFY_ASKBUTTONS_RESPONSE 'B'
/* send the question dialog response */
#define GDM_SLAVE_NOTIFY_QUESTION_RESPONSE 'Q'

/*
 * Maximum number of messages allowed over the sockets protocol.  This
 * is set to 80 since the gdmlogin/gdmgreeter programs have ~60 config
 * values that are pulled over the socket connection so it allows them
 * all to be grabbed in one pull.
 */
#define GDM_SUP_MAX_MESSAGES 80
#define GDM_SUP_SOCKET "/tmp/.gdm_socket"

/*
 * The user socket protocol.  Each command is given on a separate line
 *
 * A user should first send a VERSION\n after connecting and only do
 * anything else if gdm responds with the correct response.  The version
 * is the gdm version and not a "protocol" revision, so you can't check
 * against a single version but check if the version is higher then some
 * value.
 *
 * You can only send a few commands at a time, so if you keep getting error
 * 200 try opening a new socket for every command you send.
 *
 * For a descriptions of the commands see:
 * http://www.gnome.org/projects/gdm/docs/2.18/controlling.htlm
 *
 */
/* The user protocol, using /tmp/.gdm_socket */


#define GDM_SUP_VERSION "VERSION"
#define GDM_SUP_AUTH_LOCAL "AUTH_LOCAL"
#define GDM_SUP_FLEXI_XSERVER "FLEXI_XSERVER"
#define GDM_SUP_FLEXI_XSERVER_USER  "FLEXI_XSERVER_USER"
#define GDM_SUP_FLEXI_XNEST  "FLEXI_XNEST"
#define GDM_SUP_FLEXI_XNEST_USER  "FLEXI_XNEST_USER"
#define GDM_SUP_ADD_DYNAMIC_DISPLAY	"ADD_DYNAMIC_DISPLAY"
#define GDM_SUP_RELEASE_DYNAMIC_DISPLAYS	"RELEASE_DYNAMIC_DISPLAYS"
#define GDM_SUP_REMOVE_DYNAMIC_DISPLAY	"REMOVE_DYNAMIC_DISPLAY"
#define GDM_SUP_ATTACHED_SERVERS "ATTACHED_SERVERS"
#define GDM_SUP_CONSOLE_SERVERS  "CONSOLE_SERVERS"
#define GDM_SUP_ALL_SERVERS  "ALL_SERVERS"
#define GDM_SUP_GET_SERVER_LIST "GET_SERVER_LIST"
#define GDM_SUP_GET_SERVER_DETAILS "GET_SERVER_DETAILS"
#define GDM_SUP_GET_CONFIG "GET_CONFIG"
#define GDM_SUP_GET_CONFIG_FILE  "GET_CONFIG_FILE"
#define GDM_SUP_GET_CUSTOM_CONFIG_FILE  "GET_CUSTOM_CONFIG_FILE"
#define GDM_SUP_UPDATE_CONFIG "UPDATE_CONFIG"
#define GDM_SUP_GREETERPIDS  "GREETERPIDS"
#define GDM_SUP_QUERY_LOGOUT_ACTION "QUERY_LOGOUT_ACTION"
#define GDM_SUP_QUERY_CUSTOM_CMD_LABELS "QUERY_CUSTOM_CMD_LABELS"
#define GDM_SUP_QUERY_CUSTOM_CMD_NO_RESTART_STATUS "QUERY_CUSTOM_CMD_NO_RESTART_STATUS"
#define GDM_SUP_SET_LOGOUT_ACTION "SET_LOGOUT_ACTION"
#define GDM_SUP_SET_SAFE_LOGOUT_ACTION "SET_SAFE_LOGOUT_ACTION"
#define GDM_SUP_LOGOUT_ACTION_NONE	          "NONE"
#define GDM_SUP_LOGOUT_ACTION_HALT	          "HALT"
#define GDM_SUP_LOGOUT_ACTION_REBOOT	          "REBOOT"
#define GDM_SUP_LOGOUT_ACTION_SUSPEND	          "SUSPEND"
#define GDM_SUP_LOGOUT_ACTION_CUSTOM_CMD_TEMPLATE "CUSTOM_CMD"
#define GDM_SUP_QUERY_VT "QUERY_VT"
#define GDM_SUP_SET_VT "SET_VT"
#define GDM_SUP_SERVER_BUSY "SERVER_BUSY"
#define GDM_SUP_GET_SERVER_DETAILS "GET_SERVER_DETAILS"
#define GDM_SUP_CLOSE        "CLOSE"

/* User flags for the SUP protocol */
enum {
	GDM_SUP_FLAG_AUTHENTICATED = 0x1, /* authenticated as a local user,
					  * from a local display we started */
	GDM_SUP_FLAG_AUTH_GLOBAL = 0x2 /* authenticated with global cookie */
};

#endif /* _GDM_SOCKET_PROTOCOL_H */
