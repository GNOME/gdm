#ifndef __GREETER_PARSER_H__
#define __GREETER_PARSER_H__

#include <libgnomecanvas/libgnomecanvas.h>
#include "greeter_item.h"


gboolean greeter_parse (char        *file,
			GnomeCanvas *canvas,
			int          width,
			int          height);

GreeterItemInfo *greeter_lookup_id (const char *id);

#endif /* __GREETER_PARSER_H__ */
