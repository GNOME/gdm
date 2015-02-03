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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */

#include <act/act-user-manager.h>

#include "gdm-common.h"

#include "gdm-settings-keys.h"

#include "gdm-simple-slave.h"

#include "gdm-server.h"
#include "gdm-session.h"
#include "gdm-session-glue.h"
#include "gdm-launch-environment.h"
#include "gdm-settings-direct.h"
#include "gdm-settings-keys.h"

static void     gdm_simple_slave_class_init     (GdmSimpleSlaveClass *klass);
static void     gdm_simple_slave_init           (GdmSimpleSlave      *simple_slave);

G_DEFINE_TYPE (GdmSimpleSlave, gdm_simple_slave, GDM_TYPE_SLAVE)

static void
gdm_simple_slave_class_init (GdmSimpleSlaveClass *klass)
{
}

static void
gdm_simple_slave_init (GdmSimpleSlave *slave)
{
}
