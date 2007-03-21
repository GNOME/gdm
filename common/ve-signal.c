/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Signal routines
 *
 * (c) 2000, 2002 Queen of England
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "config.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "ve-signal.h"

typedef struct _SignalSource SignalSource;
struct _SignalSource {
	GSource     source;

	int         signal;
	guint8      index;
	guint8      shift;
};

static	guint32	signals_notified[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static gboolean
ve_signal_prepare (GSource  *source,
		   int      *timeout)
{
	SignalSource *ss = (SignalSource *)source;

	return signals_notified[ss->index] & (1 << ss->shift);
}

static gboolean
ve_signal_check (GSource *source)
{
	SignalSource *ss = (SignalSource *)source;

	return signals_notified[ss->index] & (1 << ss->shift);
}

static gboolean
ve_signal_dispatch (GSource     *source,
		    GSourceFunc  callback,
		    gpointer     user_data)
{
	SignalSource *ss = (SignalSource *)source;

	signals_notified[ss->index] &= ~(1 << ss->shift);

	return ((VeSignalFunc)callback) (ss->signal, user_data);
}

static GSourceFuncs signal_funcs = {
	ve_signal_prepare,
	ve_signal_check,
	ve_signal_dispatch
};

guint
ve_signal_add (int	      signal,
	       VeSignalFunc function,
	       gpointer      data)
{
	return ve_signal_add_full (G_PRIORITY_DEFAULT, signal, function, data, NULL);
}

guint
ve_signal_add_full (int            priority,
		    int            signal,
		    VeSignalFunc   function,
		    gpointer       data,
		    GDestroyNotify destroy)
{
	GSource *source;
	SignalSource *ss;
	guint s = 128 + signal;

	g_return_val_if_fail (function != NULL, 0);

	source = g_source_new (&signal_funcs, sizeof (SignalSource));
	ss = (SignalSource *)source;

	ss->signal = signal;
	ss->index = s / 32;
	ss->shift = s % 32;

	g_assert (ss->index < 8);

	g_source_set_priority (source, priority);
	g_source_set_callback (source, (GSourceFunc)function, data, destroy);
	g_source_set_can_recurse (source, TRUE);

	return g_source_attach (source, NULL);
}

void
ve_signal_notify (int signal)
{
	guint index, shift;
	guint s = 128 + signal;

	index = s / 32;
	shift = s % 32;

	g_assert (index < 8);

	signals_notified[index] |= 1 << shift;

	g_main_context_wakeup (NULL);
}

gboolean
ve_signal_was_notified (int signal)
{
	guint index, shift;
	guint s = 128 + signal;

	index = s / 32;
	shift = s % 32;

	g_assert (index < 8);

	return ((signals_notified[index]) & (1 << shift)) ? TRUE : FALSE;
}

void
ve_signal_unnotify (int signal)
{
	guint index, shift;
	guint s = 128 + signal;

	index = s / 32;
	shift = s % 32;

	g_assert (index < 8);

	signals_notified[index] &= ~(1 << shift);
}
