#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mincss.h"
#include "cssint.h"

static int parse_number(mincss_context *context);
static int parse_string(mincss_context *context, int32_t delim);
static int parse_ident(mincss_context *context, int gotstart);
static int parse_uri_body(mincss_context *context);
static int parse_universal_newline(mincss_context *context);
static int parse_escaped_hex(mincss_context *context, int32_t *val);

static int32_t next_char(mincss_context *context);
static void putback_char(mincss_context *context, int count);
static void erase_char(mincss_context *context, int count);
static int match_accepted_chars(mincss_context *context, char *str);

char *mincss_token_name(tokentype tok)
{
    switch (tok) {
    case tok_EOF: return "EOF";
    case tok_Delim: return "Delim";
    case tok_Space: return "Space";
    case tok_Comment: return "Comment";
    case tok_Number: return "Number";
    case tok_String: return "String";
    case tok_Ident: return "Ident";
    case tok_AtKeyword: return "AtKeyword";
    case tok_Percentage: return "Percentage";
    case tok_Dimension: return "Dimension";
    case tok_Function: return "Function";
    case tok_Hash: return "Hash";
    case tok_URI: return "URI";
    case tok_LBrace: return "LBrace";
    case tok_RBrace: return "RBrace";
    case tok_LBracket: return "LBracket";
    case tok_RBracket: return "RBracket";
    case tok_LParen: return "LParen";
    case tok_RParen: return "RParen";
    case tok_Colon: return "Colon";
    case tok_Semicolon: return "Semicolon";
    case tok_Includes: return "Includes";
    case tok_DashMatch: return "DashMatch";
    case tok_CDO: return "CDO";
    case tok_CDC: return "CDC";
    default: return "???";
    }
}

/* Some macro tests which can be applied to (unicode) characters. */

#define IS_WHITESPACE(ch) ((ch) == ' ' || (ch) == '\t' || (ch) == '\r' || (ch) == '\n' || (ch) == '\f')
#define IS_NUMBER_START(ch) (((ch) >= '0' && (ch) <= '9') || ((ch) == '.'))
#define IS_HEX_DIGIT(ch) (((ch) >= '0' && (ch) <= '9') || ((ch) >= 'a' && (ch) <= 'f') || ((ch) >= 'A' && (ch) <= 'F'))
#define IS_IDENT_START(ch) (((ch) >= 'A' && (ch) <= 'Z') || ((ch) >= 'a' && (ch) <= 'z') || (ch) == '_' || (ch >= 0xA0))

/* Grab the next token. Returns the tokentype. The token's text is available
   at context->token, length context->tokenlen.
*/
tokentype mincss_next_token(mincss_context *context)
{
    /* Discard all text in the buffer from the previous token. But if
       any characters were pushed back, keep those. */
    if (context->tokenlen) {
        int extra = context->tokenmark - context->tokenlen;
        if (extra > 0) {
            memmove(context->token, context->token+context->tokenlen, extra*sizeof(int32_t));
        }
        context->tokenlen = 0;
        context->tokenmark = extra;
    }

    int32_t ch = next_char(context);
    if (ch == -1) {
        return tok_EOF;
    }

    /* Simple one-character tokens. */
    switch (ch) {
    case '(':
        return tok_LParen;
    case ')':
        return tok_RParen;
    case '[':
        return tok_LBracket;
    case ']':
        return tok_RBracket;
    case '{':
        return tok_LBrace;
    case '}':
        return tok_RBrace;
    case ':':
        return tok_Colon;
    case ';':
        return tok_Semicolon;

    /* Some cases that are more than one character, but still easy to take care of. */

    case '~': {
        ch = next_char(context);
        if (ch == -1) 
            return tok_Delim;
        if (ch == '=')
            return tok_Includes;
        putback_char(context, 1);
        return tok_Delim;
    }

    case '|': {
        ch = next_char(context);
        if (ch == -1) 
            return tok_Delim;
        if (ch == '=')
            return tok_DashMatch;
        putback_char(context, 1);
        return tok_Delim;
    }

    case '@': {
        int len = parse_ident(context, 0);
        if (len == 0) 
            return tok_Delim;
        return tok_AtKeyword;
    }

    case '#': {
        int len = parse_ident(context, 1);
        if (len == 1) 
            return tok_Delim;
        return tok_Hash;
    }

    /* Not proud of this next one. */
    case '<': {
        ch = next_char(context);
        if (ch == -1) 
            return tok_Delim;
        if (ch == '!') {
            ch = next_char(context);
            if (ch == -1) {
                putback_char(context, 1);
                return tok_Delim;
            }
            if (ch == '-') {
                ch = next_char(context);
                if (ch == -1) {
                    putback_char(context, 2);
                    return tok_Delim;
                }
                if (ch == '-') {
                    return tok_CDO;
                }
                putback_char(context, 3);
                return tok_Delim;
            }
            putback_char(context, 2);
            return tok_Delim;
        }
        putback_char(context, 1);
        return tok_Delim;
    }
    }

    if (IS_WHITESPACE(ch)) {
        while (1) {
            ch = next_char(context);
            if (ch == -1) 
                return tok_Space;
            if (!IS_WHITESPACE(ch)) {
                putback_char(context, 1);
                return tok_Space;
            }
            continue;
        }
    }

    if (ch == '"' || ch == '\'') {
        /* Strings begin with a single or double quote. */
        parse_string(context, ch);
        return tok_String;
    }

    if (IS_NUMBER_START(ch)) {
        /* Digits could begin a number, percentage, or dimension, depending
           on what's after them. */
        putback_char(context, 1);
        int numlen = parse_number(context);
        if (numlen == 0) {
            ch = next_char(context);
            return tok_Delim;
        }
        ch = next_char(context);
        if (ch == -1)
            return tok_Number;
        if (ch == '%')
            return tok_Percentage;
        if (ch == '-' || IS_IDENT_START(ch)) {
            putback_char(context, 1);
            int len = parse_ident(context, 0);
            if (len > 0)
                return tok_Dimension;
            else
                return tok_Number;
        }
        putback_char(context, 1);
        return tok_Number;
    }

    if (ch == '-' || IS_IDENT_START(ch)) {
        /* Ordinary identifiers. Note that minus signs always indicate
           identifiers, not numbers. (At least in CSS 2.1.) (Except
           that it might be a CDC --> token.) */

        ch = next_char(context);
        if (ch == -1) {
            /* Do nothing */
        }
        else if (ch == '-') {
            ch = next_char(context);
            if (ch == -1) {
                putback_char(context, 1);
            }
            else if (ch == '>') {
                return tok_CDC;
            }
            else {
                putback_char(context, 2);
            }
        }
        else {
            putback_char(context, 1);
        }

        putback_char(context, 1);
        int len = parse_ident(context, 0);
        if (len == 0) {
            ch = next_char(context);
            return tok_Delim;
        }
        if (len == 3 && match_accepted_chars(context, "url")) {
            int sublen = parse_uri_body(context);
            if (sublen > 0)
                return tok_URI;
        }
        /* If the following character is a left-paren, this is a function. */
        ch = next_char(context);
        if (ch == -1) 
            return tok_Ident;
        if (ch == '(')
            return tok_Function;
        putback_char(context, 1);
        return tok_Ident;
    }

    if (ch == '/') {
        ch = next_char(context);
        if (ch == -1) 
            return tok_Delim;
        if (ch != '*') {
            putback_char(context, 1);
            return tok_Delim;
        }
        int gotstar = 0;
        while (1) {
            ch = next_char(context);
            if (ch == -1) {
                mincss_note_error(context, "Unterminated comment");
                return tok_Comment;
            }
            if (ch == '/' && gotstar)
                return tok_Comment;
            gotstar = (ch == '*');
        }
    }

    if (ch == '\\') {
        /* A backslash which forms a hex escape is the start of an
           identifier. (Even if it's not a normal identifier-start
           character.) A backslashed nonwhite character starts an
           identifer as itself. A backslash before whitespace is
           a delimiter. */
        int len = parse_universal_newline(context);
        if (len) {
            /* Backslashed newline: put back the newline, accept the
               backslash. */
            putback_char(context, len);
            return tok_Delim;
        }
        int32_t val = '?';
        len = parse_escaped_hex(context, &val);
        if (len) {
            /* Backslashed hex: drop the hex string... */
            erase_char(context, len);
            /* Replace the backslash itself with the named character. */
            context->token[context->tokenlen-1] = val;
            /* Parse the rest of the identifier. */
            parse_ident(context, 1);
            /* If the following character is a left-paren, this is a function. */
            ch = next_char(context);
            if (ch == -1) 
                return tok_Ident;
            if (ch == '(')
                return tok_Function;
            putback_char(context, 1);
            return tok_Ident;
        }
        ch = next_char(context);
        if (ch == -1) {
            /* If there is no next character, take the backslash as a
               delimiter. */
            return tok_Delim;
        }
        /* Any other character: take the next char literally
           (substitute it for the backslash). */
        erase_char(context, 1);
        context->token[context->tokenlen-1] = ch;
        /* Parse the rest of the identifier. */
        parse_ident(context, 1);
        /* If the following character is a left-paren, this is a function. */
        ch = next_char(context);
        if (ch == -1) 
            return tok_Ident;
        if (ch == '(')
            return tok_Function;
        putback_char(context, 1);
        return tok_Ident;
    }

    /* Anything not captured above is a one-character Delim token. */
    return tok_Delim;
}

/* Parse a number (integer or decimal, no minus sign). 
   Return the number of characters parsed. If the incoming text is not
   a number, push it back and return 0.
*/
static int parse_number(mincss_context *context)
{
    int count = 0;
    int dotpos = -1;

    int32_t ch = next_char(context);
    if (ch == -1)
        return 0;
    count++;

    if (!IS_NUMBER_START(ch)) {
        putback_char(context, count);
        return 0;
    }

    if (ch == '.')
        dotpos = 0;

    while (1) {
        ch = next_char(context);
        if (ch == -1) {
            if (dotpos == 0 && count == 1) {
                putback_char(context, count);
                return 0;
            }
            if (dotpos >= 0 && dotpos == count-1) {
                putback_char(context, 1);
                return count-1;
            }
            return count;
        }
        count++;

        if (ch == '.') {
            if (dotpos >= 0) {
                if (dotpos == 0 && count == 2) {
                    putback_char(context, count);
                    return 0;
                }
                if (dotpos == count-2) {
                    putback_char(context, 2);
                    return count-2;
                }
                putback_char(context, 1);
                return count-1;
            }
            dotpos = count-1;
            continue;
        }
        if (!(ch >= '0' && ch <= '9')) {
            if (dotpos == 0 && count == 2) {
                putback_char(context, count);
                return 0;
            }
            if (dotpos >= 0 && dotpos == count-2) {
                putback_char(context, 2);
                return count-2;
            }
            putback_char(context, 1);
            return count-1;
        }
        /* digit */
        continue;
    }
}

/* Parse a string. (Assume the leading quote has already been accepted.)
   Return the number of characters parsed. If the incoming text is not
   a valid string, push it back and return 0. 
   (But if we run into an unescaped newline, report an error and return
   the string so far, no pushback.)
*/
static int parse_string(mincss_context *context, int32_t delim)
{
    int count = 0;

    while (1) {
        int32_t ch = next_char(context);
        if (ch == -1) {
            mincss_note_error(context, "Unterminated string");
            return count;
        }
        count++;

        if (ch == delim)
            return count;

        if (ch == '\\') {
            int len = parse_universal_newline(context);
            if (len) {
                /* Backslashed newline: drop it. */
                erase_char(context, len+1);
                count -= 1;
                continue;
            }
            int32_t val = '?';
            len = parse_escaped_hex(context, &val);
            if (len) {
                /* Backslashed hex: drop the hex string... */
                erase_char(context, len);
                /* Replace the backslash itself with the named character. */
                context->token[context->tokenlen-1] = val;
                continue;
            }
            /* Any other character: take the next char literally
               (substitute it for the backslash). */
            ch = next_char(context);
            if (ch == -1) {
                mincss_note_error(context, "Unterminated string (ends with backslash)");
                return count;
            }
            erase_char(context, 1);
            context->token[context->tokenlen-1] = ch;
            continue;
        }

        /* If a string runs into an unescaped newline, we report an error
           and pretend the string ended. */
        if (ch == '\n' || ch == '\r' || ch == '\f') {
            mincss_note_error(context, "Unterminated string");
            return count;
        }
    }
}

/* Parse an identifier.
   Return the number of characters parsed. If the incoming text is not
   an identifier, push it back and return 0.
   If gotstart is false, the initial character must be read. If true,
   it's already accepted.
   (This is also used to parse #hash tokens. In that case, gotstart is
   true, but the initial character is the hash.)
*/
static int parse_ident(mincss_context *context, int gotstart)
{
    int count = 0;
    int32_t ch = 0;

    if (!gotstart) {
        ch = next_char(context);
        if (ch == -1)
            return 0;
        count++;

        /* We can start with a minus, but only if the following character
           is a legit ident-start character *or* an escape. */
        if (ch == '-') {
            ch = next_char(context);
            if (ch == -1) {
                putback_char(context, count);
                return 0;
            }
            count++;
        }
        
        if (ch == '\\') {
            int len = parse_universal_newline(context);
            if (len) {
                /* Backslashed newline: put back both, exit. */
                putback_char(context, 1+len);
                return count-(1+len);
            }
            int32_t val = '?';
            len = parse_escaped_hex(context, &val);
            if (len) {
                /* Backslashed hex: drop the hex string... */
                erase_char(context, len);
                /* Replace the backslash itself with the named character. */
                context->token[context->tokenlen-1] = val;
            }
            else {
                ch = next_char(context);
                if (ch == -1) {
                    /* If there is no next character, put the backslash back
                       and exit. */
                    putback_char(context, 1);
                    return count-1;
                }
                /* Any other character: take the next char literally
                   (substitute it for the backslash). */
                erase_char(context, 1);
                context->token[context->tokenlen-1] = ch;
            }
        }
        else {
            /* Note that Unicode characters from 0xA0 on can *all* be used in
               identifiers. IS_IDENT_START includes these. */
            if (!IS_IDENT_START(ch)) {
                putback_char(context, count);
                return 0;
            }
        }
    }
    else {
        count = 1;
    }

    while (1) {
        ch = next_char(context);
        if (ch == -1) 
            return count;
        count++;

        if (ch == '\\') {
            int len = parse_universal_newline(context);
            if (len) {
                /* Backslashed newline: put back both, exit. */
                putback_char(context, 1+len);
                return count-(1+len);
            }
            int32_t val = '?';
            len = parse_escaped_hex(context, &val);
            if (len) {
                /* Backslashed hex: drop the hex string... */
                erase_char(context, len);
                /* Replace the backslash itself with the named character. */
                context->token[context->tokenlen-1] = val;
                continue;
            }
            ch = next_char(context);
            if (ch == -1) {
                /* If there is no next character, put the backslash back
                   and exit. */
                putback_char(context, 1);
                return count-1;
            }
            /* Any other character: take the next char literally
               (substitute it for the backslash). */
            erase_char(context, 1);
            context->token[context->tokenlen-1] = ch;
            continue;
        }

        if (!(IS_IDENT_START(ch) || (ch == '-') || (ch >= '0' && ch <= '9'))) {
            putback_char(context, 1);
            return count-1;
        }
        continue;
    }
}

/* Parse a URI. (Assume the leading "url" has already been accepted.)
   Return the number of characters parsed. If the incoming text is not
   a valid URI, push it back and return 0. 
*/
static int parse_uri_body(mincss_context *context)
{
    int count = 0;

    int32_t ch = next_char(context);
    if (ch == -1)
        return 0;
    count++;

    if (ch != '(') {
        putback_char(context, 1);
        return 0;
    }

    while (1) {
        ch = next_char(context);
        if (ch == -1) {
            putback_char(context, count);
            return 0;
        }
        count++;
        if (IS_WHITESPACE(ch))
            continue;
        break;
    }

    if (ch < ' ' || ch == '(' || ch == ')' || (ch > '~' && ch < 0xA0 && ch != '\\')) {
        /* Invalid characters for a URL body. */
        putback_char(context, count);
        return 0;
    }

    if (ch == '"' || ch == '\'') {
        /* The quoted case. */
        int len = parse_string(context, ch);
        if (!len) {
            putback_char(context, count);
            return 0;
        }
        count += len;
    }
    else {
        /* The unquoted case. We put back the initial char in case it was a backslash. */
        putback_char(context, 1);
        count -= 1;
        while (1) {
            ch = next_char(context);
            if (ch == -1) {
                putback_char(context, count);
                return 0;
            }
            count++;
            if (ch == '\\') {
                int len = parse_universal_newline(context);
                if (len) {
                    /* Backslashed newline: drop it. */
                    erase_char(context, len+1);
                    count -= 1;
                    continue;
                }
                int32_t val = '?';
                len = parse_escaped_hex(context, &val);
                if (len) {
                    /* Backslashed hex: drop the hex string... */
                    erase_char(context, len);
                    /* Replace the backslash itself with the named character. */
                    context->token[context->tokenlen-1] = val;
                    continue;
                }
                /* Any other character: take the next char literally
                   (substitute it for the backslash). */
                ch = next_char(context);
                if (ch == -1) {
                    mincss_note_error(context, "Unterminated URI (ends with backslash)");
                    return count;
                }
                erase_char(context, 1);
                context->token[context->tokenlen-1] = ch;
                continue;
            }
            if (ch < ' ' || ch == '"' || ch == '\'' || ch == '(' || ch == ')' || ch == '\\' || (ch > '~' && ch < 0xA0)) {
                putback_char(context, 1);
                count -= 1;
                break;
            }
            continue;
        }

    }

    /* Chew up trailing whitespace and the close-paren. */
    while (1) {
        ch = next_char(context);
        if (ch == -1) {
            putback_char(context, count);
            return 0;
        }
        count++;
        if (IS_WHITESPACE(ch))
            continue;
        if (ch == ')')
            break;
        putback_char(context, count);
        return 0;
    }
    
    return count;    
}

/* Parse a single newline of the types that may occur in a text file:
   \n, \r\n, \r, \f. (In a string, a backslash followed by one of these
   is discarded.)
*/
static int parse_universal_newline(mincss_context *context)
{
    int count = 0;

    int32_t ch = next_char(context);
    if (ch == -1)
        return 0;
    count++;

    if (ch == '\n' || ch == '\f')
        return count;

    if (ch == '\r') {
        ch = next_char(context);
        if (ch == -1)
            return count;
        count++;
        if (ch == '\n')
            return count;
        putback_char(context, 1);
        return count-1;
    }

    putback_char(context, count);
    return 0;
}

/* Parse one to six hex digits, optionally followed by a single
   whitespace character. (In a string, a backslash followed by this is
   interpreted as a hex escape.)
*/
static int parse_escaped_hex(mincss_context *context, int32_t *retval)
{
    /* The backslash has already been accepted. */
    int count = 0;
    int32_t res = 0;
    int32_t ch = -1;

    while (1) {
        ch = next_char(context);
        if (ch == -1) {
            if (count)
                *retval = res;
            return count;
        }
        count++;
        if (count > 6)
            break;

        if (!IS_HEX_DIGIT(ch))
            break;
        if (ch >= '0' && ch <= '9')
            ch -= '0';
        else if (ch >= 'A' && ch <= 'F')
            ch -= ('A'-10);
        else if (ch >= 'a' && ch <= 'f')
            ch -= ('a'-10);
        else
            ch = 0;
        res = (res << 4) + ch;
    }

    if (ch == '\r' && count >= 2) {
        /* swallow the \r, plus an \n if one follows */
        ch = next_char(context);
        if (ch == -1) {
            *retval = res;
            return count;
        }
        count++;
        if (ch == '\n') {
            *retval = res;
            return count;
        }
        putback_char(context, 1);
        count -= 1;
        *retval = res;
        return count;
    }

    if (IS_WHITESPACE(ch) && count >= 2) {
        /* swallow it */
        *retval = res;
        return count;
    }

    putback_char(context, 1);
    count -= 1;
    if (count)
        *retval = res;
    return count;
}

/* Accept a new character into the token. If there are pushed-back
   characters, take the next one. If not, pluck a new one from the
   reader function. If no more characters are available, return -1.

   This advances tokenlen (and tokenmark, if the reader function is
   called). However, in the -1 case, neither tokenlen and tokenmark changes.
   (When we're at the end of the stream, you can call next_char() forever
   and keep getting -1 back but the state will not change.)

   Most of the ugliness in this function is UTF-8 parsing (for the
   mincss_parse_bytes_utf8() case).   
*/
static int32_t next_char(mincss_context *context)
{
    int32_t ch;

    if (!context->token)
        return -1;

    if (context->tokenlen < context->tokenmark) {
	/* Pop a put-back character. */
        ch = context->token[context->tokenlen];
        context->tokenlen++;
        return ch;
    }

    if (context->tokenlen >= context->tokenbufsize) {
        context->tokenbufsize = 2*context->tokenlen + 16;
        context->token = (int32_t *)realloc(context->token, context->tokenbufsize * sizeof(int32_t));
        if (!context->token) {
            mincss_note_error(context, "(Internal) Unable to reallocate buffer memory");
            return -1;
        }
    }

    /* Read a unichar from the input source. (If the input source is bytes,
       this is ugly UTF8 decoding.) */

    if (context->parse_byte) {
        int32_t byte0 = (context->parse_byte)(context->parserock);
        if (byte0 < 0) {
            ch = -1;
        }
        else if (byte0 < 0x80) {
            ch = byte0;
        }
        else if ((byte0 & 0xE0) == 0xC0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                mincss_note_error(context, "(UTF8) Incomplete two-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                mincss_note_error(context, "(UTF8) Malformed two-byte character");
                ch = byte0;
            }
            else {
                ch = (byte0 & 0x1F) << 6;
                ch |= (byte1 & 0x3F);
            }
        }
        else if ((byte0 & 0xF0) == 0xE0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                mincss_note_error(context, "(UTF8) Incomplete three-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                mincss_note_error(context, "(UTF8) Malformed three-byte character");
                ch = byte0;
            }
            else {
                int32_t byte2 = (context->parse_byte)(context->parserock);
                if (byte2 < 0) {
                    mincss_note_error(context, "(UTF8) Incomplete three-byte character");
                    ch = byte0;
                }
                else if ((byte2 & 0xC0) != 0x80) {
                    mincss_note_error(context, "(UTF8) Malformed three-byte character");
                    ch = byte0;
                }
                else {
                    ch = (((byte0 & 0x0F)<<12)  & 0x0000F000);
                    ch |= (((byte1 & 0x3F)<<6) & 0x00000FC0);
                    ch |= (((byte2 & 0x3F))    & 0x0000003F);
                }
            }
        }
        else if ((byte0 & 0xF0) == 0xF0) {
            int32_t byte1 = (context->parse_byte)(context->parserock);
            if (byte1 < 0) {
                mincss_note_error(context, "(UTF8) Incomplete four-byte character");
                ch = byte0;
            }
            else if ((byte1 & 0xC0) != 0x80) {
                mincss_note_error(context, "(UTF8) Malformed four-byte character");
                ch = byte0;
            }
            else {
                int32_t byte2 = (context->parse_byte)(context->parserock);
                if (byte2 < 0) {
                    mincss_note_error(context, "(UTF8) Incomplete four-byte character");
                    ch = byte0;
                }
                else if ((byte2 & 0xC0) != 0x80) {
                    mincss_note_error(context, "(UTF8) Malformed four-byte character");
                    ch = byte0;
                }
                else {
                    int32_t byte3 = (context->parse_byte)(context->parserock);
                    if (byte3 < 0) {
                        mincss_note_error(context, "(UTF8) Incomplete four-byte character");
                        ch = byte0;
                    }
                    else if ((byte3 & 0xC0) != 0x80) {
                        mincss_note_error(context, "(UTF8) Malformed four-byte character");
                        ch = byte0;
                    }
                    else {
                        ch = (((byte0 & 0x07)<<18)   & 0x1C0000);
                        ch |= (((byte1 & 0x3F)<<12) & 0x03F000);
                        ch |= (((byte2 & 0x3F)<<6)  & 0x000FC0);
                        ch |= (((byte3 & 0x3F))     & 0x00003F);
                    }
                }
            }
        }
        else {
            mincss_note_error(context, "(UTF8) Malformed character");
            ch = '?';
        }
    }
    else {
        ch = (context->parse_unicode)(context->parserock);
    }
    if (ch == -1)
        return -1;

    /* This isn't smart about DOS line breaks. */
    if (ch == '\n' || ch == '\r')
	context->linenum += 1;

    context->token[context->tokenlen] = ch;
    context->tokenlen += 1;
    context->tokenmark = context->tokenlen;
    return ch;
}

/* Push back some characters in the buffer -- reject them from the current
   token. (This decreases tokenlen without changing tokenmark.)
*/
static void putback_char(mincss_context *context, int count)
{
    if (count > context->tokenlen) {
        mincss_note_error(context, "(Internal) Put back too many characters");
        context->tokenlen = 0;
        return;
    }

    context->tokenlen -= count;
}

/* Remove some characters from the end of the current token.
   (Pushed-back characters are not affected. This moves both tokenlen
   and tokenmark back.)
*/
static void erase_char(mincss_context *context, int count)
{
    if (count > context->tokenlen) {
        mincss_note_error(context, "(Internal) Erase too many characters");
        return;
    }

    int diff = context->tokenmark - context->tokenlen;
    if (diff > 0)
        memmove(context->token+(context->tokenlen - count), context->token+context->tokenlen, diff*sizeof(int32_t));
    context->tokenmark -= count;
    context->tokenlen -= count;
}

/* Compare the tail of the current token against a given (ASCII) string,
   case-insensitively. Returns whether they match.
*/
static int match_accepted_chars(mincss_context *context, char *str)
{
    int ix;
    int len = strlen(str);
    if (len > context->tokenlen)
        return 0;

    for (ix=0; ix<len; ix++) {
        int32_t ch = (unsigned char)(str[ix]);
        int32_t ch2 = ch;
        if (ch >= 'a' && ch <= 'z')
            ch2 -= ('a' - 'A');
        else if (ch >= 'A' && ch <= 'Z')
            ch2 += ('a' - 'A');
        int32_t val = context->token[context->tokenlen - len + ix];
        if (val != ch && val != ch2)
            return 0;
    }

    return 1;
}
