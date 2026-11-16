/*
 * shell.c - Shire Shell core: input, dispatch and external commands
 *
 * Design notes
 *  - Input is read with fgets() into a fixed buffer; over-long lines are
 *    rejected (and the rest of the line discarded), never truncated into a
 *    different command.
 *  - SIGINT is caught WITHOUT SA_RESTART so a Ctrl-C at the prompt
 *    interrupts the blocking read (EINTR) and the shell prints a fresh
 *    prompt instead of dying. Children get the default dispositions back.
 *  - External programs are started with fork() + execvp() and reaped with
 *    waitpid(); no system() and no string concatenation is involved.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "parser.h"
#include "shell.h"

static volatile sig_atomic_t g_sigint_seen;

static void on_sigint(int sig)
{
    (void)sig;
    g_sigint_seen = 1;
}

void shell_init(shell_state *sh)
{
    job_list_init(&sh->jobs);
    sh->last_status = 0;
    sh->running = 1;
    sh->exit_code = 0;
    sh->interactive = 0;
    sh->prev_dir[0] = '\0';
}

void shell_free(shell_state *sh)
{
    job_list_free(&sh->jobs);
}

void shell_banner(FILE *out)
{
    fputs("\n"
          "   ____  _   _ ___ ____  _____\n"
          "  / ___|| | | |_ _|  _ \\| ____|\n"
          "  \\___ \\| |_| || || |_) |  _|\n"
          "   ___) |  _  || ||  _ <| |___\n"
          "  |____/|_| |_|___|_| \\_\\_____|\n"
          "\n", out);
    fprintf(out, "  Shire Shell %s - Shire OS / CampusCompute\n", SHIRE_VERSION);
    fputs("  Type 'help' for the list of commands, 'exit' to leave.\n\n", out);
}

/* ---------- external commands ---------- */

static void restore_default_signals(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

int shell_run_external(char *const argv[])
{
    fflush(stdout);                    /* buffered output must not be duplicated by fork */
    fflush(stderr);

    pid_t pid = fork();
    if (pid == -1) {
        fprintf(stderr, "shire: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        restore_default_signals();
        execvp(argv[0], argv);
        int e = errno;                 /* only reached when exec failed */
        if (e == ENOENT)
            fprintf(stderr, "shire: %s: command not found\n", argv[0]);
        else
            fprintf(stderr, "shire: %s: %s\n", argv[0], strerror(e));
        _exit(e == ENOENT ? 127 : 126);
    }

    int status;
    pid_t r;
    while ((r = waitpid(pid, &status, WUNTRACED)) == -1 && errno == EINTR)
        ;                              /* interrupted by our own SIGINT handler: keep waiting */
    if (r == -1) {
        fprintf(stderr, "shire: waitpid: %s\n", strerror(errno));
        return 1;
    }
    if (WIFSTOPPED(status)) {
        /* There is no job control, so a stopped child could never be resumed. */
        fprintf(stderr, "shire: process %d stopped; terminating it (no job control)\n", (int)pid);
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
            ;
        return 128 + SIGSTOP;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "shire: process %d terminated by signal %d (%s)\n",
                (int)pid, sig, strsignal(sig));
        return 128 + sig;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ---------- one command line ---------- */

int shell_execute(shell_state *sh, const char *line)
{
    parsed_cmd cmd;
    parse_status ps = parse_line(line, &cmd);

    if (ps == PARSE_EMPTY)
        return 0;                      /* blank line: keep last_status */
    if (ps != PARSE_OK) {
        fprintf(stderr, "shire: syntax error: %s\n", parse_status_str(ps));
        sh->last_status = 2;
        return 2;
    }

    int status;
    if (builtin_exists(cmd.argv[0]))
        status = builtin_run(sh, cmd.argc, cmd.argv);
    else
        status = shell_run_external(cmd.argv);

    parsed_cmd_free(&cmd);
    sh->last_status = status;
    return status;
}

/* ---------- input loop ---------- */

typedef enum { READ_OK, READ_EOF, READ_INTR, READ_TOOLONG, READ_ERR } read_status;

static read_status read_line(FILE *in, char *buf, size_t size)
{
    errno = 0;
    if (fgets(buf, (int)size, in) == NULL) {
        if (feof(in))
            return READ_EOF;
        if (errno == EINTR) {
            clearerr(in);
            return READ_INTR;
        }
        return READ_ERR;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        return READ_OK;
    }
    if (feof(in))                      /* last line without a newline */
        return READ_OK;
    int c;                             /* too long: discard the remainder */
    while ((c = fgetc(in)) != '\n' && c != EOF)
        ;
    return READ_TOOLONG;
}

static void install_signal_handlers(int interactive)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                   /* deliberately no SA_RESTART */
    sigaction(SIGINT, &sa, NULL);

    if (interactive) {                 /* no job control: do not let Ctrl-Z / Ctrl-\ stop the shell */
        signal(SIGTSTP, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
    }
}

int shell_run(shell_state *sh, FILE *in)
{
    char line[PARSE_MAX_LINE + 2];     /* room for newline + NUL */

    install_signal_handlers(sh->interactive);

    while (sh->running) {
        if (sh->interactive) {
            fputs("shire> ", stdout);
            fflush(stdout);
        }
        switch (read_line(in, line, sizeof line)) {
        case READ_OK:
            shell_execute(sh, line);
            break;
        case READ_INTR:
            g_sigint_seen = 0;
            fputc('\n', stdout);       /* Ctrl-C at the prompt: abandon the line */
            break;
        case READ_TOOLONG:
            fprintf(stderr, "shire: input line too long (maximum %d characters)\n", PARSE_MAX_LINE);
            sh->last_status = 2;
            break;
        case READ_EOF:
            if (sh->interactive)
                puts("exit");
            sh->exit_code = sh->last_status;
            sh->running = 0;
            break;
        case READ_ERR:
            fprintf(stderr, "shire: read error: %s\n", strerror(errno));
            sh->exit_code = 1;
            sh->running = 0;
            break;
        }
    }
    return sh->exit_code;
}
