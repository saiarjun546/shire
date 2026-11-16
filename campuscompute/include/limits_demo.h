/*
 * limits_demo.h - FR7: Process resource limits with setrlimit()
 *
 * Owner: Person 2
 *
 * (Named limits_demo.h rather than limits.h so it never shadows the
 *  standard C header <limits.h>.)
 *
 * Limits are applied only inside a child process, so the main program /
 * shell keeps its own limits unchanged.
 */
#ifndef LIMITS_DEMO_H
#define LIMITS_DEMO_H

#define LIMITS_FSIZE_DEFAULT  4096L   /* bytes   */
#define LIMITS_CPU_DEFAULT    1       /* seconds */

/* RLIMIT_FSIZE: child tries to write 16 KB into a file limited to
 * limit_bytes. Returns 0 if the limit was observed, -1 otherwise. */
int limits_fsize_demo(long limit_bytes);

/* RLIMIT_CPU: child busy-loops until the kernel stops it with SIGXCPU.
 * Returns 0 if the limit was observed, -1 otherwise. */
int limits_cpu_demo(int seconds);

/* Default demo = file-size limit (more reproducible than CPU time). */
int limits_demo(void);

#endif /* LIMITS_DEMO_H */
