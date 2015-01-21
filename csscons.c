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

typedef struct ustring_struct {
    int32_t *text;
    int len;
} ustring;

typedef struct selectel_struct {
    operator op;
    int32_t *element;
    int elementlen;
    ustring **classes;
    int numclasses, classes_size;
    ustring **hashes;
    int numhashes, hashes_size;
    /*### attributes, pseudo */
} selectel;

typedef struct selector_struct {
    selectel **selectels;
    int numselectels, selectels_size;
} selector;

typedef struct pvalue_struct {
    operator op;
    token tok;
} pvalue;

typedef struct declaration_struct {
    int important;
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
static int rulegroup_add_declaration(rulegroup *rgrp, declaration *decl);
static int rulegroup_add_selector(rulegroup *rgrp, selector *sel);
static selector *selector_new(void);
static void selector_delete(selector *sel);
static void selector_dump(selector *sel, int depth);
static int selector_add_selectel(selector *sel, selectel *ssel);
static selectel *selectel_new(void);
static void selectel_delete(selectel *ssel);
static void selectel_dump(selectel *ssel, int depth);
static int selectel_add_class(selectel *ssel, ustring *ustr);
static int selectel_add_hash(selectel *ssel, ustring *ustr);
static declaration *declaration_new(void);
static void declaration_delete(declaration *decl);
static void declaration_dump(declaration *decl, int depth);
static pvalue *pvalue_new(void);
static void pvalue_delete(pvalue *pval);
static void pvalue_dump(pvalue *pval, int depth);
static ustring *ustring_new(void);
static ustring *ustring_new_from_node(node *nod);
static void ustring_delete(ustring *ustr);

static void construct_atrule(mincss_context *context, node *nod);
static void construct_rulesets(mincss_context *context, node *nod, stylesheet *sheet);
static void construct_selectors(mincss_context *context, node *nod, int start, int end, rulegroup *rgrp);
static void construct_selector(mincss_context *context, node *nod, int start, int end, int *posref, operator op, selector *sel);
static void construct_declarations(mincss_context *context, node *nod, rulegroup *rgrp);
static declaration *construct_declaration(mincss_context *context, node *nod, int propstart, int propend, int valstart, int valend);
static void construct_expr(mincss_context *context, node *nod, int start, int end, int toplevel);
static int32_t *copy_text(node *nod, int32_t *lenref);

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

        if (0 /*###!rgrp->numselectors || !rgrp->numdeclarations###*/) { /*### temporarily suppress this check */
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

    selector *sel = selector_new();
    if (!sel) {
        return; /*### memory*/
    }

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
            int finalpos = pos;
            construct_selector(context, nod, pos, ix, &finalpos, op_None, sel);
            if (finalpos < ix)
                node_note_error(context, nod->nodes[finalpos], "Unrecognized text in selector");
        }
        else {
            node_note_error(context, nod->nodes[start], "Block has empty selector");
        }
        pos = ix+1;
    }

    if (!sel->numselectels) {
        selector_delete(sel);
        return;
    }
    if (!rulegroup_add_selector(rgrp, sel))
        selector_delete(sel);
}

static void construct_selector(mincss_context *context, node *nod, int start, int end, int *posref, operator op, selector *sel)
{
    mincss_dump_node_range("selector", nod, start, end); /*###*/

    int pos = start;
    *posref = pos;
    /* Start by parsing a simple selector. This is a chain of elements,
       classes, etc with no top-level whitespace. */

    selectel *ssel = selectel_new();
    if (!ssel) {
        /*### memory */
        /* But we keep parsing, so as not to get stuck in an infinite loop. */
    }
    if (ssel)
        ssel->op = op;

    int has_element = 0;
    if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && node_text_matches(nod->nodes[pos], "*")) {
        if (ssel)
            ssel->element = copy_text(nod->nodes[pos], &ssel->elementlen);
        pos++;
        has_element = 1;
    }
    else if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Ident) {
        if (ssel)
            ssel->element = copy_text(nod->nodes[pos], &ssel->elementlen);
        pos++;
        has_element = 1;
    }

    int count = 0;
    while (pos < end) {
        if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Hash) {
            /* ### hash */
            if (ssel) {
                ustring *ustr = ustring_new_from_node(nod->nodes[pos]);
                if (ustr) {
                    if (!selectel_add_hash(ssel, ustr))
                        ustring_delete(ustr);
                }
            }
            pos++;
            count++;
        }
        else if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && node_text_matches(nod->nodes[pos], ".")
                 && pos+1 < end && nod->nodes[pos+1]->typ == nod_Token && nod->nodes[pos+1]->toktype == tok_Ident) {
            if (ssel) {
                ustring *ustr = ustring_new_from_node(nod->nodes[pos+1]);
                if (ustr) {
                    if (!selectel_add_class(ssel, ustr))
                        ustring_delete(ustr);
                }
            }
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

    if (ssel) {
        if (!selector_add_selectel(sel, ssel))
            selectel_delete(ssel);
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
                    operator combinator = op_None;
                    int32_t opch = nod->nodes[pos]->text[0];
                    if (opch == '+')
                        combinator = op_Plus;
                    else if (opch == '>')
                        combinator = op_GT;
                    else
                        node_note_error(context, nod->nodes[pos], "(Internal) Unrecognized operator character");
                    pos++;
                    while (pos < end && node_is_space(nod->nodes[pos])) {
                        pos++;
                        hasspace++;
                    }
                    int newpos = pos;
                    if (pos < end) {
                        construct_selector(context, nod, pos, end, &newpos, combinator, sel);
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
                operator combinator = op_None;
                if (nod->nodes[pos]->typ == nod_Token && nod->nodes[pos]->toktype == tok_Delim && (node_text_matches(nod->nodes[pos], "+") || node_text_matches(nod->nodes[pos], ">"))) {
                    int32_t opch = nod->nodes[pos]->text[0];
                    if (opch == '+')
                        combinator = op_Plus;
                    else if (opch == '>')
                        combinator = op_GT;
                    else
                        node_note_error(context, nod->nodes[pos], "(Internal) Unrecognized operator character");
                    pos++;
                    while (pos < end && node_is_space(nod->nodes[pos])) {
                        pos++;
                        hasspace++;
                    }
                }
                int newpos = pos;
                if (pos < end) {
                    construct_selector(context, nod, pos, end, &newpos, combinator, sel);
                }
                if (combinator && newpos == pos)
                    node_note_error(context, nod->nodes[start], "Combinator not followed by selector");
                pos = newpos;
            }
        }
    }

    *posref = pos;
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
                declaration *decl = construct_declaration(context, nod, start, colonpos, valstart, semipos);
                if (decl) {
                    if (!rulegroup_add_declaration(rgrp, decl))
                        declaration_delete(decl);
                }
            }
        }
        start = semipos+1;
    }
}

static declaration *construct_declaration(mincss_context *context, node *nod, int propstart, int propend, int valstart, int valend)
{
    mincss_dump_node_range(" prop", nod, propstart, propend); /*###*/
    mincss_dump_node_range("  val", nod, valstart, valend); /*###*/

    int ix;

    if (propend <= propstart) {
        node_note_error(context, nod->nodes[propstart], "Declaration lacks property");
        return NULL;
    }
    if (valend <= propstart || valend <= valstart) {
        /* We mark this error at propstart to be extra careful about
           array overflow. */
        node_note_error(context, nod->nodes[propstart], "Declaration lacks value");
        return NULL;
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
        return NULL;
    }

    declaration *decl = declaration_new();
    decl->property = copy_text(nod->nodes[propstart], &decl->propertylen);
    if (!decl->property) {
        declaration_delete(decl);
        return NULL; /*### memory*/
    }

    /* The "!important" flag is a special case. It's always at the
       end of the value. We try backing up through that. It's a nuisance,
       because there can be whitespace. */
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
                decl->important = 1;
                break;
            }
            ix--;
        }
    }

    construct_expr(context, nod, valstart, valend, 1);
    return decl;
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

static int32_t *copy_text(node *nod, int32_t *lenref)
{
    if (!nod->text || !nod->textlen) {
        /* Should report an internal error here, but there's no context. */
        return NULL;
    }

    int32_t *res = (int32_t *)malloc(sizeof(int32_t) * nod->textlen);
    if (!res)
        return NULL;

    memcpy(res, nod->text, sizeof(int32_t) * nod->textlen);
    *lenref = nod->textlen;
    return res;
}

static void dump_text(int32_t *text, int32_t len)
{
    if (!text) {
        printf("(null)");
        return;
    }

    int ix;
    for (ix=0; ix<len; ix++) {
        int32_t ch = text[ix];
        if (ch < 32)
            printf("^%c", ch+64);
        else
            mincss_putchar_utf8(ch, stdout);
    }
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
    }
    sheet->numrulegroups = 0;
    sheet->rulegroups_size = 0;

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
    if (rgrp->selectors) {
        int ix;

        for (ix=0; ix<rgrp->numselectors; ix++) 
            selector_delete(rgrp->selectors[ix]);

        free(rgrp->selectors);
        rgrp->selectors = NULL;
    }
    rgrp->numselectors = 0;
    rgrp->selectors_size = 0;

    if (rgrp->declarations) {
        int ix;

        for (ix=0; ix<rgrp->numdeclarations; ix++) 
            declaration_delete(rgrp->declarations[ix]);

        free(rgrp->declarations);
        rgrp->declarations = NULL;
    }
    rgrp->numdeclarations = 0;
    rgrp->declarations_size = 0;

    free(rgrp);
}

static void rulegroup_dump(rulegroup *rgrp, int depth)
{
    dump_indent(depth);
    printf("### rulegroup (%d selectors, %d declarations)\n", rgrp->numselectors, rgrp->numdeclarations);

    if (rgrp->selectors) {
        int ix;
        for (ix=0; ix<rgrp->numselectors; ix++) 
            selector_dump(rgrp->selectors[ix], depth+1);
    }
    if (rgrp->declarations) {
        int ix;
        for (ix=0; ix<rgrp->numdeclarations; ix++) 
            declaration_dump(rgrp->declarations[ix], depth+1);
    }
}

static int rulegroup_add_selector(rulegroup *rgrp, selector *sel)
{
    if (!rgrp->selectors) {
        rgrp->selectors_size = 4;
        rgrp->selectors = (selector **)malloc(rgrp->selectors_size * sizeof(selector *));
    }
    else if (rgrp->numselectors >= rgrp->selectors_size) {
        rgrp->selectors_size *= 2;
        rgrp->selectors = (selector **)realloc(rgrp->selectors, rgrp->selectors_size * sizeof(selector *));
    }
    if (!rgrp->selectors) {
        rgrp->numselectors = 0;
        rgrp->selectors_size = 0;
        return 0;
    }

    rgrp->selectors[rgrp->numselectors++] = sel;
    return 1;
}

static int rulegroup_add_declaration(rulegroup *rgrp, declaration *decl)
{
    if (!rgrp->declarations) {
        rgrp->declarations_size = 4;
        rgrp->declarations = (declaration **)malloc(rgrp->declarations_size * sizeof(declaration *));
    }
    else if (rgrp->numdeclarations >= rgrp->declarations_size) {
        rgrp->declarations_size *= 2;
        rgrp->declarations = (declaration **)realloc(rgrp->declarations, rgrp->declarations_size * sizeof(declaration *));
    }
    if (!rgrp->declarations) {
        rgrp->numdeclarations = 0;
        rgrp->declarations_size = 0;
        return 0;
    }

    rgrp->declarations[rgrp->numdeclarations++] = decl;
    return 1;
}

static selector *selector_new()
{
    selector *sel = (selector *)malloc(sizeof(selector));
    if (!sel)
        return NULL;

    sel->selectels = NULL;
    sel->numselectels = 0;
    sel->selectels_size = 0;

    return sel;
}

static void selector_delete(selector *sel)
{
    if (sel->selectels) {
        int ix;

        for (ix=0; ix<sel->numselectels; ix++) 
            selectel_delete(sel->selectels[ix]);

        free(sel->selectels);
        sel->selectels = NULL;
    }
    sel->numselectels = 0;
    sel->selectels_size = 0;

    free(sel);
}

static void selector_dump(selector *sel, int depth)
{
    dump_indent(depth);
    printf("Selector\n");

    if (sel->selectels) {
        int ix;
        for (ix=0; ix<sel->numselectels; ix++) 
            selectel_dump(sel->selectels[ix], depth+1);
    }
}

static int selector_add_selectel(selector *sel, selectel *ssel)
{
    if (!sel->selectels) {
        sel->selectels_size = 4;
        sel->selectels = (selectel **)malloc(sel->selectels_size * sizeof(selectel *));
    }
    else if (sel->numselectels >= sel->selectels_size) {
        sel->selectels_size *= 2;
        sel->selectels = (selectel **)realloc(sel->selectels, sel->selectels_size * sizeof(selectel *));
    }
    if (!sel->selectels) {
        sel->numselectels = 0;
        sel->selectels_size = 0;
        return 0;
    }

    sel->selectels[sel->numselectels++] = ssel;
    return 1;
}

static selectel *selectel_new()
{
    selectel *ssel = (selectel *)malloc(sizeof(selectel));
    if (!ssel)
        return NULL;

    ssel->op = op_None;
    ssel->element = NULL;
    ssel->elementlen = 0;
    ssel->classes = NULL;
    ssel->numclasses = 0;
    ssel->classes_size = 0;
    ssel->hashes = NULL;
    ssel->numhashes = 0;
    ssel->hashes_size = 0;

    return ssel;
}

static void selectel_delete(selectel *ssel)
{
    if (ssel->element) {
        free(ssel->element);
        ssel->element = NULL;
    }
    ssel->elementlen = 0;

    if (ssel->hashes) {
        int ix;

        for (ix=0; ix<ssel->numhashes; ix++) 
            ustring_delete(ssel->hashes[ix]);

        free(ssel->hashes);
        ssel->hashes = NULL;
    }
    ssel->numhashes = 0;
    ssel->hashes_size = 0;

    if (ssel->classes) {
        int ix;

        for (ix=0; ix<ssel->numclasses; ix++) 
            ustring_delete(ssel->classes[ix]);

        free(ssel->classes);
        ssel->classes = NULL;
    }
    ssel->numclasses = 0;
    ssel->classes_size = 0;

    free(ssel);
}

static void selectel_dump(selectel *ssel, int depth)
{
    dump_indent(depth);
    
    if (ssel->op) {
        printf("(%c) ", ssel->op);
    }
    printf("Selectel\n");

    if (ssel->element) {
        dump_indent(depth+1);
        printf("Element: ");
        dump_text(ssel->element, ssel->elementlen);
        printf("\n");
    }

    if (ssel->hashes) {
        int ix;
        for (ix=0; ix<ssel->numhashes; ix++) {
            dump_indent(depth+1);
            printf("Hash: ");
            dump_text(ssel->hashes[ix]->text, ssel->hashes[ix]->len);
            printf("\n");
        }
    }
    
    if (ssel->classes) {
        int ix;
        for (ix=0; ix<ssel->numclasses; ix++) {
            dump_indent(depth+1);
            printf("Class: ");
            dump_text(ssel->classes[ix]->text, ssel->classes[ix]->len);
            printf("\n");
        }
    }

}

static int selectel_add_class(selectel *ssel, ustring *ustr)
{
    if (!ssel->classes) {
        ssel->classes_size = 4;
        ssel->classes = (ustring **)malloc(ssel->classes_size * sizeof(ustring *));
    }
    else if (ssel->numclasses >= ssel->classes_size) {
        ssel->classes_size *= 2;
        ssel->classes = (ustring **)realloc(ssel->classes, ssel->classes_size * sizeof(ustring *));
    }
    if (!ssel->classes) {
        ssel->numclasses = 0;
        ssel->classes_size = 0;
        return 0;
    }

    ssel->classes[ssel->numclasses++] = ustr;
    return 1;
}

static int selectel_add_hash(selectel *ssel, ustring *ustr)
{
    if (!ssel->hashes) {
        ssel->hashes_size = 4;
        ssel->hashes = (ustring **)malloc(ssel->hashes_size * sizeof(ustring *));
    }
    else if (ssel->numhashes >= ssel->hashes_size) {
        ssel->hashes_size *= 2;
        ssel->hashes = (ustring **)realloc(ssel->hashes, ssel->hashes_size * sizeof(ustring *));
    }
    if (!ssel->hashes) {
        ssel->numhashes = 0;
        ssel->hashes_size = 0;
        return 0;
    }

    ssel->hashes[ssel->numhashes++] = ustr;
    return 1;
}

static declaration *declaration_new()
{
    declaration *decl = (declaration *)malloc(sizeof(declaration));
    if (!decl)
        return NULL;

    decl->property = NULL;
    decl->propertylen = 0;
    decl->pvalues = NULL;
    decl->numpvalues = 0;
    decl->pvalues_size = 0;
    decl->important = 0;

    return decl;
}

static void declaration_delete(declaration *decl)
{
    if (decl->property) {
        free(decl->property);
        decl->property = NULL;
    }
    decl->propertylen = 0;

    if (decl->pvalues) {
        int ix;

        for (ix=0; ix<decl->numpvalues; ix++) 
            pvalue_delete(decl->pvalues[ix]);

        free(decl->pvalues);
        decl->pvalues = NULL;
    }
    decl->numpvalues = 0;
    decl->pvalues_size = 0;

    free(decl);
}

static void declaration_dump(declaration *decl, int depth)
{
    dump_indent(depth);
    printf("### declaration: ");
    dump_text(decl->property, decl->propertylen);
    if (decl->important)
        printf(" !IMPORTANT");
    printf("\n");

    if (decl->pvalues) {
        int ix;
        for (ix=0; ix<decl->numpvalues; ix++) 
            pvalue_dump(decl->pvalues[ix], depth+1);
    }
}

static pvalue *pvalue_new()
{
    pvalue *pval = (pvalue *)malloc(sizeof(pvalue));
    if (!pval)
        return NULL;

    pval->op = op_None;

    memset(&pval->tok, 0, sizeof(pval->tok));
    pval->tok.text = NULL;

    return pval;
}

static void pvalue_delete(pvalue *pval)
{
    if (pval->tok.text) {
        free(pval->tok.text);
        pval->tok.text = NULL;
    }
    pval->tok.len = 0;
    pval->tok.typ = tok_EOF;

    free(pval);
}

static void pvalue_dump(pvalue *pval, int depth)
{
    dump_indent(depth);
    printf("### pvalue\n");
}

static ustring *ustring_new(void)
{
    ustring *ustr = (ustring *)malloc(sizeof(ustring));
    if (!ustr)
        return NULL;

    ustr->text = NULL;
    ustr->len = 0;

    return ustr;
}

static ustring *ustring_new_from_node(node *nod)
{
    ustring *ustr = ustring_new();
    if (!ustr)
        return NULL;

    ustr->text = copy_text(nod, &ustr->len);
    if (!ustr->text) {
        ustring_delete(ustr);
        return NULL;
    }

    return ustr;
}

static void ustring_delete(ustring *ustr)
{
    if (ustr->text) {
        free(ustr->text);
        ustr->text = NULL;
    }

    free(ustr);
}
