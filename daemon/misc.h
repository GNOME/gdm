/* GDM - The Gnome Display Manager
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

#ifndef GDM_MISC_H
#define GDM_MISC_H

#include "config.h"
#include "gdm.h"

void gdm_fail   (const gchar *format, ...);
void gdm_info   (const gchar *format, ...);
void gdm_error  (const gchar *format, ...);
void gdm_debug  (const gchar *format, ...);

#ifdef HAVE_SETENV
#define gdm_setenv(var,value) setenv(var,value,-1)
#else
gint gdm_setenv (const gchar *var, const gchar *value);
#endif

#ifdef HAVE_UNSETENV
#define gdm_unsetenv(var) unsetenv(var)
#else
gint gdm_unsetenv (const gchar *var);
#endif

void gdm_clearenv (void);

/* clear environment, but keep the i18n ones (LANG, LC_ALL, etc...),
 * note that this leak memory so only use before exec */
void gdm_clearenv_no_lang (void);

#endif /* GDM_MISC_H */

/* EOF */
