#include <stdint.h>

typedef int (*mincss_byte_reader)(void *rock);
typedef int32_t (*mincss_unicode_reader)(void *rock);
typedef void (*mincss_error_handler)(char *error, void *rock);

typedef struct mincss_context_struct mincss_context;

extern mincss_context *mincss_init(void);
extern void mincss_final(mincss_context *context);

extern void mincss_parse_bytes_utf8(mincss_context *context, 
    mincss_byte_reader reader,
    mincss_error_handler error,
    void *rock);
extern void mincss_parse_unicode(mincss_context *context, 
    mincss_unicode_reader reader,
    mincss_error_handler error,
    void *rock);
