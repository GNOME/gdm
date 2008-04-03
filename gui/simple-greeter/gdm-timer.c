/*
 * Copyright (C) 2008 Red Hat, Inc.
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
 * Written by: Ray Strode <rstrode@redhat.com>
 */

#include "config.h"
#include "gdm-timer.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gdm-marshal.h"

#define GDM_TIMER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_TIMER, GdmTimerPrivate))

#ifndef GDM_TIMER_TICKS_PER_SECOND
#define GDM_TIMER_TICKS_PER_SECOND 60
#endif

struct GdmTimerPrivate
{
        double start_time;
        double duration;

        guint  tick_timeout_id;

        guint  is_started : 1;
};

enum {
        PROP_0,
        PROP_START_TIME,
        PROP_DURATION,
        PROP_IS_STARTED
};

enum {
        TICK,
        STOP,
        NUMBER_OF_SIGNALS
};

static guint signals[NUMBER_OF_SIGNALS];

static void  gdm_timer_class_init  (GdmTimerClass *klass);
static void  gdm_timer_init        (GdmTimer      *timer);
static void  gdm_timer_finalize    (GObject       *object);

static void  gdm_timer_queue_next_tick (GdmTimer *timer,
                                        double    when);

G_DEFINE_TYPE (GdmTimer, gdm_timer, G_TYPE_OBJECT)

static void
gdm_timer_finalize (GObject *object)
{
        GdmTimer *timer;

        timer = GDM_TIMER (object);

        gdm_timer_stop (timer);

        G_OBJECT_CLASS (gdm_timer_parent_class)->finalize (object);
}

static void
gdm_timer_get_property (GObject        *object,
                        guint           prop_id,
                        GValue         *value,
                        GParamSpec     *pspec)
{
        GdmTimer *timer;

        timer = GDM_TIMER (object);

        switch (prop_id) {
        case PROP_START_TIME:
                g_value_set_double (value, timer->priv->start_time);
                break;
        case PROP_DURATION:
                g_value_set_double (value, timer->priv->duration);
                break;
        case PROP_IS_STARTED:
                g_value_set_boolean (value, timer->priv->is_started);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_timer_class_init (GdmTimerClass *klass)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gdm_timer_finalize;
        object_class->get_property = gdm_timer_get_property;

        signals[TICK] = g_signal_new ("tick",
                                      G_TYPE_FROM_CLASS (object_class),
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (GdmTimerClass, tick),
                                      NULL,
                                      NULL,
                                      gdm_marshal_VOID__DOUBLE,
                                      G_TYPE_NONE,
                                      1, G_TYPE_DOUBLE);
        signals[STOP] = g_signal_new ("stop",
                                      G_TYPE_FROM_CLASS (object_class),
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (GdmTimerClass, stop),
                                      NULL,
                                      NULL,
                                      g_cclosure_marshal_VOID__VOID,
                                      G_TYPE_NONE, 0);

        g_object_class_install_property (object_class,
                                         PROP_DURATION,
                                         g_param_spec_double ("duration",
                                                              _("Duration"),
                                                              _("Number of seconds until timer stops"),
                                                              0.0, G_MAXDOUBLE, 0.0,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (object_class,
                                         PROP_START_TIME,
                                         g_param_spec_double ("start-time",
                                                              _("Start time"),
                                                              _("Time the timer was started"),
                                                              0.0, G_MAXDOUBLE, 0.0,
                                                              G_PARAM_READABLE));

        g_object_class_install_property (object_class,
                                         PROP_IS_STARTED,
                                         g_param_spec_boolean ("is-started",
                                                               _("Is it Running?"),
                                                               _("Whether or not the timer "
                                                                 "is currently ticking"),
                                                               FALSE, G_PARAM_READABLE));



        g_type_class_add_private (klass, sizeof (GdmTimerPrivate));
}

static void
gdm_timer_init (GdmTimer *timer)
{
        timer->priv = GDM_TIMER_GET_PRIVATE (timer);
}

GdmTimer *
gdm_timer_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_TIMER, NULL);

        return GDM_TIMER (object);
}

static double
get_current_time (void)
{
  const double microseconds_per_second = 1000000.0;
  double       timestamp;
  GTimeVal     now;

  g_get_current_time (&now);

  timestamp = ((microseconds_per_second * now.tv_sec) + now.tv_usec) /
               microseconds_per_second;

  return timestamp;
}

static gboolean
on_tick_timeout (GdmTimer *timer)
{
        double progress;
        double time_before_tick;
        double elapsed_time;

        time_before_tick = get_current_time ();
        elapsed_time = time_before_tick - timer->priv->start_time;
        progress = elapsed_time / timer->priv->duration;

        if (progress > 0.999) {
                gdm_timer_stop (timer);
        } else {
                static const double frequency = 1.0 / GDM_TIMER_TICKS_PER_SECOND;
                double next_tick;
                double time_after_tick;
                double tick_duration;

                g_signal_emit (G_OBJECT (timer), signals[TICK], 0, progress);

                time_after_tick = get_current_time ();
                tick_duration = time_after_tick - time_before_tick;

                next_tick = MAX (frequency - tick_duration, 0.0);
                timer->priv->tick_timeout_id = 0;
                gdm_timer_queue_next_tick (timer, next_tick);
        }

        return FALSE;
}

static void
gdm_timer_clear_timeout (GdmTimer *timer)
{
        timer->priv->tick_timeout_id = 0;
}

static void
gdm_timer_queue_next_tick (GdmTimer *timer,
                           double    when)
{
        if (timer->priv->tick_timeout_id != 0) {
                return;
        }

        if (!timer->priv->is_started) {
                return;
        }

        timer->priv->tick_timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                                           (guint) (when * 1000),
                                                           (GSourceFunc)
                                                           on_tick_timeout,
                                                           timer,
                                                           (GDestroyNotify)
                                                           gdm_timer_clear_timeout);
}

static void
gdm_timer_set_is_started (GdmTimer *timer,
                          gboolean  is_started)
{
        timer->priv->is_started = is_started;
        g_object_notify (G_OBJECT (timer), "is-started");
}

void
gdm_timer_start (GdmTimer *timer,
                 double    number_of_seconds)
{
        g_return_if_fail (GDM_IS_TIMER (timer));
        g_return_if_fail (number_of_seconds > G_MINDOUBLE);
        g_return_if_fail (!timer->priv->is_started);

        timer->priv->start_time = get_current_time ();
        timer->priv->duration = number_of_seconds;

        g_assert (timer->priv->tick_timeout_id == 0);
        gdm_timer_set_is_started (timer, TRUE);
        gdm_timer_queue_next_tick (timer, 1.0 / GDM_TIMER_TICKS_PER_SECOND);
}

void
gdm_timer_stop (GdmTimer *timer)
{
        g_return_if_fail (GDM_IS_TIMER (timer));

        if (!timer->priv->is_started) {
                return;
        }

        if (timer->priv->tick_timeout_id != 0) {
                g_source_remove (timer->priv->tick_timeout_id);
                timer->priv->tick_timeout_id = 0;
        }

        gdm_timer_set_is_started (timer, FALSE);
        g_signal_emit (G_OBJECT (timer), signals[STOP], 0);
}

gboolean
gdm_timer_is_started (GdmTimer *timer)
{
        g_return_val_if_fail (GDM_IS_TIMER (timer), FALSE);

        return timer->priv->is_started;
}
