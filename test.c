#include <stdio.h>
#include <string.h>
#include "mincss.h"

static int read_stdin_byte(void *rock);

int main(int argc, char *argv[])
{
    int ix;
    int lexer_debug = 0;

    for (ix=1; ix<argc; ix++) {
        if (!strcmp(argv[ix], "-l")
            || !strcmp(argv[ix], "--lexer"))
            lexer_debug = 1;
    }

    mincss_context *context = mincss_init();
    mincss_set_lexer_debug(context, lexer_debug);

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

