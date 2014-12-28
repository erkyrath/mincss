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
    nod_AtRule = 2,
    nod_Block = 3,
} nodetype;

typedef struct node_struct {
    nodetype typ;

    /* All of these fields are optional. */
    int32_t *text;
    int textlen;
    
    token **tokens;
    int numtokens;
    int tokens_size;

    struct node_struct **nodes;
    int numnodes;
    int nodes_size;
} node;

static void read_token(mincss_context *context, int skipwhite);
static void free_token(token *tok);
static token *copy_token(token *tok);
static void dump_token(token *tok);

static node *new_node(nodetype typ);
static void free_node(node *nod);
static void node_copy_text(node *nod, token *tok);
static void node_add_token(node *nod, token *tok);
static void node_add_node(node *nod, node *nod2);

static node *read_stylesheet(mincss_context *context);
static node *read_statement(mincss_context *context);
static node *read_block(mincss_context *context);
static int read_any_until_semiblock(mincss_context *context, node *nod);

void mincss_read(mincss_context *context)
{
    read_token(context, 1);

    node *nod = read_stylesheet(context);

    free_node(nod);
}

/* Read the next token, storing it in context->nexttok. Stores NULL on EOF
   (rather than an EOF token). 
   Optionally skip over whitespace and comments.
   ### I may have to change this to "skip comments but not whitespace."
 */
static void read_token(mincss_context *context, int skipwhite)
{
    tokentype typ;

    if (context->nexttok) {
	free_token(context->nexttok);
	context->nexttok = NULL;
    }

    while (1) {
        typ = mincss_next_token(context);
        if (typ == tok_EOF)
            return;
        if (typ != tok_Comment && typ != tok_Space)
            break;
        if (!skipwhite)
            break;
    }

    token *tok = (token *)malloc(sizeof(token));
    if (!tok)
        return;

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

    context->nexttok = tok;
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

static token *copy_token(token *tok)
{
    token *newtok = (token *)malloc(sizeof(token));
    if (!newtok)
        return NULL;

    newtok->typ = tok->typ;
    if (tok->text) {
	newtok->len = tok->len;
        newtok->text = (int32_t *)malloc(sizeof(int32_t) * tok->len);
        memcpy(newtok->text, tok->text, sizeof(int32_t) * tok->len);
    }
    else {
	newtok->len = 0;
	newtok->text = NULL;
    }

    return newtok;
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
    nod->textlen = 0;
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

static void node_copy_text(node *nod, token *tok)
{
    if (tok->text) {
	nod->textlen = tok->len;
        nod->text = (int32_t *)malloc(sizeof(int32_t) * tok->len);
        memcpy(nod->text, tok->text, sizeof(int32_t) * tok->len);
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
    nod->tokens[nod->numtokens] = copy_token(tok);
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
    node *sheetnod = new_node(nod_Stylesheet);

    while (context->nexttok) {
	token *tok = context->nexttok;
	if (tok->typ == tok_CDO || tok->typ == tok_CDC) {
	    read_token(context, 1);
	    continue;
	}
	node *nod = read_statement(context);
	if (nod)
	    node_add_node(sheetnod, nod);
    }

    return sheetnod;
}

static node *read_statement(mincss_context *context)
{
    token *tok = context->nexttok;
    if (!tok)
	return NULL;
    if (tok->typ == tok_AtKeyword) {
	node *nod = new_node(nod_AtRule);
	node_copy_text(nod, tok);
	read_token(context, 1);
	int res = read_any_until_semiblock(context, nod);
	if (res == 1) {
	    /* semicolon */
	    return nod;
	}
	if (res == 2) {
	    /* beginning of block */
	    node *blocknod = read_block(context);
	    if (!blocknod) {
		/* error */
		free_node(nod);
		return NULL;
	    }
	    node_add_node(nod, blocknod);
	    return nod;
	}
	/* error */
	free_node(nod);
	return NULL;
    }
    else {
	/* ### ruleset */
	/* ### eat any */
	return NULL;
    }
}

static int read_any_until_semiblock(mincss_context *context, node *nod)
{
    while (1) {
	token *tok = context->nexttok;
	if (!tok)
	    return 0;
	if (tok->typ == tok_Semicolon) {
	    read_token(context, 1);
	    return 1;
	}
	if (tok->typ == tok_Ident 
	    || tok->typ == tok_Number
	    || tok->typ == tok_String) {
	    node_add_token(nod, tok);
	    read_token(context, 1);
	    continue;
	}
	if (tok->typ == tok_LBrace) {
	    return 2;
	}
	/* ### eat balanced until the next semi or block-end */
	return 0;
    }
}

static node *read_block(mincss_context *context)
{
    token *tok = context->nexttok;
    if (!tok || tok->typ != tok_LBrace) {
	return NULL;
    }
    read_token(context, 1);
    if (!tok)
	return NULL;

    node *nod = new_node(nod_Block);

    if (tok->typ == tok_RBrace) {
	return nod;
    }
    return NULL;
}
