/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#ifndef __GDM_SLAVE_JOB_H
#define __GDM_SLAVE_JOB_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_SLAVE_JOB         (gdm_slave_job_get_type ())
#define GDM_SLAVE_JOB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_SLAVE_JOB, GdmSlaveJob))
#define GDM_SLAVE_JOB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_SLAVE_JOB, GdmSlaveJobClass))
#define GDM_IS_SLAVE_JOB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_SLAVE_JOB))
#define GDM_IS_SLAVE_JOB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_SLAVE_JOB))
#define GDM_SLAVE_JOB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_SLAVE_JOB, GdmSlaveJobClass))

typedef struct GdmSlaveJobPrivate GdmSlaveJobPrivate;

typedef struct
{
        GObject               parent;
        GdmSlaveJobPrivate *priv;
} GdmSlaveJob;

typedef struct
{
        GObjectClass   parent_class;
        void (* exited)            (GdmSlaveJob  *job,
                                    int           exit_code);

        void (* died)              (GdmSlaveJob  *job,
                                    int           signal_number);
} GdmSlaveJobClass;

GType               gdm_slave_job_get_type     (void);
GdmSlaveJob *     gdm_slave_job_new          (void);
void                gdm_slave_job_set_command  (GdmSlaveJob *slave,
                                                const char  *command);
void                gdm_slave_job_set_log_path (GdmSlaveJob *slave,
                                                const char  *path);
gboolean            gdm_slave_job_start        (GdmSlaveJob *slave);
gboolean            gdm_slave_job_stop         (GdmSlaveJob *slave);

G_END_DECLS

#endif /* __GDM_SLAVE_JOB_H */
