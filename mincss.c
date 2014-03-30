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
    tok_LBracket = 4,
    tok_RBracket = 5,
    tok_LParen = 6,
    tok_RParen = 7,
    tok_Delim = 8,
    tok_Space = 9,
    tok_Colon = 10,
    tok_Semicolon = 11,
    tok_Comment = 12,
    tok_Percentage = 13,
} tokentype;

static void perform_parse(mincss_context *context);
static void note_error(mincss_context *context, char *msg);
static void putchar_utf8(int32_t val, FILE *fl);
static char *token_name(tokentype tok);
static tokentype next_token(mincss_context *context);
static int parse_number(mincss_context *context);
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
        printf("<%s> \"", token_name(toktype));
        for (ix=0; ix<context->tokenlen; ix++) {
            int32_t ch = context->token[ix];
            if (ch < 32)
                printf("^%c", ch+64);
            else
                putchar_utf8(ch, stdout);
        }
        printf("\"\n");
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
        fprintf(stderr, "MinCSS error: %s\n", msg);
}

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

static char *token_name(tokentype tok)
{
    switch (tok) {
    case tok_EOF: return "EOF";
    case tok_Number: return "Number";
    case tok_LBrace: return "LBrace";
    case tok_RBrace: return "RBrace";
    case tok_LBracket: return "LBracket";
    case tok_RBracket: return "RBracket";
    case tok_LParen: return "LParen";
    case tok_RParen: return "RParen";
    case tok_Delim: return "Delim";
    case tok_Space: return "Space";
    case tok_Colon: return "Colon";
    case tok_Semicolon: return "Semicolon";
    case tok_Comment: return "Comment";
    case tok_Percentage: return "Percentage";
    default: return "???";
    }
}

#define IS_WHITESPACE(ch) ((ch) == ' ' || (ch) == '\t' || (ch) == '\r' || (ch) == '\n' || (ch) == '\f')

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
    case '(':
        return tok_LParen;
    case ')':
        return tok_RParen;
    case '[':
        return tok_LBracket;
    case ']':
        return tok_RBracket;
    case '{':
        return tok_LBrace;
    case '}':
        return tok_RBrace;
    case ':':
        return tok_Colon;
    case ';':
        return tok_Semicolon;
    }

    if (IS_WHITESPACE(ch)) {
        while (1) {
            ch = next_char(context);
            if (ch == -1) 
                return tok_Space;
            if (!IS_WHITESPACE(ch)) {
                putback_char(context, 1);
                return tok_Space;
            }
            continue;
        }
    }

    if ((ch >= '0' && ch <= '9') || (ch == '.')) {
        putback_char(context, 1);
        int numlen = parse_number(context);
        if (numlen == 0) {
            ch = next_char(context);
            return tok_Delim;
        }
        ch = next_char(context);
        if (ch == -1)
            return tok_Number;
        if (ch == '%')
            return tok_Percentage;
        putback_char(context, 1);
        return tok_Number;
    }

    if (ch == '/') {
        ch = next_char(context);
        if (ch == -1) 
            return tok_Delim;
        if (ch != '*') {
            putback_char(context, 1);
            return tok_Delim;
        }
        int gotstar = 0;
        while (1) {
            ch = next_char(context);
            if (ch == -1) {
                note_error(context, "Unterminated comment");
                return tok_Comment;
            }
            if (ch == '/' && gotstar)
                return tok_Comment;
            gotstar = (ch == '*');
        }
    }

    return tok_Delim;
}

static int parse_number(mincss_context *context)
{
    int count = 0;
    int dotpos = -1;

    int32_t ch = next_char(context);
    if (ch == -1)
        return 0;
    count++;

    if (!((ch >= '0' && ch <= '9') || (ch == '.'))) {
        putback_char(context, count);
        return 0;
    }

    if (ch == '.')
        dotpos = 0;

    while (1) {
        ch = next_char(context);
        if (ch == -1) {
            if (dotpos == 0 && count == 1) {
                putback_char(context, count);
                return 0;
            }
            if (dotpos >= 0 && dotpos == count-1) {
                putback_char(context, 1);
                return count-1;
            }
            return count;
        }
        count++;

        if (ch == '.') {
            if (dotpos >= 0) {
                if (dotpos == 0 && count == 2) {
                    putback_char(context, count);
                    return 0;
                }
                if (dotpos == count-2) {
                    putback_char(context, 2);
                    return count-2;
                }
                putback_char(context, 1);
                return count-1;
            }
            dotpos = count-1;
            continue;
        }
        if (!(ch >= '0' && ch <= '9')) {
            if (dotpos == 0 && count == 2) {
                putback_char(context, count);
                return 0;
            }
            if (dotpos >= 0 && dotpos == count-2) {
                putback_char(context, 2);
                return count-2;
            }
            putback_char(context, 1);
            return count-1;
        }
        /* digit */
        continue;
    }
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

    if (context->parse_byte) {
        int32_t byte0 = (context->parse_byte)(context->parserock);
        if (byte0 < 0) {
            ch = -1;
        }
        else if (byte0 < 0x80) {
            ch = byte0;
        }
        else if ((byte0 & 0xE0) == 0xC0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                note_error(context, "(UTF8) Incomplete two-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                note_error(context, "(UTF8) Malformed two-byte character");
                ch = byte0;
            }
            else {
                ch = (byte0 & 0x1F) << 6;
                ch |= (byte1 & 0x3F);
            }
        }
        else if ((byte0 & 0xF0) == 0xE0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                note_error(context, "(UTF8) Incomplete three-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                note_error(context, "(UTF8) Malformed three-byte character");
                ch = byte0;
            }
            else {
                int32_t byte2 = (context->parse_byte)(context->parserock);
                if (byte2 < 0) {
                    note_error(context, "(UTF8) Incomplete three-byte character");
                    ch = byte0;
                }
                else if ((byte2 & 0xC0) != 0x80) {
                    note_error(context, "(UTF8) Malformed three-byte character");
                    ch = byte0;
                }
                else {
                    ch = (((byte0 & 0x0F)<<12)  & 0x0000F000);
                    ch |= (((byte1 & 0x3F)<<6) & 0x00000FC0);
                    ch |= (((byte2 & 0x3F))    & 0x0000003F);
                }
            }
        }
        else if ((byte0 & 0xF0) == 0xF0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                note_error(context, "(UTF8) Incomplete four-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                note_error(context, "(UTF8) Malformed four-byte character");
                ch = byte0;
            }
            else {
                int32_t byte2 = (context->parse_byte)(context->parserock);
                if (byte2 < 0) {
                    note_error(context, "(UTF8) Incomplete four-byte character");
                    ch = byte0;
                }
                else if ((byte2 & 0xC0) != 0x80) {
                    note_error(context, "(UTF8) Malformed four-byte character");
                    ch = byte0;
                }
                else {
                    int32_t byte3 = (context->parse_byte)(context->parserock);
                    if (byte3 < 0) {
                        note_error(context, "(UTF8) Incomplete four-byte character");
                        ch = byte0;
                    }
                    else if ((byte3 & 0xC0) != 0x80) {
                        note_error(context, "(UTF8) Malformed four-byte character");
                        ch = byte0;
                    }
                    else {
                        ch = (((byte0 & 0x07)<<18)   & 0x1C0000);
                        ch |= (((byte1 & 0x3F)<<12) & 0x03F000);
                        ch |= (((byte2 & 0x3F)<<6)  & 0x000FC0);
                        ch |= (((byte3 & 0x3F))     & 0x00003F);
                    }
                }
            }
        }
        else {
            note_error(context, "(UTF8) Malformed character");
            ch = '?';
        }
    }
    else {
        ch = (context->parse_unicode)(context->parserock);
    }
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

