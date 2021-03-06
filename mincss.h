#include <stdint.h>

typedef int (*mincss_byte_reader)(void *rock);
typedef int32_t (*mincss_unicode_reader)(void *rock);
typedef void (*mincss_error_handler)(char *error, int linenum, void *rock);

typedef struct mincss_context_struct mincss_context;

/* Create a context for MinCSS parsing.
 */
extern mincss_context *mincss_init(void);

/* Clean up a context for MinCSS parsing. 
 */
extern void mincss_final(mincss_context *context);

/* Parse a CSS stream. 

   This uses a reader function, which is expected to return a stream
   of bytes (UTF-8 encoded) or -1 if there are no more.

   The error function is optional; if provided, it is used to report
   syntax errors in the CSS. If NULL, error messages are printed on
   stderr.
*/
extern void mincss_parse_bytes_utf8(mincss_context *context, 
    mincss_byte_reader reader,
    mincss_error_handler error,
    void *rock);

/* Parse a CSS stream. Same as above, except the reader function is expected
   to return a stream of Unicode character values (or -1 for end of stream).
*/
extern void mincss_parse_unicode(mincss_context *context, 
    mincss_unicode_reader reader,
    mincss_error_handler error,
    void *rock);

/* A nonzero level tells the parsing process to just print debug
   output instead of constructing a full stylesheet.
*/
#define MINCSS_TRACE_OFF (0)   /* normal operation */
#define MINCSS_TRACE_LEXER (1) /* print lex tokens, stop */
#define MINCSS_TRACE_TREE (2)  /* print the stage-one tree, stop */
extern void mincss_set_debug_trace(mincss_context *context, int level);

