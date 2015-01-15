typedef enum tokentype_enum {
    tok_EOF = 0,
    tok_Delim = 1,
    tok_Space = 2,
    tok_Comment = 3,
    tok_Number = 4,
    tok_String = 5,
    tok_Ident = 6,
    tok_AtKeyword = 7,
    tok_Percentage = 8,
    tok_Dimension = 9,
    tok_Function = 10,
    tok_Hash = 11,
    tok_URI = 12,
    tok_LBrace = 13,
    tok_RBrace = 14,
    tok_LBracket = 15,
    tok_RBracket = 16,
    tok_LParen = 17,
    tok_RParen = 18,
    tok_Colon = 19,
    tok_Semicolon = 20,
    tok_Includes = 21,
    tok_DashMatch = 22,
    tok_CDO = 23,
    tok_CDC = 24,
} tokentype;

typedef struct token_struct {
    tokentype typ;
    int32_t *text;
    int len;
    int div;
} token;

struct mincss_context_struct {
    int errorcount;

    /* These fields are only valid during a mincss_parse_bytes_utf8()
       or mincss_parse_unicode() call. */
    void *parserock;
    mincss_unicode_reader parse_unicode;
    mincss_byte_reader parse_byte;
    mincss_error_handler parse_error;

    /* Print debug output and stop at a given stage. */
    int debug_trace;

    /* The lexer maintains a buffer of Unicode characters.
       tokenbufsize is the available malloced size of the buffer.
       tokenmark is the number of characters currently in the buffer.
       tokenlen is the number of characters accepted into the current token.
       (tokenmark always >= tokenlen. tokenmark will be greater than tokenlen
       if some characters have been pushed back -- that is, not accepted
       in the current token, available for the next token.)
    */
    int32_t *token;
    int tokenbufsize;
    int tokenmark;
    int tokenlen;
    /* tokendiv is a marked position within the token, between 0 and
       tokenlen. This is used for the Dimension token. */
    int tokendiv;

    int linenum; /* for error messages */

    /* The reader condenses all of the above info into a smaller structure.
       This is a bit redundant (the div value is just copied down from
       tokendiv) but it's tidier to have it all in one package. */
    token nexttok;
};

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

typedef struct stylesheet_struct stylesheet;

/* mincss.c */
#define mincss_note_error(context, msg) mincss_note_error_line(context, msg, -1)
extern void mincss_note_error_line(mincss_context *context, char *msg, int linenum);
extern void mincss_putchar_utf8(int32_t val, FILE *fl);

/* csslex.c */
extern tokentype mincss_next_token(mincss_context *context);
extern char *mincss_token_name(tokentype tok);

/* cssread.c */
extern void mincss_read(mincss_context *context);
extern void mincss_dump_node(node *nod, int depth);
extern void mincss_dump_node_range(char *label, node *nod, int start, int end);

/* csscons.c */
extern void mincss_construct_stylesheet(mincss_context *context, node *nod);
extern void mincss_stylesheet_dump(stylesheet *sheet);

