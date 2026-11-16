/*
 * builtins.c - Shire Shell built-in commands
 *
 * Status convention: 0 = success, non-zero = error (like a Unix exit code).
 * Built-ins print their own error messages to stderr and never call exit().
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "banker.h"
#include "builtins.h"
#include "ipc.h"
#include "jobs.h"
#include "limits_demo.h"
#include "logger.h"
#include "parser.h"
#include "scheduler.h"
#include "worker.h"

typedef int (*builtin_fn)(shell_state *sh, int argc, char **argv);

typedef struct {
    const char *name;
    builtin_fn  fn;
    const char *usage;
    const char *help;
} builtin_entry;

static int bi_help(shell_state *sh, int argc, char **argv);
static int bi_clear(shell_state *sh, int argc, char **argv);
static int bi_pwd(shell_state *sh, int argc, char **argv);
static int bi_cd(shell_state *sh, int argc, char **argv);
static int bi_jobs(shell_state *sh, int argc, char **argv);
static int bi_schedule(shell_state *sh, int argc, char **argv);
static int bi_sysinfo(shell_state *sh, int argc, char **argv);
static int bi_version(shell_state *sh, int argc, char **argv);
static int bi_exit(shell_state *sh, int argc, char **argv);
static int bi_log(shell_state *sh, int argc, char **argv);
static int bi_ipc(shell_state *sh, int argc, char **argv);
static int bi_banker(shell_state *sh, int argc, char **argv);
static int bi_process(shell_state *sh, int argc, char **argv);
static int bi_limit(shell_state *sh, int argc, char **argv);

static const builtin_entry table[] = {
    { "help",     bi_help,     "help",
      "show this list" },
    { "clear",    bi_clear,    "clear",
      "clear the terminal screen" },
    { "pwd",      bi_pwd,      "pwd",
      "print the current working directory" },
    { "cd",       bi_cd,       "cd [dir | -]",
      "change directory (no argument = $HOME, '-' = previous directory)" },
    { "jobs",     bi_jobs,     "jobs [list | add <id> <arrival> <burst> <priority> | reset | clear]",
      "manage the job table (FR1)" },
    { "schedule", bi_schedule, "schedule fcfs | rr <quantum> | priority",
      "run a CPU scheduling algorithm on the job table (FR2)" },
    { "sysinfo",  bi_sysinfo,  "sysinfo",
      "show kernel, memory, uptime and CPU information" },
    { "version",  bi_version,  "version",
      "show the Shire Shell version" },
    { "exit",     bi_exit,     "exit [code]",
      "leave the shell (code 0-255, default = last command's status)" },
    { "log",      bi_log,      "log [producers consumers msgs capacity]",
      "FR3 producer-consumer logger demo" },
    { "ipc",      bi_ipc,      "ipc [messages]",
      "FR4 shared-memory + semaphore IPC demo" },
    { "banker",   bi_banker,   "banker safe | unsafe | file <path>",
      "FR5 Banker's algorithm" },
    { "process",  bi_process,  "process normal | fail | signal",
      "FR6 worker process and failure detection" },
    { "limit",    bi_limit,    "limit [fsize [bytes] | cpu [seconds]]",
      "FR7 resource-limit demo (runs in a child process)" },
};
#define N_BUILTINS (sizeof table / sizeof table[0])

int builtin_exists(const char *name)
{
    for (size_t i = 0; i < N_BUILTINS; i++)
        if (strcmp(name, table[i].name) == 0)
            return 1;
    return 0;
}

int builtin_run(shell_state *sh, int argc, char **argv)
{
    for (size_t i = 0; i < N_BUILTINS; i++)
        if (strcmp(argv[0], table[i].name) == 0) {
            fflush(stdout);       /* Person 2's demos fork(); never duplicate buffered output */
            return table[i].fn(sh, argc, argv);
        }
    fprintf(stderr, "shire: internal error: '%s' is not a built-in\n", argv[0]);
    return 1;
}

static int usage_error(const char *name)
{
    for (size_t i = 0; i < N_BUILTINS; i++)
        if (strcmp(name, table[i].name) == 0)
            fprintf(stderr, "usage: %s\n", table[i].usage);
    return 2;
}

/* ---------- required built-ins ---------- */

static int bi_help(shell_state *sh, int argc, char **argv)
{
    (void)sh; (void)argv;
    if (argc != 1)
        return usage_error("help");
    puts("Shire Shell built-in commands:");
    for (size_t i = 0; i < N_BUILTINS; i++)
        printf("  %-10s %s\n             usage: %s\n", table[i].name, table[i].help, table[i].usage);
    puts("\nAny other command is run as an external program found through PATH.\n"
         "Quoting: 'single', \"double\" and backslash. Pipes, redirection and globbing\n"
         "are not supported.");
    return 0;
}

static int bi_clear(shell_state *sh, int argc, char **argv)
{
    (void)sh; (void)argv;
    if (argc != 1)
        return usage_error("clear");
    fputs("\033[H\033[2J", stdout);      /* ANSI: cursor home + erase screen */
    return 0;
}

static int bi_pwd(shell_state *sh, int argc, char **argv)
{
    (void)sh; (void)argv;
    if (argc != 1)
        return usage_error("pwd");
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof buf) == NULL) {
        fprintf(stderr, "pwd: %s\n", strerror(errno));
        return 1;
    }
    puts(buf);
    return 0;
}

static int bi_cd(shell_state *sh, int argc, char **argv)
{
    if (argc > 2) {
        fputs("cd: too many arguments\n", stderr);
        return usage_error("cd");
    }
    const char *target;
    int print_dir = 0;
    if (argc == 1) {
        target = getenv("HOME");
        if (target == NULL || *target == '\0') {
            fputs("cd: HOME is not set\n", stderr);
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        if (sh->prev_dir[0] == '\0') {
            fputs("cd: no previous directory\n", stderr);
            return 1;
        }
        target = sh->prev_dir;
        print_dir = 1;
    } else {
        target = argv[1];
    }

    char old[PATH_MAX];
    int have_old = getcwd(old, sizeof old) != NULL;
    if (chdir(target) != 0) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    if (have_old) {
        snprintf(sh->prev_dir, sizeof sh->prev_dir, "%s", old);
        setenv("OLDPWD", old, 1);
    }
    char now[PATH_MAX];
    if (getcwd(now, sizeof now) != NULL) {
        setenv("PWD", now, 1);
        if (print_dir)
            puts(now);
    }
    return 0;
}

static int bi_version(shell_state *sh, int argc, char **argv)
{
    (void)sh; (void)argv;
    if (argc != 1)
        return usage_error("version");
    puts("Shire Shell " SHIRE_VERSION " (Shire OS / CampusCompute)");
    return 0;
}

static int bi_exit(shell_state *sh, int argc, char **argv)
{
    long code = sh->last_status;
    if (argc > 2) {
        fputs("exit: too many arguments\n", stderr);
        return usage_error("exit");
    }
    if (argc == 2 && parse_int_range(argv[1], 0, 255, &code) != 0) {
        fprintf(stderr, "exit: '%s' is not an exit code in 0..255\n", argv[1]);
        return 2;                       /* stay in the shell */
    }
    sh->exit_code = (int)code;
    sh->running = 0;
    return (int)code;
}

static int bi_sysinfo(shell_state *sh, int argc, char **argv)
{
    (void)sh; (void)argv;
    if (argc != 1)
        return usage_error("sysinfo");

    struct utsname u;
    if (uname(&u) != 0) {
        fprintf(stderr, "sysinfo: uname: %s\n", strerror(errno));
        return 1;
    }
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        fprintf(stderr, "sysinfo: %s\n", strerror(errno));
        return 1;
    }
    unsigned long long unit = si.mem_unit ? si.mem_unit : 1;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long up = si.uptime;

    printf("Shire Shell %s\n", SHIRE_VERSION);
    printf("  Hostname      : %s\n", u.nodename);
    printf("  OS            : %s\n", u.sysname);
    printf("  Kernel        : %s (%s)\n", u.release, u.version);
    printf("  Architecture  : %s\n", u.machine);
    printf("  CPUs online   : %ld\n", cpus > 0 ? cpus : 1);
    printf("  Uptime        : %ldd %ldh %ldm %lds\n",
           up / 86400, (up % 86400) / 3600, (up % 3600) / 60, up % 60);
    printf("  Load average  : %.2f %.2f %.2f\n",
           si.loads[0] / 65536.0, si.loads[1] / 65536.0, si.loads[2] / 65536.0);
    printf("  Memory total  : %llu MB\n", (unsigned long long)si.totalram * unit / (1024 * 1024));
    printf("  Memory free   : %llu MB\n", (unsigned long long)si.freeram * unit / (1024 * 1024));
    printf("  Processes     : %u\n", (unsigned)si.procs);
    printf("  Shell PID     : %d   UID: %d\n", (int)getpid(), (int)getuid());
    return 0;
}

/* ---------- FR1: jobs ---------- */

static int bi_jobs(shell_state *sh, int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "list") == 0)) {
        job_list_print(&sh->jobs, stdout);
        return 0;
    }
    if (strcmp(argv[1], "add") == 0) {
        if (argc != 6) {
            fputs("jobs add: expected 4 numbers\n", stderr);
            return usage_error("jobs");
        }
        long v[4];
        static const char *const names[4] = { "id", "arrival", "burst", "priority" };
        for (int i = 0; i < 4; i++)
            if (parse_int_range(argv[2 + i], 0, JOB_VALUE_MAX, &v[i]) != 0) {
                fprintf(stderr, "jobs add: %s '%s' is not an integer in 0..%d\n",
                        names[i], argv[2 + i], JOB_VALUE_MAX);
                return 1;
            }
        job_status st = job_list_add(&sh->jobs, (int)v[0], (int)v[1], (int)v[2], (int)v[3]);
        if (st != JOB_OK) {
            fprintf(stderr, "jobs add: %s\n", job_status_str(st));
            return 1;
        }
        printf("Added P%ld (arrival %ld, burst %ld, priority %ld). %zu job(s) defined.\n",
               v[0], v[1], v[2], v[3], sh->jobs.count);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        job_list_reset_results(&sh->jobs);
        puts("Scheduling results cleared.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        job_list_free(&sh->jobs);
        puts("All jobs removed.");
        return 0;
    }
    return usage_error("jobs");
}

/* ---------- FR2: schedule ---------- */

static int bi_schedule(shell_state *sh, int argc, char **argv)
{
    if (argc < 2)
        return usage_error("schedule");

    sched_result res;
    sched_status st;
    sched_result_init(&res);

    if (strcmp(argv[1], "fcfs") == 0 && argc == 2) {
        st = sched_fcfs(&sh->jobs, &res);
    } else if (strcmp(argv[1], "priority") == 0 && argc == 2) {
        st = sched_priority(&sh->jobs, &res);
    } else if (strcmp(argv[1], "rr") == 0 && argc == 3) {
        long q;
        if (parse_int_range(argv[2], 1, JOB_VALUE_MAX, &q) != 0) {
            fprintf(stderr, "schedule: quantum '%s' must be an integer in 1..%d\n",
                    argv[2], JOB_VALUE_MAX);
            return 1;
        }
        st = sched_round_robin(&sh->jobs, (int)q, &res);
    } else {
        return usage_error("schedule");
    }

    if (st != SCHED_OK) {
        fprintf(stderr, "schedule: %s\n", sched_status_str(st));
        return 1;
    }
    sched_print_report(&sh->jobs, &res, stdout);
    sched_result_free(&res);
    return 0;
}

/* ---------- Person 2 integration ---------- */

static int bi_log(shell_state *sh, int argc, char **argv)
{
    (void)sh;
    if (argc == 1)
        return logger_demo() == 0 ? 0 : 1;
    long v[4];
    static const long hi[4] = { LOGGER_MAX_THREADS, LOGGER_MAX_THREADS,
                                LOGGER_MAX_MSGS, LOGGER_MAX_CAPACITY };
    if (argc != 5)
        return usage_error("log");
    for (int i = 0; i < 4; i++)
        if (parse_int_range(argv[1 + i], 1, hi[i], &v[i]) != 0) {
            fprintf(stderr, "log: '%s' is not an integer in 1..%ld\n", argv[1 + i], hi[i]);
            return 1;
        }
    return logger_run((int)v[0], (int)v[1], (int)v[2], (int)v[3], LOGGER_DEFAULT_FILE) == 0 ? 0 : 1;
}

static int bi_ipc(shell_state *sh, int argc, char **argv)
{
    (void)sh;
    if (argc == 1)
        return ipc_demo() == 0 ? 0 : 1;
    long n;
    if (argc != 2 || parse_int_range(argv[1], 1, IPC_MAX_MESSAGES, &n) != 0) {
        fprintf(stderr, "ipc: messages must be an integer in 1..%d\n", IPC_MAX_MESSAGES);
        return 2;
    }
    return ipc_run((int)n) == 0 ? 0 : 1;
}

static int bi_banker(shell_state *sh, int argc, char **argv)
{
    (void)sh;
    if (argc == 2 && strcmp(argv[1], "safe") == 0)
        return banker_demo_safe() == 1 ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "unsafe") == 0)
        return banker_demo_unsafe() == 0 ? 0 : 1;
    if (argc == 3 && strcmp(argv[1], "file") == 0) {
        banker_state s;
        if (banker_load_file(argv[2], &s) != 0)
            return 1;
        printf("\n=== FR5: Banker's Algorithm - state from %s ===\n", argv[2]);
        /* both "safe" and "unsafe" are valid answers; only invalid input fails */
        return banker_check_safety(&s) < 0 ? 1 : 0;
    }
    return usage_error("banker");
}

static int bi_process(shell_state *sh, int argc, char **argv)
{
    (void)sh;
    if (argc == 2 && strcmp(argv[1], "normal") == 0)
        return worker_demo(WORKER_NORMAL) == 0 ? 0 : 1;
    /* for the failure demos, status 0 means "the failure was detected" */
    if (argc == 2 && strcmp(argv[1], "fail") == 0)
        return worker_demo(WORKER_FAIL_EXIT) == 1 ? 0 : 1;
    if (argc == 2 && strcmp(argv[1], "signal") == 0)
        return worker_demo(WORKER_FAIL_SIGNAL) == 1 ? 0 : 1;
    return usage_error("process");
}

static int bi_limit(shell_state *sh, int argc, char **argv)
{
    (void)sh;
    if (argc == 1)
        return limits_demo() == 0 ? 0 : 1;
    if (argc >= 2 && argc <= 3 && strcmp(argv[1], "fsize") == 0) {
        long b = LIMITS_FSIZE_DEFAULT;
        if (argc == 3 && parse_int_range(argv[2], 1, 1024L * 1024L, &b) != 0) {
            fprintf(stderr, "limit: bytes must be an integer in 1..1048576\n");
            return 2;
        }
        return limits_fsize_demo(b) == 0 ? 0 : 1;
    }
    if (argc >= 2 && argc <= 3 && strcmp(argv[1], "cpu") == 0) {
        long s = LIMITS_CPU_DEFAULT;
        if (argc == 3 && parse_int_range(argv[2], 1, 10, &s) != 0) {
            fprintf(stderr, "limit: seconds must be an integer in 1..10\n");
            return 2;
        }
        return limits_cpu_demo((int)s) == 0 ? 0 : 1;
    }
    return usage_error("limit");
}
