#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

typedef enum nodetype_enum {
    nod_None = 0,
    nod_Token = 1,
    nod_Stylesheet = 2,
    nod_TopLevel = 3,
    nod_AtRule = 4,
    nod_Ruleset = 5,
    nod_Selector = 6,
    nod_Block = 7,
    nod_Parens = 8,
    nod_Brackets = 9,
    nod_Function = 10,
} nodetype;

typedef struct node_struct {
    nodetype typ;

    int linenum; /* for debugging */

    /* All of these fields are optional. */
    int32_t *text;
    int textlen;
    int textdiv;

    tokentype toktype;
    
    struct node_struct **nodes;
    int numnodes;
    int nodes_size;
} node;

static void read_token(mincss_context *context);

static node *new_node(mincss_context *context, nodetype typ);
static node *new_node_token(mincss_context *context, token *tok);
static void free_node(node *nod);
static void dump_node(node *nod, int depth);
static void node_copy_text(node *nod, token *tok);
static void node_add_node(node *nod, node *nod2);

#define node_note_error(context, nod, msg) mincss_note_error_line(context, msg, nod->linenum)
#define node_is_space(nod) ((nod)->typ == nod_Token && (nod)->toktype == tok_Space)

static node *read_stylesheet(mincss_context *context);
static node *read_statement(mincss_context *context);
static node *read_block(mincss_context *context);
static void read_any_top_level(mincss_context *context, node *nod);
static void read_any_until_semiblock(mincss_context *context, node *nod);
static void read_any_until_close(mincss_context *context, node *nod, tokentype closetok);

static void construct_stylesheet(mincss_context *context, node *nod);

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
        dump_node(nod, 0);
        free_node(nod);
        return;
    }

    construct_stylesheet(context, nod);
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

static void dump_node(node *nod, int depth)
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
                dump_node(nod->nodes[ix], depth+1);
            }
        }
    }
}

static void dump_node_range(char *label, node *nod, int start, int end)
{
    printf("%s from %d to %d: ", label, start, end);
    int ix;
    for (ix=start; ix<end; ix++) {
        if (ix > start)
            printf(", ");
        dump_node(nod->nodes[ix], -1);
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

/* Test whether the text of a node matches the given ASCII string.
   (Case-insensitive.) */
static int node_text_matches(node *nod, char *text)
{
    int len = strlen(text);
    if (!text || !len)
        return (nod->text == NULL);
    if (len != nod->textlen)
        return 0;

    int ix;
    for (ix=0; ix<len; ix++) {
        int ch = text[ix];
        int altch = ch;
        if (ch >= 'A' && ch <= 'Z')
            altch = ch + ('a'-'A');
        else if (ch >= 'a' && ch <= 'z')
            altch = ch - ('a'-'A');
        if (nod->text[ix] != ch && nod->text[ix] != altch)
            return 0;
    }
    return 1;
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

static void construct_atrule(mincss_context *context, node *nod);
static void construct_rulesets(mincss_context *context, node *nod);
static void construct_selectors(mincss_context *context, node *nod, int start, int end);
static void construct_selector(mincss_context *context, node *nod, int start, int end);
static void construct_declarations(mincss_context *context, node *nod);
static void construct_declaration(mincss_context *context, node *nod, int propstart, int propend, int valstart, int valend);
static void construct_expr(mincss_context *context, node *nod, int start, int end, int toplevel);

static void construct_stylesheet(mincss_context *context, node *nod)
{
    int ix;
    for (ix=0; ix<nod->numnodes; ix++) {
        node *subnod = nod->nodes[ix];
        if (subnod->typ == nod_AtRule)
            construct_atrule(context, subnod);
        else if (subnod->typ == nod_TopLevel)
            construct_rulesets(context, subnod);
        else
            mincss_note_error(context, "(Internal) Invalid node type in construct_stylesheet");
    }
}

static void construct_atrule(mincss_context *context, node *nod)
{
    if (node_text_matches(nod, "charset")) {
        node_note_error(context, nod, "@charset rule ignored (must be UTF-8)");
        return;
    }
    if (node_text_matches(nod, "import")) {
        node_note_error(context, nod, "@import rule ignored");
        return;
    }
    if (node_text_matches(nod, "page")) {
        node_note_error(context, nod, "@page rule ignored");
        return;
    }
    if (node_text_matches(nod, "media")) {
        /* Could parse this, but currently we don't. */
        return;
    }
    /* Unrecognized at-rule; ignore. */
}

static void construct_rulesets(mincss_context *context, node *nod)
{
    /* Ruleset content parses as "a bunch of stuff that isn't a block"
       (the selector) followed by a block. */

    int start = 0;
    int blockpos = -1;
    for (start = 0; start < nod->numnodes; start = blockpos+1) {
        int ix;
        for (ix = start, blockpos = -1; ix < nod->numnodes; ix++) {
            if (nod->nodes[ix]->typ == nod_Block) {
                blockpos = ix;
                break;
            }
        }

        if (blockpos < 0) {
            /* The last ruleset is missing its block. */
            node_note_error(context, nod->nodes[start], "Selector missing block");
            return;
        }
        if (start >= blockpos) {
            /* This block has no selectors. Ignore it. */
            node_note_error(context, nod->nodes[start], "Block missing selectors");
            continue;
        }

        construct_selectors(context, nod, start, blockpos);

        node *blocknod = nod->nodes[blockpos];
        construct_declarations(context, blocknod);
    }
}

static void construct_selectors(mincss_context *context, node *nod, int start, int end)
{
    int pos = start;

    /* Split the range by commas; each is a selector. */
    while (pos < end) {
        if (node_is_space(nod->nodes[pos])) {
            /* skip initial whitespace */
            pos++;
            continue;
        }

        int ix;
        for (ix = pos; ix < end; ix++) {
            if (nod->nodes[ix]->typ == nod_Token && nod->nodes[ix]->toktype == tok_Delim && node_text_matches(nod->nodes[ix], ","))
                break;
        }

        if (ix > pos) {
            construct_selector(context, nod, pos, ix);
        }
        else {
            node_note_error(context, nod->nodes[start], "Block has empty selector");
        }
        pos = ix+1;
    }
}

static void construct_selector(mincss_context *context, node *nod, int start, int end)
{
    dump_node_range("selector", nod, start, end); /*###*/

    /* Start by parsing a simple selector. This is a chain of elements,
       classes, etc with no top-level whitespace. */

    /*### simple selector: ident|* followed by HASH, .IDENT, [...], :...
      OR one or more HASH, .IDENT, [...], :... */
}

static void construct_declarations(mincss_context *context, node *nod)
{
    int start = 0;
    int semipos = -1;
    /* Split the range by semicolons; each is a declaration. */
    while (start < nod->numnodes) {
        if (node_is_space(nod->nodes[start])) {
            /* skip initial whitespace */
            start++;
            continue;
        }

        /* Locate the colon and semicolon in the declaration. */
        int ix;
        int colonpos = -1;
        for (ix = start; ix < nod->numnodes; ix++) {
            if (nod->nodes[ix]->typ == nod_Token && nod->nodes[ix]->toktype == tok_Colon && colonpos < 0)
                colonpos = ix;
            if (nod->nodes[ix]->typ == nod_Token && nod->nodes[ix]->toktype == tok_Semicolon) 
                break;
        }
        semipos = ix;

        if (semipos > start) {
            if (colonpos < 0) {
                node_note_error(context, nod->nodes[start], "Declaration lacks colon");
            }
            else {
                /* Locate the first non-whitespace after the colon. */
                int valstart = colonpos+1;
                while (valstart < semipos) {
                    if (!node_is_space(nod->nodes[valstart]))
                        break;
                    valstart++;
                }
                construct_declaration(context, nod, start, colonpos, valstart, semipos);
            }
        }
        start = semipos+1;
    }
}

static void construct_declaration(mincss_context *context, node *nod, int propstart, int propend, int valstart, int valend)
{
    dump_node_range(" prop", nod, propstart, propend); /*###*/
    dump_node_range("  val", nod, valstart, valend); /*###*/

    int ix;

    if (propend <= propstart) {
        node_note_error(context, nod->nodes[propstart], "Declaration lacks property");
        return;
    }
    if (valend <= propstart || valend <= valstart) {
        /* We mark this error at propstart to be extra careful about
           array overflow. */
        node_note_error(context, nod->nodes[propstart], "Declaration lacks value");
        return;
    }

    /* The property part must be a single identifier (plus optional
       whitespace). Check this by backing propend up through any
       trailing whitespace. */

    while (propend > propstart) {
        if (!node_is_space(nod->nodes[propend-1]))
            break;
        propend--;
    }

    if (propend - propstart != 1 || nod->nodes[propstart]->typ != nod_Token || nod->nodes[propstart]->toktype != tok_Ident) {
        node_note_error(context, nod->nodes[propstart], "Declaration property is not an identifier");
        return;
    }

    /* The "!important" flag is a special case. It's always at the
       end of the value. We try backing up through that. It's a nuisance,
       because there can be whitespace. */
    int important = 0;
    {
        int counter = 0;
        ix = valend;
        while (ix > valstart) {
            node *subnod = nod->nodes[ix-1];
            if (!node_is_space(subnod)) {
                if (counter == 0) {
                    if (subnod->typ == nod_Token && subnod->toktype == tok_Ident && node_text_matches(subnod, "important"))
                        counter++;
                    else
                        break;
                }
                else if (counter == 1) {
                    if (subnod->typ == nod_Token && subnod->toktype == tok_Delim && node_text_matches(subnod, "!"))
                        counter++;
                    else
                        break;
                }
            }
            if (counter >= 2) {
                valend = ix;
                important = 1;
                break;
            }
            ix--;
        }
    }

    construct_expr(context, nod, valstart, valend, 1);
}

static void construct_expr(mincss_context *context, node *nod, int start, int end, int toplevel)
{
    int ix;

    /* Parse out a list of values. These are normally separated only 
       by whitespace, but a slash is possible (see the CSS spec re the
       "font" shorthand property). We don't try to work out the value
       type or check type validity here. We do verify the expression
       syntax, though. */
    int valsep = 0;
    int unaryop = 0;
    int terms = 0;
    for (ix=start; ix<end; ix++) {
        node *valnod = nod->nodes[ix];
        if (node_is_space(valnod)) {
            if (unaryop) {
                node_note_error(context, nod, "Unexpected +/- with no value");
                return;
            }
            continue;
        }

        if (valnod->typ == nod_Token) {
            /*### This accepts a slash/comma before the first term */
            if (valnod->toktype == tok_Delim && node_text_matches(valnod, "/") && !valsep && !unaryop) {
                valsep = '/';
                continue;
            }
            if (valnod->toktype == tok_Delim && node_text_matches(valnod, ",") && !valsep && !unaryop) {
                valsep = ',';
                continue;
            }
            if (valnod->toktype == tok_Delim && node_text_matches(valnod, "+") && !unaryop) {
                unaryop = '+';
                continue;
            }
            if (valnod->toktype == tok_Delim && node_text_matches(valnod, "-") && !unaryop) {
                unaryop = '-';
                continue;
            }
        }

        if (valnod->typ == nod_Function) {
            if (unaryop) {
                node_note_error(context, valnod, "Function cannot have +/-");
                return; /*###*/
            }
            construct_expr(context, valnod, 0, valnod->numnodes, 0); /*### store */
            printf("### %c %c: ", (valsep?valsep:' '), (unaryop?unaryop:' '));
            dump_node(valnod, 0);
            terms += 1;
            unaryop = 0;
            valsep = 0;
            continue;
        }

        if (valnod->typ == nod_Token) {
            if (valnod->toktype == tok_Number || valnod->toktype == tok_Percentage || valnod->toktype == tok_Dimension) {
                printf("### %c %c: ", (valsep?valsep:' '), (unaryop?unaryop:' '));
                dump_node(valnod, 0);
                terms += 1;
                unaryop = 0;
                valsep = 0;
                continue;
            }

            if (valnod->toktype == tok_String || valnod->toktype == tok_Ident || valnod->toktype == tok_URI) {
                if (unaryop) {
                    node_note_error(context, valnod, "Declaration value cannot have +/-");
                    return; /*###*/
                }
                printf("### %c %c: ", (valsep?valsep:' '), (unaryop?unaryop:' '));
                dump_node(valnod, 0);
                terms += 1;
                unaryop = 0;
                valsep = 0;
                continue;
            }
        }

        node_note_error(context, valnod, "Invalid declaration value");
        return; /*###*/
    }

    if (valsep) {
        node_note_error(context, nod, "Unexpected trailing separator");
        return; /*###*/
    }
    if (unaryop) {
        node_note_error(context, nod, "Unexpected trailing +/-");
        return; /*###*/
    }
    if (!terms) {
        node_note_error(context, nod, "Missing declaration value");
        return; /*###*/
    }

    /* ### all ok */
}
