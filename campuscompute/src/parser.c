/*
 * parser.c - Shire Shell tokenizer (rules in parser.h)
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

const char *parse_status_str(parse_status st)
{
    switch (st) {
    case PARSE_OK:           return "ok";
    case PARSE_EMPTY:        return "empty command";
    case PARSE_ERR_QUOTE:    return "unterminated quote";
    case PARSE_ERR_ESCAPE:   return "line ends with a dangling backslash";
    case PARSE_ERR_TOO_MANY: return "too many arguments (maximum 64)";
    case PARSE_ERR_NOMEM:    return "out of memory";
    }
    return "unknown parse error";
}

void parsed_cmd_free(parsed_cmd *cmd)
{
    if (cmd == NULL)
        return;
    if (cmd->argv != NULL)
        for (int i = 0; i < cmd->argc; i++)
            free(cmd->argv[i]);
    free(cmd->argv);
    cmd->argv = NULL;
    cmd->argc = 0;
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t';
}

parse_status parse_line(const char *line, parsed_cmd *out)
{
    out->argv = NULL;
    out->argc = 0;

    out->argv = calloc(PARSE_MAX_ARGS + 1, sizeof *out->argv);
    if (out->argv == NULL)
        return PARSE_ERR_NOMEM;

    size_t len = strlen(line);
    const char *p = line;
    parse_status st = PARSE_OK;

    for (;;) {
        while (is_blank(*p))
            p++;
        if (*p == '\0' || *p == '#')            /* end of line / comment */
            break;
        if (out->argc >= PARSE_MAX_ARGS) {
            st = PARSE_ERR_TOO_MANY;
            break;
        }

        /* a token can never be longer than the whole line */
        char *tok = malloc(len + 1);
        if (tok == NULL) {
            st = PARSE_ERR_NOMEM;
            break;
        }
        size_t t = 0;

        while (*p != '\0' && !is_blank(*p)) {
            if (*p == '\'') {
                p++;
                while (*p != '\0' && *p != '\'')
                    tok[t++] = *p++;
                if (*p == '\0') {
                    st = PARSE_ERR_QUOTE;
                    break;
                }
                p++;                            /* closing quote */
            } else if (*p == '"') {
                p++;
                while (*p != '\0' && *p != '"') {
                    if (*p == '\\' && (p[1] == '"' || p[1] == '\\'))
                        p++;
                    tok[t++] = *p++;
                }
                if (*p == '\0') {
                    st = PARSE_ERR_QUOTE;
                    break;
                }
                p++;
            } else if (*p == '\\') {
                if (p[1] == '\0') {
                    st = PARSE_ERR_ESCAPE;
                    break;
                }
                tok[t++] = p[1];
                p += 2;
            } else {
                tok[t++] = *p++;
            }
        }
        if (st != PARSE_OK) {
            free(tok);
            break;
        }
        tok[t] = '\0';
        out->argv[out->argc++] = tok;
    }

    if (st == PARSE_OK && out->argc == 0)
        st = PARSE_EMPTY;
    if (st != PARSE_OK)
        parsed_cmd_free(out);
    return st;
}

int parse_int_range(const char *s, long lo, long hi, long *out)
{
    if (s == NULL || *s == '\0' || is_blank(*s))
        return -1;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}
