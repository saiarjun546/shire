/*
 * main.c - Shire Shell entry point
 *
 *   shire            interactive (or reads commands from stdin)
 *   shire -q         same, without the start-up banner
 *   shire -c "cmd"   run one command line and exit with its status
 *   shire --help | --version
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"

static void usage(const char *prog)
{
    printf("Usage: %s [-q] [-c command] | --help | --version\n"
           "  -q           do not print the start-up banner\n"
           "  -c command   run one command line, then exit with its status\n", prog);
}

int main(int argc, char *argv[])
{
    int quiet = 0;
    const char *command = NULL;

    setvbuf(stdout, NULL, _IOLBF, 0);   /* line-buffered even when piped */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("Shire Shell %s\n", SHIRE_VERSION);
            return 0;
        } else {
            fprintf(stderr, "shire: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    shell_state sh;
    shell_init(&sh);
    sh.interactive = isatty(STDIN_FILENO);

    int rc;
    if (command != NULL) {
        shell_execute(&sh, command);
        rc = sh.running ? sh.last_status : sh.exit_code;
    } else {
        if (!quiet)
            shell_banner(stdout);
        rc = shell_run(&sh, stdin);
    }
    shell_free(&sh);
    return rc;
}
