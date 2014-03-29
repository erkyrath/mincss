#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"

struct mincss_context_struct {
    int errorcount;

    void *parserock;
    mincss_unicode_reader parse_unicode;
    mincss_byte_reader parse_byte;
    mincss_error_handler parse_error;

    int32_t *token;
    int tokenbufsize;
    int tokenmark;
    int tokenlen;
};

typedef enum tokentype_enum {
    tok_EOF = 0,
    tok_Number = 1,
    tok_LBrace = 2,
    tok_RBrace = 3,
    tok_Delim = 4,
} tokentype;

static void perform_parse(mincss_context *context);
static void note_error(mincss_context *context, char *msg);
static tokentype next_token(mincss_context *context);
static int32_t next_char(mincss_context *context);
static void putback_char(mincss_context *context, int count);

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

static void perform_parse(mincss_context *context)
{
    context->errorcount = 0;

    context->tokenlen = 0;
    context->tokenmark = 0;
    context->tokenbufsize = 16;
    context->token = (int32_t *)malloc(context->tokenbufsize * sizeof(int32_t));

    if (!context->token) {
        note_error(context, "(Internal) Unable to allocate buffer memory");
        return;
    }

    while (1) {
        int ix;
        tokentype toktype = next_token(context);
        if (toktype == tok_EOF)
            break;
        printf("### token %d: '", toktype);
        for (ix=0; ix<context->tokenlen; ix++)
            printf("%c", (char)context->token[ix]);
        printf("'\n");
    }

    free(context->token);
    context->token = NULL;
    context->tokenbufsize = 0;
    context->tokenlen = 0;
    context->tokenmark = 0;
}

static void note_error(mincss_context *context, char *msg)
{
    context->errorcount += 1;

    if (context->parse_error)
        context->parse_error(msg, context->parserock);
    else
        fprintf(stderr, "MinCSS error: %s", msg);
}

static tokentype next_token(mincss_context *context)
{
    if (context->tokenlen) {
        int extra = context->tokenmark - context->tokenlen;
        if (extra) {
            memmove(context->token, context->token+context->tokenlen, extra*sizeof(int32_t));
        }
        context->tokenlen = 0;
        context->tokenmark = extra;
    }

    int32_t ch = next_char(context);
    if (ch == -1) {
        return tok_EOF;
    }

    switch (ch) {
    case '{':
        return tok_LBrace;
    case '}':
        return tok_RBrace;
    }

    if (ch >= '0' && ch <= '9') {
        int dotpos = -1;
        while (1) {
            ch = next_char(context);
            if (ch == -1) {
                if (dotpos > 0 && dotpos == context->tokenlen-1) {
                    putback_char(context, 1);
                    return tok_Number;
                }
                return tok_Number;
            }
            if (ch == '.') {
                if (dotpos >= 0) {
                    if (dotpos == context->tokenlen-2) {
                        putback_char(context, 2);
                        return tok_Number;
                    }
                    putback_char(context, 1);
                    return tok_Number;
                }
                dotpos = context->tokenlen-1;
                continue;
            }
            if (!(ch >= '0' && ch <= '9')) {
                if (dotpos > 0 && dotpos == context->tokenlen-2) {
                    putback_char(context, 2);
                    return tok_Number;
                }
                putback_char(context, 1);
                return tok_Number;
            }
            /* digit */
            continue;
        }
    }

    return tok_Delim;
}

static int32_t next_char(mincss_context *context)
{
    int32_t ch;

    if (!context->token)
        return -1;

    if (context->tokenlen < context->tokenmark) {
        ch = context->token[context->tokenlen];
        context->tokenlen++;
        return ch;
    }

    if (context->tokenlen >= context->tokenbufsize) {
        context->tokenbufsize = 2*context->tokenlen + 16;
        context->token = (int32_t *)realloc(context->token, context->tokenbufsize * sizeof(int32_t));
        if (!context->token) {
            note_error(context, "(Internal) Unable to reallocate buffer memory");
            return -1;
        }
    }

    /* ### or bytes */
    ch = (context->parse_unicode)(context->parserock);
    if (ch == -1)
        return -1;

    context->token[context->tokenlen] = ch;
    context->tokenlen += 1;
    context->tokenmark = context->tokenlen;
    return ch;
}

static void putback_char(mincss_context *context, int count)
{
    if (count > context->tokenlen) {
        note_error(context, "(Internal) Put back too many characters");
        context->tokenlen = 0;
    }
    else {
        context->tokenlen -= count;
    }
}

