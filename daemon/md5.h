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

#ifndef GdmMD5_H
#define GdmMD5_H

struct GdmMD5Context {
	guint32 buf[4];
	guint32 bits[2];
	unsigned char in[64];
};

void gdm_md5_init (struct GdmMD5Context *context);
void gdm_md5_update (struct GdmMD5Context *context, unsigned char const *buf,
		     unsigned len);
void gdm_md5_final (unsigned char digest[16], struct GdmMD5Context *context);
void gdm_md5_transform (guint32 buf[4], guint32 const in[16]);

/*
 * This is needed to make RSAREF happy on some MS-DOS compilers.
 */
/* typedef struct gdm_md5_Context gdm_md5__CTX; */

#endif /* !GdmMD5_H */
