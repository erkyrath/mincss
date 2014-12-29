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
    nod_Token = 1,
    nod_Stylesheet = 2,
    nod_AtRule = 3,
    nod_Block = 4,
    nod_Parens = 5,
    nod_Brackets = 6,
    nod_Function = 7,
} nodetype;

typedef struct node_struct {
    nodetype typ;

    /* All of these fields are optional. */
    int32_t *text;
    int textlen;

    tokentype toktype;
    
    struct node_struct **nodes;
    int numnodes;
    int nodes_size;
} node;

static void warning(mincss_context *context, char *msg);

static void read_token(mincss_context *context, int skipwhite);
static void free_token(token *tok);

static node *new_node(nodetype typ);
static node *new_node_token(token *tok);
static void free_node(node *nod);
static void dump_node(node *nod, int depth);
static void node_copy_text(node *nod, token *tok);
static void node_add_node(node *nod, node *nod2);

static node *read_stylesheet(mincss_context *context);
static node *read_statement(mincss_context *context);
static node *read_block(mincss_context *context);
static int read_any_until_semiblock(mincss_context *context, node *nod);

void mincss_read(mincss_context *context)
{
    read_token(context, 1);

    node *nod = read_stylesheet(context);
    dump_node(nod, 0);
    free_node(nod);
}

void warning(mincss_context *context, char *msg)
{
    printf("### WARNING (line %d): %s\n", context->linenum, msg);
}

/* Read the next token, storing it in context->nexttok. Stores NULL on EOF
   (rather than an EOF token). 
   Optionally skip over whitespace and comments.
   ### I may have to change this to "skip comments but not whitespace."
   ### If we wind up never having more than one allocated, I may blow off
       the struct and just keep data in the context.
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

static node *new_node(nodetype typ)
{
    node *nod = (node *)malloc(sizeof(node));
    if (!nod)
	return NULL; /*### malloc error*/
    nod->typ = typ;
    nod->text = NULL;
    nod->textlen = 0;
    nod->nodes = NULL;
    nod->numnodes = 0;
    nod->nodes_size = 0;
    return nod;
}

static node *new_node_token(token *tok)
{
    node *nod = new_node(nod_Token);
    nod->toktype = tok->typ;
    node_copy_text(nod, tok);
    return nod;
}

static void free_node(node *nod)
{
    int ix;

    if (nod->text) {
	free(nod->text);
	nod->text = NULL;
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

static void dump_indent(int val)
{
    int ix;
    for (ix=0; ix<val; ix++)
	putchar(' ');
}

static void dump_node(node *nod, int depth)
{
    dump_indent(depth);
    switch (nod->typ) {
    case nod_None:
	printf("None");
	break;
    case nod_Token:
	printf("Token");
	printf(" (%s)", mincss_token_name(nod->toktype));
	break;
    case nod_Stylesheet:
	printf("Stylesheet");
	break;
    case nod_AtRule:
	printf("AtRule");
	break;
    case nod_Block:
	printf("Block");
	break;
    case nod_Parens:
	printf("Parens");
	break;
    case nod_Brackets:
	printf("Brackets");
	break;
    case nod_Function:
	printf("Function");
	break;
    default:
	printf("??? node-type %d", (int)nod->typ);
	break;
    }

    if (nod->text) {
	printf(" \"");
	int ix;
        for (ix=0; ix<nod->textlen; ix++) {
            int32_t ch = nod->text[ix];
            if (ch < 32)
                printf("^%c", ch+64);
            else
                mincss_putchar_utf8(ch, stdout);
        }
	printf("\"");
    }
    printf("\n");

    if (nod->nodes) {
	int ix;
	for (ix=0; ix<nod->numnodes; ix++) {
	    dump_node(nod->nodes[ix], depth+1);
	}
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

/* The "any" production in the CSS grammar is any token except
   Semicolon, AtKeyword, LBrace, RBrace, RParen, RBracket, CDO, CDC.
   An LParen or LBracket causes a balanced read, as does Function.
   Bad tokens are discarded with a warning (including a balanced
   block), unless it's an expected terminator.
*/

static int read_any_until_semiblock(mincss_context *context, node *nod)
{
    while (1) {
	token *tok = context->nexttok;
	if (!tok) {
	    /* ### unclosed at-rule; warning, treat as terminated */
	    return 1;
	}
	if (tok->typ == tok_Semicolon) {
	    read_token(context, 1);
	    return 1;
	}
	if (tok->typ == tok_LBrace) {
	    return 2;
	}

	/* ### open-paren/bracket, eat balanced */
	/* ### illegal tokens, eat and discard */

	node *toknod = new_node_token(tok);
	node_add_node(nod, toknod);
	read_token(context, 1);
    }
}

static void read_any_until_close(mincss_context *context, node *nod, tokentype closetok)
{
    while (1) {
	token *tok = context->nexttok;
	if (!tok) {
	    /* ### unclosed paren/bracket; warning, treat as terminated */
	    return;
	}
	if (tok->typ == tok_Semicolon) {
	    warning(context, "Unexpected semicolon");
	    read_token(context, 1);
	    continue;
	}
	if (tok->typ == tok_LBrace) {
	    warning(context, "Unexpected block");
	    read_block(context);
	    continue;
	}

	if (tok->typ == closetok) {
	    /* Expected close-token. */
	    read_token(context, 1);
	    return;
	}

	/* ### open-paren/bracket, eat balanced */
	/* ### illegal tokens, eat and discard */

	node *toknod = new_node_token(tok);
	node_add_node(nod, toknod);
	read_token(context, 1);
    }
}

static node *read_block(mincss_context *context)
{
    token *tok = context->nexttok;
    if (!tok || tok->typ != tok_LBrace) {
	return NULL; /* ### internal error */
    }
    read_token(context, 1);

    node *nod = new_node(nod_Block);

    while (1) {
	tok = context->nexttok;
	if (!tok) {
	    warning(context, "Unexpected end of block");
	    return nod;
	}

	switch (tok->typ) {

	case tok_RBrace:
	    /* Done */
	    read_token(context, 1);
	    return nod;

	case tok_LBrace: {
	    /* Sub-block */
	    node *blocknod = read_block(context);
	    if (!blocknod) {
		/* error, already reported */
		continue;
	    }
	    node_add_node(nod, blocknod);
	    continue;
	}

	case tok_Semicolon: {
	    node *subnod = new_node_token(tok);
	    node_add_node(nod, subnod);
            read_token(context, 1);
	    continue;
	}

	case tok_AtKeyword: {
	    node *atnod = new_node_token(tok);
	    node_add_node(nod, atnod);
            read_token(context, 1);
	    continue;
	}

	case tok_Function: {
	    node *subnod = new_node(nod_Function);
	    node_copy_text(subnod, tok);
	    node_add_node(nod, subnod);
            read_token(context, 1);
	    read_any_until_close(context, subnod, tok_RParen);
	    continue;
	}

	case tok_LParen: {
	    node *subnod = new_node(nod_Parens);
	    node_add_node(nod, subnod);
            read_token(context, 1);
	    read_any_until_close(context, subnod, tok_RParen);
	    continue;
	}

	case tok_LBracket: {
	    node *subnod = new_node(nod_Brackets);
	    node_add_node(nod, subnod);
            read_token(context, 1);
	    read_any_until_close(context, subnod, tok_RBracket);
	    continue;
	}

	case tok_CDO:
	case tok_CDC:
	    warning(context, "HTML comment delimiters not allowed inside block");
            read_token(context, 1);
	    continue;

	case tok_RParen:
	    warning(context, "Unexpected close-paren inside block");
            read_token(context, 1);
	    continue;

	case tok_RBracket:
	    warning(context, "Unexpected close-bracket inside block");
            read_token(context, 1);
	    continue;

	default: {
	    /* Anything else is a single "any". */
	    node *subnod = new_node_token(tok);
	    node_add_node(nod, subnod);
	    read_token(context, 1);
	}
	}
    }
}
