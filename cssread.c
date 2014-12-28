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

typedef enum nodetype_enum {
    nod_None = 0,
    nod_Stylesheet = 1,
} nodetype;

typedef struct node_struct {
    nodetype typ;

    /* All of these fields are optional. */
    int32_t *text;
    int len;
    
    token **tokens;
    int numtokens;
    int tokens_size;

    struct node_struct **nodes;
    int numnodes;
    int nodes_size;
} node;

static token *read_token(mincss_context *context, int skipwhite);
static void free_token(token *tok);
static void dump_token(token *tok);

static node *new_node(nodetype typ);
static void free_node(node *nod);
static void node_add_token(node *nod, token *tok);
static void node_add_node(node *nod, node *nod2);

static node *read_stylesheet(mincss_context *context);

void mincss_read(mincss_context *context)
{
    node *nod = read_stylesheet(context);

    free_node(nod);
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

static node *new_node(nodetype typ)
{
    node *nod = (node *)malloc(sizeof(node));
    if (!nod)
	return NULL; /*### malloc error*/
    nod->typ = typ;
    nod->text = NULL;
    nod->len = 0;
    nod->tokens = NULL;
    nod->numtokens = 0;
    nod->tokens_size = 0;
    nod->nodes = NULL;
    nod->numnodes = 0;
    nod->nodes_size = 0;
    return nod;
}

static void free_node(node *nod)
{
    int ix;

    if (nod->text) {
	free(nod->text);
	nod->text = NULL;
    }

    if (nod->tokens) {
	for (ix=0; ix<nod->numtokens; ix++) {
	    free_token(nod->tokens[ix]);
	    nod->tokens[ix] = NULL;
	}
	free(nod->tokens);
	nod->tokens = NULL;
    }

    if (nod->nodes) {
	for (ix=0; ix<nod->numnodes; ix++) {
	    free_node(nod->nodes[ix]);
	    nod->nodes[ix] = NULL;
	}
	free(nod->nodes);
	nod->nodes = NULL;
    }
}

static void node_add_token(node *nod, token *tok)
{
    if (!nod->tokens) {
	nod->tokens_size = 4;
	nod->tokens = (token **)malloc(nod->tokens_size * sizeof(token *));
    }
    else if (nod->numtokens >= nod->tokens_size) {
	nod->tokens_size *= 2;
	nod->tokens = (token **)realloc(nod->tokens, nod->tokens_size * sizeof(token *));
    }
    if (!nod->tokens)
	return; /*### malloc error*/
    nod->tokens[nod->numtokens] = tok;
    nod->numtokens += 1;
}

static void node_add_node(node *nod, node *nod2)
{
    if (!nod->nodes) {
	nod->nodes_size = 4;
	nod->nodes = (node **)malloc(nod->nodes_size * sizeof(node *));
    }
    else if (nod->numnodes >= nod->nodes_size) {
	nod->nodes_size *= 2;
	nod->nodes = (node **)realloc(nod->nodes, nod->nodes_size * sizeof(node *));
    }
    if (!nod->nodes)
	return; /*### malloc error*/
    nod->nodes[nod->numnodes] = nod2;
    nod->numnodes += 1;
}

static node *read_stylesheet(mincss_context *context)
{
}

