/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-signal-handler.h"

#define GDM_SIGNAL_HANDLER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIGNAL_HANDLER, GdmSignalHandlerPrivate))

typedef struct {
	GdmSignalHandlerFunc func;
	gpointer             data;
} CallbackData;

struct GdmSignalHandlerPrivate
{
	GMainLoop  *main_loop;
	GHashTable *lookup;
};

static void	gdm_signal_handler_class_init	(GdmSignalHandlerClass *klass);
static void	gdm_signal_handler_init	        (GdmSignalHandler      *signal_handler);
static void	gdm_signal_handler_finalize	(GObject	       *object);

static gpointer signal_handler_object = NULL;
static int      signal_pipes[2];
static int      signals_blocked = 0;
static sigset_t signals_block_mask;
static sigset_t signals_oldmask;

G_DEFINE_TYPE (GdmSignalHandler, gdm_signal_handler, G_TYPE_OBJECT)

static void
block_signals_push (void)
{
	signals_blocked++;

	if (signals_blocked == 1) {
		/* Set signal mask */
		sigemptyset (&signals_block_mask);
		sigfillset (&signals_block_mask);
		sigprocmask (SIG_BLOCK, &signals_block_mask, &signals_oldmask);
	}
}

static void
block_signals_pop (void)
{
	signals_blocked--;

	if (signals_blocked == 0) {
		/* Set signal mask */
		sigprocmask (SIG_SETMASK, &signals_oldmask, NULL);
	}
}

static gboolean
signal_io_watch (GIOChannel       *ioc,
		 GIOCondition      condition,
		 GdmSignalHandler *handler)
{
	char     buf[256];
	gboolean is_fatal;
	gsize    bytes_read;
	int      i;

	block_signals_push ();

	g_io_channel_read_chars (ioc, buf, sizeof (buf), &bytes_read, NULL);

	is_fatal = FALSE;

	g_debug ("Read %d chars", (int)bytes_read);

	for (i = 0; i < bytes_read; i++) {
		int     signum;
		GSList *handlers;
		GSList *l;

		signum = (gint32)buf[i];

		g_debug ("handling signal %d", signum);
		handlers = g_hash_table_lookup (handler->priv->lookup, GINT_TO_POINTER (signum));

		g_debug ("Found %u callbacks", g_slist_length (handlers));
		for (l = handlers; l != NULL; l = l->next) {
			gboolean      res;
			CallbackData *data;

			data = l->data;
			g_debug ("running %d handler: %p", signum, data->func);
			res = data->func (signum, data->data);
			if (! res) {
				is_fatal = TRUE;
			}
		}
	}

	block_signals_pop ();

	if (is_fatal) {
		g_debug ("Caught termination signal - exiting main loop");
		g_main_loop_quit (handler->priv->main_loop);
		return FALSE;
	}

	g_debug ("Done handling signals");

        return TRUE;
}

static void
signal_handler (int signo)
{
	int ignore;

	/* FIXME: should probably use int32 here */
	ignore = write (signal_pipes [1], (guchar *)&signo, 1);
}

static void
catch_signal (GdmSignalHandler *handler,
	      int               signal_number)
{
        struct sigaction action;

        action.sa_handler = signal_handler;
        sigemptyset (&action.sa_mask);
        action.sa_flags = 0;

	/* FIXME: save old action? */
	sigaction (signal_number, &action, NULL);
}

void
gdm_signal_handler_add (GdmSignalHandler    *handler,
			int                  signal_number,
			GdmSignalHandlerFunc callback,
			gpointer             data)
{
	CallbackData *cdata;
	GSList       *list;

	g_return_if_fail (GDM_IS_SIGNAL_HANDLER (handler));

	cdata = g_new0 (CallbackData, 1);
	cdata->func = callback;
	cdata->data = data;

	list = g_hash_table_lookup (handler->priv->lookup, GINT_TO_POINTER (signal_number));
	if (list == NULL) {
		catch_signal (handler, signal_number);
	}

	list = g_slist_prepend (list, cdata);
	g_debug ("Inserting %d callback %p", signal_number, cdata->func);
	g_hash_table_insert (handler->priv->lookup, GINT_TO_POINTER (signal_number), list);
}

static void
gdm_signal_handler_class_init (GdmSignalHandlerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gdm_signal_handler_finalize;

	g_type_class_add_private (klass, sizeof (GdmSignalHandlerPrivate));
}

static void
signal_list_free (GSList *list)
{
	g_slist_foreach (list, (GFunc)g_free, NULL);
	g_slist_free (list);
}

void
gdm_signal_handler_set_main_loop (GdmSignalHandler *handler,
				  GMainLoop        *main_loop)
{
	g_return_if_fail (GDM_IS_SIGNAL_HANDLER (handler));

	/* FIXME: take a ref */
	handler->priv->main_loop = main_loop;
}

static void
gdm_signal_handler_init (GdmSignalHandler *handler)
{
        GIOChannel *ioc;

	handler->priv = GDM_SIGNAL_HANDLER_GET_PRIVATE (handler);

	handler->priv->lookup = g_hash_table_new (NULL, NULL);

        if (pipe (signal_pipes) == -1) {
                g_error ("Could not create pipe() for signal handling");
        }

        ioc = g_io_channel_unix_new (signal_pipes[0]);
	g_io_channel_set_flags (ioc, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch (ioc, G_IO_IN, (GIOFunc)signal_io_watch, handler);
	g_io_channel_set_close_on_unref (ioc, TRUE);
	g_io_channel_unref (ioc);
}

static void
gdm_signal_handler_finalize (GObject *object)
{
	GdmSignalHandler *handler;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_SIGNAL_HANDLER (object));

	handler = GDM_SIGNAL_HANDLER (object);

	g_return_if_fail (handler->priv != NULL);

	/* FIXME: free hash lists */
	g_hash_table_destroy (handler->priv->lookup);

	G_OBJECT_CLASS (gdm_signal_handler_parent_class)->finalize (object);
}

GdmSignalHandler *
gdm_signal_handler_new (void)
{
	if (signal_handler_object != NULL) {
		g_object_ref (signal_handler_object);
	} else {
		signal_handler_object = g_object_new (GDM_TYPE_SIGNAL_HANDLER, NULL);
		g_object_add_weak_pointer (signal_handler_object,
					   (gpointer *) &signal_handler_object);
	}

	return GDM_SIGNAL_HANDLER (signal_handler_object);
}
