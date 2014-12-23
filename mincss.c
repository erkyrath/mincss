#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

/* ### CSS 2.2 (draft) has several syntax changes, not yet implemented here.
   - The letters "URL" can be written as hex escapes
   - The nonascii range starts at 0x80 rather than 0xA0
   - Numbers can start with + or -, and can end with an exponent
   - Probably other changes
 */

static void perform_parse(mincss_context *context);

static void putchar_utf8(int32_t val, FILE *fl);

mincss_context *mincss_init()
{
    mincss_context *context = (mincss_context *)malloc(sizeof(mincss_context));
    memset(context, 0, sizeof(mincss_context));

    return context;
}

void mincss_final(mincss_context *context)
{
    free(context);
}

void mincss_set_lexer_debug(mincss_context *context, int flag)
{
    context->lexer_debug = flag;
}

void mincss_parse_unicode(mincss_context *context, 
    mincss_unicode_reader reader,
    mincss_error_handler error,
    void *rock)
{
    context->parserock = rock;
    context->parse_unicode = reader;
    context->parse_byte = NULL;
    context->parse_error = error;

    perform_parse(context);

    context->parserock = NULL;
    context->parse_unicode = NULL;
    context->parse_byte = NULL;
    context->parse_error = NULL;
}

void mincss_parse_bytes_utf8(mincss_context *context, 
    mincss_unicode_reader reader,
    mincss_error_handler error,
    void *rock)
{
    context->parserock = rock;
    context->parse_unicode = NULL;
    context->parse_byte = reader;
    context->parse_error = error;

    perform_parse(context);

    context->parserock = NULL;
    context->parse_unicode = NULL;
    context->parse_byte = NULL;
    context->parse_error = NULL;
}

/* Do the parsing work. This is invoked by mincss_parse_unicode() and
   mincss_parse_bytes_utf8(). 
*/
static void perform_parse(mincss_context *context)
{
    context->errorcount = 0;

    context->tokenlen = 0;
    context->tokenmark = 0;
    context->tokenbufsize = 16;
    context->token = (int32_t *)malloc(context->tokenbufsize * sizeof(int32_t));

    if (!context->token) {
        mincss_note_error(context, "(Internal) Unable to allocate buffer memory");
        return;
    }

    if (context->lexer_debug) {
        /* Just read tokens and print them until the stream is done. */
        while (1) {
            int ix;
            tokentype toktype = mincss_next_token(context);
            if (toktype == tok_EOF)
                break;
            printf("<%s> \"", mincss_token_name(toktype));
            for (ix=0; ix<context->tokenlen; ix++) {
                int32_t ch = context->token[ix];
                if (ch < 32)
                    printf("^%c", ch+64);
                else
                    putchar_utf8(ch, stdout);
            }
            printf("\"\n");
        }
    }
    else {
        mincss_read(context);
    }

    free(context->token);
    context->token = NULL;
    context->tokenbufsize = 0;
    context->tokenlen = 0;
    context->tokenmark = 0;
}

/* Send a Unicode character to a UTF8-encoded stream. */
static void putchar_utf8(int32_t val, FILE *fl)
{
    if (val < 0) {
        putc('?', fl);
    }
    else if (val < 0x80) {
        putc(val, fl);
    }
    else if (val < 0x800) {
        putc((0xC0 | ((val & 0x7C0) >> 6)), fl);
        putc((0x80 |  (val & 0x03F)     ),  fl);
    }
    else if (val < 0x10000) {
        putc((0xE0 | ((val & 0xF000) >> 12)), fl);
        putc((0x80 | ((val & 0x0FC0) >>  6)), fl);
        putc((0x80 |  (val & 0x003F)      ),  fl);
    }
    else if (val < 0x200000) {
        putc((0xF0 | ((val & 0x1C0000) >> 18)), fl);
        putc((0x80 | ((val & 0x03F000) >> 12)), fl);
        putc((0x80 | ((val & 0x000FC0) >>  6)), fl);
        putc((0x80 |  (val & 0x00003F)      ),  fl);
    }
    else {
        putc('?', fl);
    }
}

void mincss_note_error(mincss_context *context, char *msg)
{
    context->errorcount += 1;

    if (context->parse_error)
        context->parse_error(msg, context->parserock);
    else
        fprintf(stderr, "MinCSS error: %s\n", msg);
}

