#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

#define node_note_error(context, nod, msg) mincss_note_error_line(context, msg, nod->linenum)
#define node_is_space(nod) ((nod)->typ == nod_Token && (nod)->toktype == tok_Space)

typedef enum operator_enum {
    op_None = 0,
    op_Plus = '+',
    op_GT = '>',
    op_Comma = ',',
    op_Slash = '/',
} operator;

typedef struct sselector_struct {
    operator op;
    int32_t *element;
    int elementlen;
    int32_t **classes;
    int numclasses, classes_size;
    /*### attributes, pseudo */
} sselector;

typedef struct selector_struct {
    sselector **sselectors;
    int numsselectors, sselectors_size;
} selector;

typedef struct pvalue_struct {
    operator operator;
    token tok;
} pvalue;

typedef struct declaration_struct {
    int32_t *property;
    int propertylen;
    pvalue **pvalues;
    int numpvalues, pvalues_size;
} declaration;

typedef struct rulegroup_struct {
    selector **selectors;
    int numselectors, selectors_size;
    declaration **declarations;
    int numdeclarations, declarations_size;
} rulegroup;

struct stylesheet_struct {
    rulegroup **rulegroups;
    int numrulegroups, rulegroups_size;
};

static stylesheet *stylesheet_new(void);
static void stylesheet_delete(stylesheet *sheet);
static int stylesheet_add_rulegroup(stylesheet *sheet, rulegroup *rgrp);
static rulegroup *rulegroup_new(void);
static void rulegroup_delete(rulegroup *rgrp);
static void rulegroup_dump(rulegroup *rgrp, int depth);

static void construct_atrule(mincss_context *context, node *nod);
static void construct_rulesets(mincss_context *context, node *nod, stylesheet *sheet);
static void construct_selectors(mincss_context *context, node *nod, int start, int end, rulegroup *rgrp);
static int construct_selector(mincss_context *context, node *nod, int start, int end);
static void construct_declarations(mincss_context *context, node *nod, rulegroup *rgrp);
static void construct_declaration(mincss_context *context, node *nod, int propstart, int propend, int valstart, int valend);
static void construct_expr(mincss_context *context, node *nod, int start, int end, int toplevel);

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

void mincss_construct_stylesheet(mincss_context *context, node *nod)
{
    int ix;

    stylesheet *sheet = stylesheet_new();
    if (!sheet) {
	return; /*### memory*/
    }

    for (ix=0; ix<nod->numnodes; ix++) {
        node *subnod = nod->nodes[ix];
        if (subnod->typ == nod_AtRule)
            construct_atrule(context, subnod);
        else if (subnod->typ == nod_TopLevel)
            construct_rulesets(context, subnod, sheet);
        else
            mincss_note_error(context, "(Internal) Invalid node type in construct_stylesheet");
    }

    mincss_stylesheet_dump(sheet);
    stylesheet_delete(sheet);
    /* return sheet; ### */
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

static void construct_rulesets(mincss_context *context, node *nod, stylesheet *sheet)
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

        rulegroup *rgrp = rulegroup_new();
        if (!rgrp) {
            return; /*### memory*/
        }

        construct_selectors(context, nod, start, blockpos, rgrp);

        node *blocknod = nod->nodes[blockpos];
        construct_declarations(context, blocknod, rgrp);

        if (!rgrp->numselectors || !rgrp->numdeclarations) {
            /* Empty, skip. */
            rulegroup_delete(rgrp);
        }
        else {
            if (!stylesheet_add_rulegroup(sheet, rgrp))
                rulegroup_delete(rgrp);
        }
    }
}

static void construct_selectors(mincss_context *context, node *nod, int start, int end, rulegroup *rgrp)
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
            int finalpos = construct_selector(context, nod, pos, ix);
            if (finalpos < ix)
                node_note_error(context, nod->nodes[finalpos], "Unrecognized text in selector");
        }
        else {
            node_note_error(context, nod->nodes[start], "Block has empty selector");
        }
        pos = ix+1;
    }
}

static int construct_selector(mincss_context *context, node *nod, int start, int end)
{
    mincss_dump_node_range("selector", nod, start, end); /*###*/

    int pos = start;
    /* Start by parsing a simple selector. This is a chain of elements,
       classes, etc with no top-level whitespace. */

    int has_element = 0;
    if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && node_text_matches(nod->nodes[pos], "*")) {
        /*### asterisk */
        printf("### element asterisk\n");
        pos++;
        has_element = 1;
    }
    else if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Ident) {
        /* ### element-name */
        printf("### element ident\n");
        pos++;
        has_element = 1;
    }

    int count = 0;
    while (pos < end) {
        if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Hash) {
            /* ### hash */
            printf("### hash\n");
            pos++;
            count++;
        }
        else if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && node_text_matches(nod->nodes[pos], ".")
                 && pos+1 < end && nod->nodes[pos+1]->typ == nod_Token && nod->nodes[pos+1]->toktype == tok_Ident) {
            /*### class */
            printf("### class\n");
            pos += 2;
            count++;
        }
        else if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && node_text_matches(nod->nodes[pos], ":")
                 && pos+1 < end && nod->nodes[pos+1]->typ == nod_Token && nod->nodes[pos+1]->toktype == tok_Ident) {
            /*### pseudo */
            printf("### pseudo\n");
            /* ### does not catch the :func() case */
            pos += 2;
            count++;
        }
        /*### or [attribute] */
        else {
            /* Not a recognized part of a simple selector. */
            break;
        }
    }
    
    if (!has_element && !count) {
        node_note_error(context, nod->nodes[start], "No selector found");
    }

    if (pos < end) {
        /* What happens next depends on whether there's whitespace. */
        int hasspace = 0;
        while (pos < end && node_is_space(nod->nodes[pos])) {
            pos++;
            hasspace++;
        }
        if (!hasspace) {
            /* Must be a combinator (+/>) followed by another selector. */
            if (pos < end) {
                if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && (node_text_matches(nod->nodes[pos], "+") || node_text_matches(nod->nodes[pos], ">"))) {
                    int combinator = nod->nodes[pos]->text[0];
                    printf("### combinator %c\n", combinator);
                    pos++;
                    while (pos < end && node_is_space(nod->nodes[pos])) {
                        pos++;
                        hasspace++;
                    }
                    int newpos = pos;
                    if (pos < end) {
                        newpos = construct_selector(context, nod, pos, end);
                    }
                    if (newpos == pos)
                        node_note_error(context, nod->nodes[start], "Combinator not followed by selector");
                    pos = newpos;
                }
            }
        }
        else {
            /* Must be nothing, or a selector, or a combinator
               followed by a selector. */
            if (pos < end) {
                int combinator = 0;
                if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && (node_text_matches(nod->nodes[pos], "+") || node_text_matches(nod->nodes[pos], ">"))) {
                    combinator = nod->nodes[pos]->text[0];
                    printf("### combinator %c\n", combinator);
                    pos++;
                    while (pos < end && node_is_space(nod->nodes[pos])) {
                        pos++;
                        hasspace++;
                    }
                }
                int newpos = pos;
                if (pos < end) {
                    newpos = construct_selector(context, nod, pos, end);
                }
                if (combinator && newpos == pos)
                    node_note_error(context, nod->nodes[start], "Combinator not followed by selector");
                pos = newpos;
            }
        }
    }

    return pos;
}

static void construct_declarations(mincss_context *context, node *nod, rulegroup *rgrp)
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
    mincss_dump_node_range(" prop", nod, propstart, propend); /*###*/
    mincss_dump_node_range("  val", nod, valstart, valend); /*###*/

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
            mincss_dump_node(valnod, 0);
            terms += 1;
            unaryop = 0;
            valsep = 0;
            continue;
        }

        if (valnod->typ == nod_Token) {
            if (valnod->toktype == tok_Number || valnod->toktype == tok_Percentage || valnod->toktype == tok_Dimension) {
                printf("### %c %c: ", (valsep?valsep:' '), (unaryop?unaryop:' '));
                mincss_dump_node(valnod, 0);
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
                mincss_dump_node(valnod, 0);
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

static void dump_indent(int val)
{
    int ix;
    for (ix=0; ix<val; ix++)
        putchar(' ');
}

/* The general principle of the stylesheet data structure is that _new
   functions can fail, returning NULL, as long as they leave the existing
   structure in a non-broken state. In practice this should never happen
   anyhow. */

static stylesheet *stylesheet_new()
{
    stylesheet *sheet = (stylesheet *)malloc(sizeof(stylesheet));
    if (!sheet)
	return NULL;

    sheet->rulegroups = NULL;
    sheet->numrulegroups = 0;
    sheet->rulegroups_size = 0;

    return sheet;
}

static void stylesheet_delete(stylesheet *sheet)
{
    if (sheet->rulegroups) {
	int ix;

	for (ix=0; ix<sheet->numrulegroups; ix++) 
	    rulegroup_delete(sheet->rulegroups[ix]);

	free(sheet->rulegroups);
	sheet->rulegroups = NULL;
	sheet->numrulegroups = 0;
	sheet->rulegroups_size = 0;
    }

    free(sheet);
}

void mincss_stylesheet_dump(stylesheet *sheet)
{
    printf("Stylesheet:\n");

    if (sheet->rulegroups) {
	int ix;
	for (ix=0; ix<sheet->numrulegroups; ix++) 
	    rulegroup_dump(sheet->rulegroups[ix], 1);
    }
}

static int stylesheet_add_rulegroup(stylesheet *sheet, rulegroup *rgrp)
{
    if (!sheet->rulegroups) {
	sheet->rulegroups_size = 4;
	sheet->rulegroups = (rulegroup **)malloc(sheet->rulegroups_size * sizeof(rulegroup *));
    }
    else if (sheet->numrulegroups >= sheet->rulegroups_size) {
	sheet->rulegroups_size *= 2;
	sheet->rulegroups = (rulegroup **)realloc(sheet->rulegroups, sheet->rulegroups_size * sizeof(rulegroup *));
    }
    if (!sheet->rulegroups) {
	sheet->numrulegroups = 0;
	sheet->rulegroups_size = 0;
	return 0;
    }

    sheet->rulegroups[sheet->numrulegroups++] = rgrp;
    return 1;
}

static rulegroup *rulegroup_new()
{
    rulegroup *rgrp = (rulegroup *)malloc(sizeof(rulegroup));
    if (!rgrp)
	return NULL;

    rgrp->selectors = NULL;
    rgrp->numselectors = 0;
    rgrp->selectors_size = 0;
    rgrp->declarations = NULL;
    rgrp->numdeclarations = 0;
    rgrp->declarations_size = 0;

    return rgrp;
}

static void rulegroup_delete(rulegroup *rgrp)
{
    /*###*/
}

static void rulegroup_dump(rulegroup *rgrp, int depth)
{
    dump_indent(depth);
    printf("### rulegroup\n");
    /*###*/
}
