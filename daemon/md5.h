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
