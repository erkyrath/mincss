#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

typedef struct token_struct {
    tokentype typ;
    int32_t *text;
    int len;
} token;

static token *read_token(mincss_context *context, int skipwhite);
static void free_token(token *tok);
static void dump_token(token *tok);

void mincss_read(mincss_context *context)
{
    while (1) {
        token *tok = read_token(context, 1);
        if (!tok)
            break;
        dump_token(tok);
        free_token(tok);
    }
}

/* Read the next token. Returns NULL on EOF (rather than an EOF token). 
   Optionally skip over whitespace and comments.
   ### I may have to change this to "skip comments but not whitespace."
 */
static token *read_token(mincss_context *context, int skipwhite)
{
    tokentype typ;

    while (1) {
        typ = mincss_next_token(context);
        if (typ == tok_EOF)
            return NULL;
        if (typ != tok_Comment && typ != tok_Space)
            break;
        if (!skipwhite)
            break;
    }

    token *tok = (token *)malloc(sizeof(token));
    if (!tok)
        return NULL;

    /* We're going to copy out the content part of the token string. Skip
       string delimiters, the @ in AtKeyword, etc. If the content length
       is zero, we'll skip allocating entirely. */
    /* ### We could assert a bunch here, for safety. */

    int pos = 0;
    int len = context->tokenlen;
    switch (typ) {

    case tok_Ident:
    case tok_Number:
    case tok_Delim:
        /* Copy the entire text. */
        break;

    case tok_Dimension:
        /* ### Should we allocate the unit separately? Or store the
           division? Really, we should have marked that division
           during lexing. */
        break;

    case tok_Comment:
        pos = 2;
        len -= 4;
        break;

    case tok_String:
        pos = 1;
        len -= 2;
        break;

    case tok_AtKeyword:
    case tok_Hash:
        pos = 1;
        len -= 1;
        break;

    case tok_Percentage:
    case tok_Function:
        len -= 1;
        break;

    case tok_URI:
        pos = 4;
        len -= 5;
        /* ### and the string delimiters, if present */
        break;

    case tok_Space:
        /* Nobody cares. */
        len = 0;
        break;

    default:
        /* Everything else is a fixed string, so we don't need to store
           the text. */
        len = 0;
        break;
    }

    tok->typ = typ;
    if (len > 0) {
        tok->len = len;
        tok->text = (int32_t *)malloc(sizeof(int32_t) * len);
        memcpy(tok->text, context->token+pos, sizeof(int32_t) * len);
    }
    else {
        tok->len = 0;
        tok->text = NULL;
    }

    return tok;
}

static void free_token(token *tok)
{
    if (tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
    tok->len = 0;
    free(tok);
}

static void dump_token(token *tok) 
{
    printf("%s", mincss_token_name(tok->typ));
    if (tok->text) {
        int ix;
        printf(":\"");
        for (ix=0; ix<tok->len; ix++) {
            int32_t ch = tok->text[ix];
            if (ch < 32)
                printf("^%c", ch+64);
            else
                mincss_putchar_utf8(ch, stdout);
        }
        printf("\"");
    }
    printf("\n");
}
