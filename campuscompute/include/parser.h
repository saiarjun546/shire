/*
 * parser.h - Shire Shell: command-line tokenizer and number parsing
 *
 * Owner: Person 1
 *
 * Tokenizing rules (a deliberately small, safe subset of POSIX sh quoting):
 *   - Tokens are separated by spaces and tabs.
 *   - 'single quotes'  keep every character literally.
 *   - "double quotes"  keep characters literally except \" and \\ .
 *   - \x outside quotes makes x literal (e.g. a\ b is one token "a b").
 *   - An unquoted # at the start of a token begins a comment.
 *   - "" and '' produce an empty argument.
 * NOT supported (documented limitations): pipes, redirection, globbing,
 * variable expansion, command substitution, background '&', ';' lists.
 * Those characters are passed through to the command as ordinary text.
 *
 * The parser never invokes a shell and never builds command strings; the
 * resulting argv is handed straight to execvp().
 */
#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#define PARSE_MAX_LINE 1024     /* longest accepted input line (bytes)    */
#define PARSE_MAX_ARGS 64       /* most arguments in one command          */

typedef struct {
    char **argv;                /* NULL-terminated, heap-allocated        */
    int    argc;
} parsed_cmd;

typedef enum {
    PARSE_OK = 0,
    PARSE_EMPTY,                /* blank line or only a comment           */
    PARSE_ERR_QUOTE,            /* unterminated ' or "                    */
    PARSE_ERR_ESCAPE,           /* line ends with a lone backslash        */
    PARSE_ERR_TOO_MANY,         /* more than PARSE_MAX_ARGS arguments     */
    PARSE_ERR_NOMEM
} parse_status;

/* Split `line` into arguments. On PARSE_OK the caller must call
 * parsed_cmd_free(). On any other status `out` is empty (nothing to free,
 * but freeing is harmless). */
parse_status parse_line(const char *line, parsed_cmd *out);
void parsed_cmd_free(parsed_cmd *cmd);
const char *parse_status_str(parse_status st);

/* Strict integer parsing: the whole string must be a decimal number in
 * [lo, hi] (optional leading +/-, no spaces, no trailing text).
 * Returns 0 and stores the value, or -1 (value untouched). */
int parse_int_range(const char *s, long lo, long hi, long *out);

#endif /* PARSER_H */
