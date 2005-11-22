/* GDM - The GNOME Display Manager
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

#ifndef __GREETER_PARSER_H__
#define __GREETER_PARSER_H__

#include <libgnomecanvas/libgnomecanvas.h>
#include "greeter_item.h"


typedef enum 
{
  GREETER_PARSER_ERROR_NO_FILE,
  GREETER_PARSER_ERROR_BAD_XML,
  GREETER_PARSER_ERROR_WRONG_TYPE,
  GREETER_PARSER_ERROR_BAD_SPEC
} GreeterParseError;

#define GREETER_PARSER_ERROR greeter_parser_error_quark()
GQuark greeter_parser_error_quark (void);

GreeterItemInfo *greeter_parse (const char  *file,
				const char  *data_dir,
				GnomeCanvas *canvas,
				int          width,
				int          height,
				GError     **error);

GreeterItemInfo *greeter_lookup_id (const char *id);
const GList *greeter_custom_items (void);

#endif /* __GREETER_PARSER_H__ */
