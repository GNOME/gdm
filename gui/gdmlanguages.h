/* GDM - The Gnome Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2001 George Lebl
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

#ifndef GDM_LANGUAGES_H
#define GDM_LANGUAGES_H

/* This is the interface for translating languages.  Language translations
 * are now hardocded in, but that may change */

const char *	gdm_lang_group1		(void);
const char *	gdm_lang_group2		(void);

/* locale is the locale we want the language name in or NULL
 * if any language */
char *		gdm_lang_name		(const char *locale,
					 const char *language,
					 gboolean never_encoding,
					 gboolean no_group);
GdkFont *	gdm_lang_font		(const char *locale);

GList *		gdm_lang_read_locale_file (const char *file);

#endif /* GDM_LANGUAGES_H */

/* EOF */
