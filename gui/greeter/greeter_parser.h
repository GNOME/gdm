#ifndef __GREETER_PARSER_H__
#define __GREETER_PARSER_H__

#include <libgnomecanvas/libgnomecanvas.h>
#include "greeter_item.h"


typedef enum 
{
  GREETER_PARSER_ERROR_NO_FILE,
  GREETER_PARSER_ERROR_BAD_XML,
  GREETER_PARSER_ERROR_WRONG_TYPE,
  GREETER_PARSER_ERROR_BAD_SPEC,
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

#endif /* __GREETER_PARSER_H__ */
