#include <stdio.h>
#include <string.h>
#include "mincss.h"

static int read_stdin_byte(void *rock);

int main(int argc, char *argv[])
{
    int ix;
    int debug_trace = MINCSS_TRACE_OFF;

    for (ix=1; ix<argc; ix++) {
        if (!strcmp(argv[ix], "-l")
            || !strcmp(argv[ix], "--lexer"))
            debug_trace = MINCSS_TRACE_LEXER;
        if (!strcmp(argv[ix], "-t")
            || !strcmp(argv[ix], "--tree"))
            debug_trace = MINCSS_TRACE_TREE;
    }

    mincss_context *context = mincss_init();
    mincss_set_debug_trace(context, debug_trace);

    mincss_parse_bytes_utf8(context, read_stdin_byte, NULL, NULL);

    mincss_final(context);

    return 0;
}

static int read_stdin_byte(void *rock)
{
    int ch = fgetc(stdin);
    if (ch == EOF)
        return -1;
    return ch;
}

