#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

static void read_token(mincss_context *context);

static node *new_node(mincss_context *context, nodetype typ);
static node *new_node_token(mincss_context *context, token *tok);
static void free_node(node *nod);
static void node_copy_text(node *nod, token *tok);
static void node_add_node(node *nod, node *nod2);

static node *read_stylesheet(mincss_context *context);
static node *read_statement(mincss_context *context);
static node *read_block(mincss_context *context);
static void read_any_top_level(mincss_context *context, node *nod);
static void read_any_until_semiblock(mincss_context *context, node *nod);
static void read_any_until_close(mincss_context *context, node *nod, tokentype closetok);

void mincss_read(mincss_context *context)
{
    if (context->debug_trace == MINCSS_TRACE_LEXER) {
        /* Just read tokens and print them until the stream is done. 
           Then stop. */
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
                    mincss_putchar_utf8(ch, stdout);
            }
            printf("\"\n");
        }
        return;
    }

    /* Prime the one-ahead token-reader... */
    read_token(context);
    /* And read in the stage-one tree. */
    node *nod = read_stylesheet(context);

    if (context->debug_trace == MINCSS_TRACE_TREE) {
        /* Dump out the stage-one tree, stop. */
        mincss_dump_node(nod, 0);
        free_node(nod);
        return;
    }

    mincss_construct_stylesheet(context, nod);
    free_node(nod);
}

/* Read the next token, storing it in context->nexttok.
   We skip over comments.
 */
static void read_token(mincss_context *context)
{
    tokentype typ;

    /* Free the current nexttok contents. */
    token *tok = &(context->nexttok);
    tok->typ = tok_EOF;
    if (tok->text) {
        free(tok->text);
        tok->text = NULL;
    }
    tok->len = 0;
    tok->div = 0;

    /* Run forwards to the next meaningful token. */
    while (1) {
        typ = mincss_next_token(context);
        if (typ == tok_EOF)
            return;
        /* if (ttyp == tok_Space && skipwhite)
            continue; */
        if (typ != tok_Comment)
            break;
    }

    /* We're going to copy out the content part of the token string. Skip
       string delimiters, the @ in AtKeyword, etc. If the content length
       is zero, we'll skip allocating entirely. */
    /* ### We could assert a bunch here, for safety. */

    int pos = 0;
    int len = context->tokenlen;
    int div = 0;
    switch (typ) {

    case tok_Ident:
    case tok_Number:
    case tok_Delim:
        /* Copy the entire text. */
        break;

    case tok_Dimension:
        /* Copy the entire text; retain the division mark. */
        div = context->tokendiv;
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
        tok->div = div;
    }
    else {
        tok->len = 0;
        tok->text = NULL;
        tok->div = 0;
    }
}

/* If the current token is whitespace, read more tokens until it's not. */
static void read_token_skipspace(mincss_context *context)
{
    while (context->nexttok.typ == tok_Space)
        read_token(context);
}

static node *new_node(mincss_context *context, nodetype typ)
{
    node *nod = (node *)malloc(sizeof(node));
    if (!nod)
        return NULL; /*### malloc error*/
    nod->typ = typ;
    nod->linenum = context->linenum;
    nod->text = NULL;
    nod->textlen = 0;
    nod->textdiv = 0;
    nod->nodes = NULL;
    nod->numnodes = 0;
    nod->nodes_size = 0;
    return nod;
}

static node *new_node_token(mincss_context *context, token *tok)
{
    /* This is always called with tok = &context->nexttok, so I could
       drop the second argument, really. */
    node *nod = new_node(context, nod_Token);
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

void mincss_dump_node(node *nod, int depth)
{
    if (depth >= 0) {
        printf("%02d:", nod->linenum);
        dump_indent(depth);
    }

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
    case nod_TopLevel:
        printf("TopLevel");
        break;
    case nod_AtRule:
        printf("AtRule");
        break;
    case nod_Ruleset:
        printf("Ruleset");
        break;
    case nod_Selector:
        printf("Selector");
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
    if (nod->textdiv) {
        printf(" <%d/%d>", nod->textdiv, nod->textlen);
    }

    if (depth >= 0) {
        printf("\n");

        if (nod->nodes) {
            int ix;
            for (ix=0; ix<nod->numnodes; ix++) {
                mincss_dump_node(nod->nodes[ix], depth+1);
            }
        }
    }
}

void mincss_dump_node_range(char *label, node *nod, int start, int end)
{
    printf("%s from %d to %d: ", label, start, end);
    int ix;
    for (ix=start; ix<end; ix++) {
        if (ix > start)
            printf(", ");
        mincss_dump_node(nod->nodes[ix], -1);
    }
    printf("\n");
}

static void node_copy_text(node *nod, token *tok)
{
    if (tok->text) {
        nod->textlen = tok->len;
        nod->text = (int32_t *)malloc(sizeof(int32_t) * tok->len);
        memcpy(nod->text, tok->text, sizeof(int32_t) * tok->len);
        nod->textdiv = tok->div;
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

/* Read in the first-stage syntax tree. This will be a Stylesheet node,
   containing AtRule and TopLevel nodes. 
*/
static node *read_stylesheet(mincss_context *context)
{
    node *sheetnod = new_node(context, nod_Stylesheet);

    while (1) {
        tokentype toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF)
            break;

        if (toktyp == tok_CDO || toktyp == tok_CDC) {
            /* Comment delimiters are ignored at the top level. */
            read_token(context);
            continue;
        }

        if (toktyp == tok_Space) {
            /* We also ignore whitespace between statements. */
            read_token(context);
            continue;
        }

        node *nod = read_statement(context);
        if (nod)
            node_add_node(sheetnod, nod);
    }

    return sheetnod;
}

/* Read one AtRule or TopLevel. A TopLevel is basically a sequence of anything
   that isn't an AtRule. 
*/
static node *read_statement(mincss_context *context)
{
    tokentype toktyp = context->nexttok.typ;
    if (toktyp == tok_EOF)
        return NULL;

    if (toktyp == tok_AtKeyword) {
        node *nod = new_node(context, nod_AtRule);
        node_copy_text(nod, &context->nexttok);
        read_token(context);
        read_token_skipspace(context);
        read_any_until_semiblock(context, nod);
        toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF) {
            return nod; /* end of file */
        }
        if (toktyp == tok_Semicolon) {
            /* drop the semicolon, end the AtRule */
            read_token(context);
            read_token_skipspace(context);
            return nod;
        }
        if (toktyp == tok_LBrace) {
            /* beginning of block */
            node *blocknod = read_block(context);
            if (!blocknod) {
                /* error */
                free_node(nod);
                return NULL;
            }
            node_add_node(nod, blocknod);
            return nod; /* the block ends the AtRule */
        }
        /* error */
        mincss_note_error(context, "(Internal) Unexpected token after read_any_until_semiblock");
        free_node(nod);
        return NULL;
    }
    else {
        /* The syntax spec lets us parse a ruleset here. But we don't
           bother; we just parse any/blocks until the next AtKeyword. 
           They all get stuffed into a single TopLevel node. (Unless
           there's no content at all, in which case we don't create a
           node.) */
        node *nod = new_node(context, nod_TopLevel);
        while (1) {
            read_any_top_level(context, nod);
            tokentype toktyp = context->nexttok.typ;
            if (toktyp == tok_EOF) {
                break; /* end of file */
            }
            if (toktyp == tok_AtKeyword) {
                break; /* an @-rule is next */
            }
            if (toktyp == tok_LBrace) {
                node *blocknod = read_block(context);
                if (!blocknod) {
                    /* error, already reported */
                    continue;
                }
                node_add_node(nod, blocknod);
                continue;
            }
            mincss_note_error(context, "(Internal) Unexpected token after read_any_top_level");
            free_node(nod);
            return NULL;
        }
        if (nod->numnodes == 0) {
            /* empty group, don't bother returning it. */
            free_node(nod);
            return NULL;
        }
        return nod;
    }
}

/* The "any" production in the CSS grammar is any token except
   Semicolon, AtKeyword, LBrace, RBrace, RParen, RBracket, CDO, CDC.
   An LParen or LBracket causes a balanced read, as does Function.
   Bad tokens are discarded with a warning (including a balanced
   block), unless it's an expected terminator.

   We have three functions to suck in "any". In fact they all read a
   sequence of "any" nodes, appending them to the given node. They
   differ in their termination conditions and what's considered an
   error. I could probably combine them, but the result would be messy
   (messier).
*/

/* Read an "any*" sequence, up until end-of-file or an AtKeyword
   token. Appends nodes to the node passed in (which will be a
   TopLevel).

   On return, the current token is EOF, LBrace (meaning start of
   a block), or AtKeyword.
*/
static void read_any_top_level(mincss_context *context, node *nod)
{
    while (1) {
        tokentype toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF) {
            return; /* end of file */
        }

        switch (toktyp) {

        case tok_LBrace:
            return;
            
        case tok_Function: {
            node *subnod = new_node(context, nod_Function);
            node_copy_text(subnod, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LParen: {
            node *subnod = new_node(context, nod_Parens);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LBracket: {
            node *subnod = new_node(context, nod_Brackets);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RBracket);
            continue;
        }

        case tok_CDO:
        case tok_CDC:
            /* Swallow, ignore */
            read_token(context);
            read_token_skipspace(context);
            continue;

        case tok_RParen:
            mincss_note_error(context, "Unexpected close-paren");
            read_token(context);
            continue;

        case tok_RBracket:
            mincss_note_error(context, "Unexpected close-bracket");
            read_token(context);
            continue;

        case tok_AtKeyword:
            return;

        case tok_Semicolon: {
            node *toknod = new_node_token(context, &context->nexttok);
            node_add_node(nod, toknod);
            read_token(context);
            read_token_skipspace(context);
            continue;
        }

        default: {
            node *toknod = new_node_token(context, &context->nexttok);
            node_add_node(nod, toknod);
            read_token(context);
        }
        }
    }
}

/* Read an "any*" sequence, up until a semicolon or the beginning of a
   block. An AtKeyword is considered an error.

   On return, the current token is EOF, Semicolon, or LBrace.
*/
static void read_any_until_semiblock(mincss_context *context, node *nod)
{
    while (1) {
        tokentype toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF) {
            mincss_note_error(context, "Incomplete @-rule");
            /* treat as terminated */
            return;
        }

        switch (toktyp) {

        case tok_Semicolon: 
            return;
        
        case tok_LBrace:
            return;
            
        case tok_Function: {
            node *subnod = new_node(context, nod_Function);
            node_copy_text(subnod, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LParen: {
            node *subnod = new_node(context, nod_Parens);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LBracket: {
            node *subnod = new_node(context, nod_Brackets);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RBracket);
            continue;
        }

        case tok_CDO:
        case tok_CDC:
            mincss_note_error(context, "HTML comment delimiters not allowed inside @-rule");
            read_token(context);
            read_token_skipspace(context);
            continue;

        case tok_RParen:
            mincss_note_error(context, "Unexpected close-paren inside @-rule");
            read_token(context);
            continue;

        case tok_RBracket:
            mincss_note_error(context, "Unexpected close-bracket inside @-rule");
            read_token(context);
            continue;

        case tok_AtKeyword:
            mincss_note_error(context, "Unexpected @-keyword inside @-rule");
            read_token(context);
            continue;

        default: {
            node *toknod = new_node_token(context, &context->nexttok);
            node_add_node(nod, toknod);
            read_token(context);
        }
        }
    }
}

/* Read an "any* sequence up until a particular close token (RBracket or
   RParen). Blocks cannot occur in this context.

   On return, the current token is whatever's next.
*/
static void read_any_until_close(mincss_context *context, node *nod, tokentype closetok)
{
    while (1) {
        tokentype toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF) {
            mincss_note_error(context, "Missing close-delimiter");
            return;
        }

        if (toktyp == closetok) {
            /* The expected close-token. */
            read_token(context);
            return;
        }

        switch (toktyp) {

        case tok_Semicolon: 
            mincss_note_error(context, "Unexpected semicolon inside brackets");
            read_token(context);
            continue;
            
        case tok_LBrace:
            mincss_note_error(context, "Unexpected block inside brackets");
            read_block(context);
            continue;

        case tok_Function: {
            node *subnod = new_node(context, nod_Function);
            node_copy_text(subnod, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LParen: {
            node *subnod = new_node(context, nod_Parens);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LBracket: {
            node *subnod = new_node(context, nod_Brackets);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RBracket);
            continue;
        }

        case tok_CDO:
        case tok_CDC:
            mincss_note_error(context, "HTML comment delimiters not allowed inside brackets");
            read_token(context);
            read_token_skipspace(context);
            continue;

        case tok_RParen:
            mincss_note_error(context, "Unexpected close-paren inside brackets");
            read_token(context);
            continue;

        case tok_RBracket:
            mincss_note_error(context, "Unexpected close-bracket inside brackets");
            read_token(context);
            continue;

        case tok_AtKeyword:
            mincss_note_error(context, "Unexpected @-keyword inside brackets");
            read_token(context);
            continue;

        default: {
            node *toknod = new_node_token(context, &context->nexttok);
            node_add_node(nod, toknod);
            read_token(context);
        }
        }
    }
}

/* Read in a block. When called, the current token must be an LBrace.
   On return, the current token is whatever was after the RBrace.
*/
static node *read_block(mincss_context *context)
{
    tokentype toktyp = context->nexttok.typ;
    if (toktyp == tok_EOF || toktyp != tok_LBrace) {
        mincss_note_error(context, "(Internal) Unexpected token at read_block");
        return NULL;
    }
    read_token(context);
    read_token_skipspace(context);

    node *nod = new_node(context, nod_Block);

    while (1) {
        toktyp = context->nexttok.typ;
        if (toktyp == tok_EOF) {
            mincss_note_error(context, "Unexpected end of block");
            return nod;
        }

        switch (toktyp) {

        case tok_RBrace:
            /* Done */
            read_token(context);
            read_token_skipspace(context);
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
            node *subnod = new_node_token(context, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
            continue;
        }

        case tok_AtKeyword: {
            node *atnod = new_node_token(context, &context->nexttok);
            node_add_node(nod, atnod);
            read_token(context);
            continue;
        }

        case tok_Function: {
            node *subnod = new_node(context, nod_Function);
            node_copy_text(subnod, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LParen: {
            node *subnod = new_node(context, nod_Parens);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RParen);
            continue;
        }

        case tok_LBracket: {
            node *subnod = new_node(context, nod_Brackets);
            node_add_node(nod, subnod);
            read_token(context);
            read_any_until_close(context, subnod, tok_RBracket);
            continue;
        }

        case tok_CDO:
        case tok_CDC:
            mincss_note_error(context, "HTML comment delimiters not allowed inside block");
            read_token(context);
            read_token_skipspace(context);
            continue;

        case tok_RParen:
            mincss_note_error(context, "Unexpected close-paren inside block");
            read_token(context);
            continue;

        case tok_RBracket:
            mincss_note_error(context, "Unexpected close-bracket inside block");
            read_token(context);
            continue;

        default: {
            /* Anything else is a single "any". */
            node *subnod = new_node_token(context, &context->nexttok);
            node_add_node(nod, subnod);
            read_token(context);
        }
        }
    }
}

