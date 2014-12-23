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

void mincss_read(mincss_context *context)
{
}

static token *read_token(mincss_context *context)
{
    tokentype typ = mincss_next_token(context);
    if (typ == tok_EOF)
        return NULL;

    token *tok = (token *)malloc(sizeof(token));
    if (!tok)
        return NULL;

    tok->typ = typ;
    if (!context->tokenlen) {
        tok->len = 0;
        tok->text = NULL;
    }
    else {
        tok->len = context->tokenlen;
        tok->text = (int32_t *)malloc(sizeof(int32_t) * context->tokenlen);
        memcpy(tok->text, context->token, sizeof(int32_t) * context->tokenlen);
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
