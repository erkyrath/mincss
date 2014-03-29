#include <stdio.h>
#include "mincss.h"

static int read_stdin_byte(void *rock);

int main(int argc, char *argv[])
{
    mincss_context *context = mincss_init();

    /*### bytes really */
    mincss_parse_unicode(context, read_stdin_byte, NULL, NULL);

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

