/*
 * limits_demo.c - FR7: Resource limits
 *
 * RLIMIT_FSIZE: when a write would grow a file past the soft limit, the
 *   kernel sends SIGXFSZ. The child ignores SIGXFSZ so write() instead
 *   fails with errno EFBIG, which lets it report exactly how many bytes
 *   were written before the limit hit.
 *
 * RLIMIT_CPU: when the process has used the soft limit of CPU seconds,
 *   the kernel sends SIGXCPU, whose default action terminates the process.
 *   The parent detects this through waitpid(). RLIMIT_CORE is set to 0 in
 *   the child so no core file is left behind.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "limits_demo.h"

#define FSIZE_CHUNK    1024
#define FSIZE_ATTEMPTS 16

static void print_limit(const char *who, const char *name, int resource)
{
    struct rlimit rl;
    if (getrlimit(resource, &rl) == -1) {
        perror("getrlimit");
        return;
    }
    printf("  [%s] %s soft=", who, name);
    if (rl.rlim_cur == RLIM_INFINITY) printf("unlimited"); else printf("%llu", (unsigned long long)rl.rlim_cur);
    printf(" hard=");
    if (rl.rlim_max == RLIM_INFINITY) printf("unlimited"); else printf("%llu", (unsigned long long)rl.rlim_max);
    printf("\n");
}

static int wait_child(pid_t pid, int *status)
{
    pid_t r;
    while ((r = waitpid(pid, status, 0)) == -1 && errno == EINTR)
        ;
    if (r == -1) {
        perror("waitpid");
        return -1;
    }
    return 0;
}

/* ---------------- RLIMIT_FSIZE ---------------- */

static void fsize_child(const char *path, long limit)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_FSIZE, &rl) == -1) {
        perror("child: getrlimit");
        _exit(2);
    }
    rl.rlim_cur = (rlim_t)limit;               /* lower only the soft limit */
    if (setrlimit(RLIMIT_FSIZE, &rl) == -1) {
        perror("child: setrlimit(RLIMIT_FSIZE)");
        _exit(2);
    }
    print_limit("child ", "RLIMIT_FSIZE (after setrlimit)", RLIMIT_FSIZE);
    signal(SIGXFSZ, SIG_IGN);                  /* get EFBIG instead of being killed */

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        perror("child: open");
        _exit(2);
    }

    char buf[FSIZE_CHUNK];
    memset(buf, 'X', sizeof(buf));
    long total = 0;
    int hit_limit = 0;

    printf("  [child ] trying to write %d x %d bytes = %d bytes\n",
           FSIZE_ATTEMPTS, FSIZE_CHUNK, FSIZE_ATTEMPTS * FSIZE_CHUNK);
    for (int i = 1; i <= FSIZE_ATTEMPTS; i++) {
        ssize_t n = write(fd, buf, sizeof(buf));
        if (n == -1) {
            printf("  [child ] write #%d FAILED: %s (errno %d) after %ld bytes\n",
                   i, strerror(errno), errno, total);
            hit_limit = (errno == EFBIG);
            break;
        }
        total += n;
        printf("  [child ] write #%d ok, file size now %ld bytes\n", i, total);
        if (n < (ssize_t)sizeof(buf))           /* partial write right at the limit */
            printf("  [child ] partial write (%zd bytes): limit reached\n", n);
    }
    close(fd);
    fflush(stdout);
    _exit(hit_limit ? 0 : 1);
}

int limits_fsize_demo(long limit_bytes)
{
    if (limit_bytes < 1 || limit_bytes > 1024L * 1024L) {
        fprintf(stderr, "limits: file-size limit must be 1..1048576 bytes\n");
        return -1;
    }

    char path[64];
    snprintf(path, sizeof(path), "/tmp/cc_fsize_test_%d.tmp", (int)getpid());

    printf("\n=== FR7: Resource Limit - RLIMIT_FSIZE (max file size) ===\n");
    printf("  limit to apply in child: %ld bytes, test file: %s\n", limit_bytes, path);
    print_limit("parent", "RLIMIT_FSIZE (before)", RLIMIT_FSIZE);

    fflush(stdout);
    pid_t pid = fork();
    if (pid == -1) {
        perror("limits: fork");
        return -1;
    }
    if (pid == 0)
        fsize_child(path, limit_bytes);

    int status, rc = -1;
    if (wait_child(pid, &status) == 0) {
        if (WIFEXITED(status))
            printf("  [parent] child PID %d exited with status %d\n", (int)pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("  [parent] child PID %d killed by signal %d (%s)\n",
                   (int)pid, WTERMSIG(status), strsignal(WTERMSIG(status)));

        struct stat st;
        if (stat(path, &st) == 0) {
            printf("  [parent] actual file size on disk: %lld bytes (limit %ld)\n",
                   (long long)st.st_size, limit_bytes);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && st.st_size <= limit_bytes)
                rc = 0;
        }
    }

    print_limit("parent", "RLIMIT_FSIZE (after, unchanged)", RLIMIT_FSIZE);

    if (unlink(path) == 0)
        printf("  [parent] removed test file %s\n", path);
    else if (errno != ENOENT)
        perror("limits: unlink");

    printf("  OBSERVED EFFECT: %s\n", rc == 0
           ? "the kernel refused to grow the file past the limit (write -> EFBIG); "
             "the parent's own limit was not affected"
           : "limit was NOT observed as expected");
    return rc;
}

/* ---------------- RLIMIT_CPU ---------------- */

int limits_cpu_demo(int seconds)
{
    if (seconds < 1 || seconds > 10) {
        fprintf(stderr, "limits: CPU limit must be 1..10 seconds\n");
        return -1;
    }

    printf("\n=== FR7: Resource Limit - RLIMIT_CPU (CPU seconds) ===\n");
    print_limit("parent", "RLIMIT_CPU (before)", RLIMIT_CPU);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == -1) {
        perror("limits: fork");
        return -1;
    }
    if (pid == 0) {
        struct rlimit core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &core);                 /* no core dump file */
        struct rlimit rl = { (rlim_t)seconds, (rlim_t)seconds + 1 };
        if (setrlimit(RLIMIT_CPU, &rl) == -1) {
            perror("child: setrlimit(RLIMIT_CPU)");
            _exit(2);
        }
        print_limit("child ", "RLIMIT_CPU (after setrlimit)", RLIMIT_CPU);
        printf("  [child ] busy-looping until the kernel stops me...\n");
        fflush(stdout);
        volatile unsigned long long spin = 0;
        for (;;)
            spin++;
    }

    int status, rc = -1;
    if (wait_child(pid, &status) == 0) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            printf("  [parent] child PID %d terminated by signal %d (%s) after %.2f s\n",
                   (int)pid, sig, strsignal(sig), secs);
            if (sig == SIGXCPU || sig == SIGKILL)
                rc = 0;
        } else if (WIFEXITED(status)) {
            printf("  [parent] child exited with status %d (limit not observed)\n",
                   WEXITSTATUS(status));
        }
    }
    print_limit("parent", "RLIMIT_CPU (after, unchanged)", RLIMIT_CPU);
    printf("  OBSERVED EFFECT: %s\n", rc == 0
           ? "the kernel stopped the child once it used its CPU-time limit "
             "(exact timing can vary slightly)"
           : "limit was NOT observed as expected");
    return rc;
}

int limits_demo(void)
{
    return limits_fsize_demo(LIMITS_FSIZE_DEFAULT);
}
