/*
 * p1test.c - Unit tests for Person 1's modules: jobs, scheduler, parser.
 *
 * Expected values in the fixed scenarios were worked out by hand (the
 * working is shown in the comments), so the tests check the algorithms
 * against independent arithmetic, not against their own output.
 * A randomised invariant test then checks every algorithm on many
 * generated job sets.
 *
 * Exit status: 0 = all checks passed, 1 = at least one failed.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jobs.h"
#include "parser.h"
#include "scheduler.h"

static int g_checks, g_failed;

#define CHECK(cond) do {                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-9)

static void section(const char *name) { printf("[%s]\n", name); }

/* ---- helpers ---- */

static const job_t *J(const job_list *jl, int id) { return job_list_find(jl, id); }

static void add(job_list *jl, int id, int a, int b, int p)
{
    job_status st = job_list_add(jl, id, a, b, p);
    if (st != JOB_OK) {
        printf("  setup error: add P%d: %s\n", id, job_status_str(st));
        g_failed++;
    }
}

/* Check slice i is (job, start, end). */
static int slice_is(const sched_result *r, size_t i, int job, int s, int e)
{
    return i < r->count && r->slices[i].job_id == job &&
           r->slices[i].start == s && r->slices[i].end == e;
}

/* Check completion/turnaround/waiting of one job. */
static int times_are(const job_list *jl, int id, int c, int t, int w)
{
    const job_t *j = J(jl, id);
    return j && j->completion == c && j->turnaround == t && j->waiting == w &&
           j->remaining == 0;
}

/* ================= jobs ================= */

static void test_jobs(void)
{
    section("jobs: add / validate / duplicates / reset");
    job_list jl;
    job_list_init(&jl);

    CHECK(jl.count == 0);
    CHECK(J(&jl, 1) == NULL);                              /* empty list */

    CHECK(job_list_add(&jl, 1, 0, 5, 2) == JOB_OK);
    CHECK(jl.count == 1);
    CHECK(J(&jl, 1)->remaining == 5);
    CHECK(J(&jl, 1)->completion == JOB_UNSET);

    CHECK(job_list_add(&jl, 1, 3, 3, 3) == JOB_ERR_DUPLICATE);  /* duplicate id */
    CHECK(jl.count == 1);
    CHECK(J(&jl, 1)->burst == 5);                           /* original untouched */

    CHECK(job_list_add(&jl, 0, 0, 1, 0) == JOB_ERR_ID);
    CHECK(job_list_add(&jl, -4, 0, 1, 0) == JOB_ERR_ID);
    CHECK(job_list_add(&jl, JOB_VALUE_MAX + 1, 0, 1, 0) == JOB_ERR_ID);
    CHECK(job_list_add(&jl, 2, -1, 1, 0) == JOB_ERR_ARRIVAL);
    CHECK(job_list_add(&jl, 2, JOB_VALUE_MAX + 1, 1, 0) == JOB_ERR_ARRIVAL);
    CHECK(job_list_add(&jl, 2, 0, 0, 0) == JOB_ERR_BURST);  /* zero burst */
    CHECK(job_list_add(&jl, 2, 0, -3, 0) == JOB_ERR_BURST);
    CHECK(job_list_add(&jl, 2, 0, 1, -1) == JOB_ERR_PRIORITY);
    CHECK(job_list_add(NULL, 2, 0, 1, 0) == JOB_ERR_NULL);
    CHECK(jl.count == 1);                                   /* nothing was added */

    /* boundaries that must be accepted */
    CHECK(job_list_add(&jl, JOB_VALUE_MAX, JOB_VALUE_MAX, JOB_VALUE_MAX, JOB_VALUE_MAX) == JOB_OK);
    CHECK(job_list_add(&jl, 2, 0, 1, 0) == JOB_OK);
    CHECK(jl.count == 3);

    /* results reset keeps the original burst */
    sched_result r;
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    CHECK(J(&jl, 1)->completion != JOB_UNSET);
    sched_result_free(&r);
    job_list_reset_results(&jl);
    CHECK(J(&jl, 1)->remaining == 5 && J(&jl, 1)->burst == 5);
    CHECK(J(&jl, 1)->completion == JOB_UNSET && J(&jl, 1)->turnaround == JOB_UNSET &&
          J(&jl, 1)->waiting == JOB_UNSET);

    job_list_free(&jl);
    CHECK(jl.count == 0 && jl.items == NULL);
    job_list_free(&jl);                                     /* double free is safe */

    section("jobs: table limit and growth");
    for (int i = 1; i <= JOBS_MAX; i++)
        CHECK(job_list_add(&jl, i, i, 1, 0) == JOB_OK);
    CHECK(jl.count == JOBS_MAX);
    CHECK(job_list_add(&jl, JOBS_MAX + 1, 0, 1, 0) == JOB_ERR_FULL);
    CHECK(jl.count == JOBS_MAX);
    CHECK(J(&jl, JOBS_MAX) != NULL && J(&jl, JOBS_MAX)->arrival == JOBS_MAX);
    job_list_free(&jl);

    section("jobs: printing");
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        job_list_print(&jl, f);                             /* empty list */
        add(&jl, 7, 1, 2, 3);
        job_list_print(&jl, f);
        rewind(f);
        char buf[512] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        CHECK(strstr(buf, "No jobs defined") != NULL);
        CHECK(strstr(buf, "P7") != NULL);
        fclose(f);
    }
    job_list_free(&jl);
}

/* ================= scheduler: fixed scenarios ================= */

static void test_fcfs(void)
{
    section("FCFS: classic example");
    /* P1 a0 b5 | P2 a1 b3 | P3 a2 b8 | P4 a3 b6
     * Timeline: P1 0-5, P2 5-8, P3 8-16, P4 16-22
     * TAT = 5, 7, 14, 19  -> avg 11.25
     * WT  = 0, 4,  6, 13  -> avg  5.75 */
    job_list jl; job_list_init(&jl);
    sched_result r;
    add(&jl, 1, 0, 5, 0); add(&jl, 2, 1, 3, 0); add(&jl, 3, 2, 8, 0); add(&jl, 4, 3, 6, 0);
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    CHECK(r.count == 4);
    CHECK(slice_is(&r, 0, 1, 0, 5) && slice_is(&r, 1, 2, 5, 8) &&
          slice_is(&r, 2, 3, 8, 16) && slice_is(&r, 3, 4, 16, 22));
    CHECK(times_are(&jl, 1, 5, 5, 0) && times_are(&jl, 2, 8, 7, 4) &&
          times_are(&jl, 3, 16, 14, 6) && times_are(&jl, 4, 22, 19, 13));
    CHECK_NEAR(r.avg_turnaround, 11.25);
    CHECK_NEAR(r.avg_waiting, 5.75);
    CHECK(r.total_time == 22);
    sched_result_free(&r);

    section("FCFS: input order is irrelevant, arrival decides");
    job_list_free(&jl);
    add(&jl, 1, 4, 2, 0); add(&jl, 2, 0, 3, 0);             /* typed out of order */
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    /* idle: none. P2 0-3, idle 3-4, P1 4-6 */
    CHECK(r.count == 3 && slice_is(&r, 0, 2, 0, 3) && slice_is(&r, 1, GANTT_IDLE, 3, 4) &&
          slice_is(&r, 2, 1, 4, 6));
    CHECK(jl.items[0].id == 1 && jl.items[1].id == 2);      /* caller's order unchanged */
    sched_result_free(&r);

    section("FCFS: CPU idle at start and between jobs");
    job_list_free(&jl);
    add(&jl, 1, 2, 3, 0); add(&jl, 2, 10, 2, 0);
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    /* idle 0-2, P1 2-5, idle 5-10, P2 10-12 ; TAT 3,2 ; WT 0,0 */
    CHECK(r.count == 4 && slice_is(&r, 0, GANTT_IDLE, 0, 2) && slice_is(&r, 1, 1, 2, 5) &&
          slice_is(&r, 2, GANTT_IDLE, 5, 10) && slice_is(&r, 3, 2, 10, 12));
    CHECK(times_are(&jl, 1, 5, 3, 0) && times_are(&jl, 2, 12, 2, 0));
    CHECK(r.total_time == 12);
    sched_result_free(&r);

    section("FCFS: simultaneous arrivals -> lower id first");
    job_list_free(&jl);
    add(&jl, 3, 0, 2, 0); add(&jl, 1, 0, 2, 0); add(&jl, 2, 0, 2, 0);
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    CHECK(slice_is(&r, 0, 1, 0, 2) && slice_is(&r, 1, 2, 2, 4) && slice_is(&r, 2, 3, 4, 6));
    CHECK_NEAR(r.avg_waiting, 2.0);                         /* 0,2,4 */
    sched_result_free(&r);

    section("FCFS: single job and error cases");
    job_list_free(&jl);
    CHECK(sched_fcfs(&jl, &r) == SCHED_ERR_EMPTY);          /* empty list */
    CHECK(r.count == 0 && r.slices == NULL);
    CHECK(sched_fcfs(NULL, &r) == SCHED_ERR_NULL);
    CHECK(sched_fcfs(&jl, NULL) == SCHED_ERR_NULL);
    add(&jl, 9, 0, 1, 0);
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    CHECK(r.count == 1 && times_are(&jl, 9, 1, 1, 0));
    sched_result_free(&r);
    job_list_free(&jl);
}

static void test_rr(void)
{
    section("Round Robin: hand-traced example, quantum 2");
    /* P1 a0 b5 | P2 a1 b3 | P3 a2 b1 | P4 a3 b2 , q = 2
     * t0  queue[P1]            run P1 0-2 (rem 3); P2,P3 arrive -> [P2 P3 P1]
     * t2  P2 2-4 (rem 1); P4 arrives at 3 -> [P3 P1 P4 P2]
     * t4  P3 4-5 done(5)       -> [P1 P4 P2]
     * t5  P1 5-7 (rem 1)       -> [P4 P2 P1]
     * t7  P4 7-9 done(9)       -> [P2 P1]
     * t9  P2 9-10 done(10)     -> [P1]
     * t10 P1 10-11 done(11)
     * TAT: P1 11, P2 9, P3 3, P4 6 (avg 7.25)   WT: 6, 6, 2, 4 (avg 4.5) */
    job_list jl; job_list_init(&jl);
    sched_result r;
    add(&jl, 1, 0, 5, 0); add(&jl, 2, 1, 3, 0); add(&jl, 3, 2, 1, 0); add(&jl, 4, 3, 2, 0);
    CHECK(sched_round_robin(&jl, 2, &r) == SCHED_OK);
    CHECK(r.count == 7);
    CHECK(slice_is(&r, 0, 1, 0, 2) && slice_is(&r, 1, 2, 2, 4) && slice_is(&r, 2, 3, 4, 5) &&
          slice_is(&r, 3, 1, 5, 7) && slice_is(&r, 4, 4, 7, 9) && slice_is(&r, 5, 2, 9, 10) &&
          slice_is(&r, 6, 1, 10, 11));
    CHECK(times_are(&jl, 1, 11, 11, 6) && times_are(&jl, 2, 10, 9, 6) &&
          times_are(&jl, 3, 5, 3, 2) && times_are(&jl, 4, 9, 6, 4));
    CHECK_NEAR(r.avg_turnaround, 7.25);
    CHECK_NEAR(r.avg_waiting, 4.5);
    CHECK(J(&jl, 1)->burst == 5 && J(&jl, 2)->burst == 3);   /* burst preserved */
    sched_result_free(&r);

    section("Round Robin: quantum 1, quantum = max burst, quantum > all bursts");
    job_list_free(&jl);
    add(&jl, 1, 0, 3, 0); add(&jl, 2, 0, 2, 0);
    CHECK(sched_round_robin(&jl, 1, &r) == SCHED_OK);
    /* q=1: P1 P2 P1 P2 P1 -> 5 slices; P2 done at 4, P1 at 5 */
    CHECK(r.count == 5 && slice_is(&r, 0, 1, 0, 1) && slice_is(&r, 1, 2, 1, 2) &&
          slice_is(&r, 4, 1, 4, 5));
    CHECK(times_are(&jl, 2, 4, 4, 2) && times_are(&jl, 1, 5, 5, 2));
    sched_result_free(&r);

    CHECK(sched_round_robin(&jl, 3, &r) == SCHED_OK);        /* q = largest burst */
    CHECK(r.count == 2 && slice_is(&r, 0, 1, 0, 3) && slice_is(&r, 1, 2, 3, 5));
    sched_result_free(&r);

    CHECK(sched_round_robin(&jl, 1000, &r) == SCHED_OK);     /* behaves like FCFS */
    CHECK(r.count == 2 && times_are(&jl, 1, 3, 3, 0) && times_are(&jl, 2, 5, 5, 3));
    sched_result_free(&r);

    section("Round Robin: arrival exactly when a slice ends goes BEFORE the preempted job");
    job_list_free(&jl);
    add(&jl, 1, 0, 4, 0); add(&jl, 2, 2, 2, 0);              /* P2 arrives at t=2 = end of P1's 1st slice */
    CHECK(sched_round_robin(&jl, 2, &r) == SCHED_OK);
    /* P1 0-2, then queue is [P2, P1]: P2 2-4, P1 4-6 */
    CHECK(r.count == 3 && slice_is(&r, 0, 1, 0, 2) && slice_is(&r, 1, 2, 2, 4) &&
          slice_is(&r, 2, 1, 4, 6));
    sched_result_free(&r);

    section("Round Robin: idle periods");
    job_list_free(&jl);
    add(&jl, 1, 3, 2, 0); add(&jl, 2, 10, 1, 0);
    CHECK(sched_round_robin(&jl, 4, &r) == SCHED_OK);
    /* idle 0-3, P1 3-5, idle 5-10, P2 10-11 */
    CHECK(r.count == 4 && slice_is(&r, 0, GANTT_IDLE, 0, 3) && slice_is(&r, 1, 1, 3, 5) &&
          slice_is(&r, 2, GANTT_IDLE, 5, 10) && slice_is(&r, 3, 2, 10, 11));
    CHECK(r.total_time == 11);
    sched_result_free(&r);

    section("Round Robin: a lone job keeps running (slices merged)");
    job_list_free(&jl);
    add(&jl, 1, 0, 7, 0);
    CHECK(sched_round_robin(&jl, 2, &r) == SCHED_OK);
    CHECK(r.count == 1 && slice_is(&r, 0, 1, 0, 7));
    sched_result_free(&r);

    section("Round Robin: invalid quantum / empty list");
    CHECK(sched_round_robin(&jl, 0, &r) == SCHED_ERR_QUANTUM);
    CHECK(r.count == 0 && J(&jl, 1)->completion == JOB_UNSET);
    CHECK(sched_round_robin(&jl, -3, &r) == SCHED_ERR_QUANTUM);
    CHECK(sched_round_robin(&jl, JOB_VALUE_MAX + 1, &r) == SCHED_ERR_QUANTUM);
    CHECK(sched_round_robin(&jl, JOB_VALUE_MAX, &r) == SCHED_OK);   /* boundary */
    sched_result_free(&r);
    job_list_free(&jl);
    CHECK(sched_round_robin(&jl, 2, &r) == SCHED_ERR_EMPTY);
    CHECK(sched_round_robin(&jl, 2, NULL) == SCHED_ERR_NULL);
}

static void test_priority(void)
{
    section("Priority (non-preemptive): hand-traced example");
    /* P1 a0 b3 p2 | P2 a1 b2 p1 | P3 a2 b1 p1 | P4 a3 b4 p3
     * t0: only P1 has arrived -> runs 0-3 (non-preemptive even though P2 is better)
     * t3: ready P2(p1) P3(p1) P4(p3) -> P2 (same priority as P3, earlier arrival) 3-5
     * t5: P3 5-6, then P4 6-10
     * C = 3,5,6,10  TAT = 3,4,4,7 (avg 4.5)  WT = 0,2,3,3 (avg 2.0) */
    job_list jl; job_list_init(&jl);
    sched_result r;
    add(&jl, 1, 0, 3, 2); add(&jl, 2, 1, 2, 1); add(&jl, 3, 2, 1, 1); add(&jl, 4, 3, 4, 3);
    CHECK(sched_priority(&jl, &r) == SCHED_OK);
    CHECK(r.count == 4 && slice_is(&r, 0, 1, 0, 3) && slice_is(&r, 1, 2, 3, 5) &&
          slice_is(&r, 2, 3, 5, 6) && slice_is(&r, 3, 4, 6, 10));
    CHECK(times_are(&jl, 1, 3, 3, 0) && times_are(&jl, 2, 5, 4, 2) &&
          times_are(&jl, 3, 6, 4, 3) && times_are(&jl, 4, 10, 7, 3));
    CHECK_NEAR(r.avg_turnaround, 4.5);
    CHECK_NEAR(r.avg_waiting, 2.0);
    sched_result_free(&r);

    section("Priority: smaller number wins when all arrive together");
    job_list_free(&jl);
    add(&jl, 1, 0, 2, 5); add(&jl, 2, 0, 2, 0); add(&jl, 3, 0, 2, 3);
    CHECK(sched_priority(&jl, &r) == SCHED_OK);
    CHECK(slice_is(&r, 0, 2, 0, 2) && slice_is(&r, 1, 3, 2, 4) && slice_is(&r, 2, 1, 4, 6));
    sched_result_free(&r);

    section("Priority: equal priorities -> arrival, then id (behaves like FCFS)");
    job_list_free(&jl);
    add(&jl, 4, 0, 1, 1); add(&jl, 2, 0, 1, 1); add(&jl, 3, 0, 1, 1); add(&jl, 1, 0, 1, 1);
    CHECK(sched_priority(&jl, &r) == SCHED_OK);
    CHECK(r.count == 4 && r.slices[0].job_id == 1 && r.slices[1].job_id == 2 &&
          r.slices[2].job_id == 3 && r.slices[3].job_id == 4);
    sched_result_free(&r);

    section("Priority: idle CPU");
    job_list_free(&jl);
    add(&jl, 1, 5, 2, 0); add(&jl, 2, 20, 1, 0);
    CHECK(sched_priority(&jl, &r) == SCHED_OK);
    CHECK(r.count == 4 && slice_is(&r, 0, GANTT_IDLE, 0, 5) && slice_is(&r, 1, 1, 5, 7) &&
          slice_is(&r, 2, GANTT_IDLE, 7, 20) && slice_is(&r, 3, 2, 20, 21));
    sched_result_free(&r);

    section("Priority: errors");
    job_list_free(&jl);
    CHECK(sched_priority(&jl, &r) == SCHED_ERR_EMPTY);
    CHECK(sched_priority(NULL, &r) == SCHED_ERR_NULL);
}

static void test_repeatability(void)
{
    section("Repeatability: algorithms can be re-run on the same list");
    job_list jl; job_list_init(&jl);
    sched_result a, b;
    add(&jl, 1, 0, 5, 1); add(&jl, 2, 2, 3, 0); add(&jl, 3, 4, 2, 2);
    CHECK(sched_round_robin(&jl, 2, &a) == SCHED_OK);
    CHECK(sched_fcfs(&jl, &b) == SCHED_OK);                  /* different algorithm in between */
    sched_result_free(&b);
    sched_result c;
    CHECK(sched_round_robin(&jl, 2, &c) == SCHED_OK);        /* same as the first run */
    CHECK(a.count == c.count && a.total_time == c.total_time);
    int same = a.count == c.count;
    for (size_t i = 0; same && i < a.count; i++)
        same = a.slices[i].job_id == c.slices[i].job_id && a.slices[i].start == c.slices[i].start &&
               a.slices[i].end == c.slices[i].end;
    CHECK(same);
    CHECK_NEAR(a.avg_waiting, c.avg_waiting);
    sched_result_free(&a);
    sched_result_free(&c);
    job_list_free(&jl);
}

/* ================= scheduler: randomised invariants ================= */

static unsigned long g_seed = 12345;
static int rnd(int lo, int hi)           /* deterministic LCG, inclusive range */
{
    g_seed = g_seed * 6364136223846793005UL + 1442695040888963407UL;
    return lo + (int)((g_seed >> 33) % (unsigned long)(hi - lo + 1));
}

/* Verify every property that must hold for ANY correct schedule. */
static int validate_schedule(const job_list *jl, const sched_result *r)
{
    int t = 0;
    int cpu[JOBS_MAX + 1];
    memset(cpu, 0, sizeof cpu);
    if (r->count == 0)
        return 0;

    for (size_t i = 0; i < r->count; i++) {                  /* contiguous from t=0, no overlap */
        const gantt_slice *s = &r->slices[i];
        if (s->start != t || s->end <= s->start)
            return 0;
        if (i > 0 && r->slices[i - 1].job_id == s->job_id)   /* merged */
            return 0;
        t = s->end;
        if (s->job_id != GANTT_IDLE) {
            const job_t *j = job_list_find(jl, s->job_id);
            if (!j || s->start < j->arrival)                 /* never runs before arrival */
                return 0;
            cpu[j - jl->items] += s->end - s->start;
        }
    }
    if (t != r->total_time)
        return 0;

    double sw = 0, st = 0;
    for (size_t i = 0; i < jl->count; i++) {
        const job_t *j = &jl->items[i];
        if (cpu[i] != j->burst)                              /* exactly `burst` CPU time */
            return 0;
        if (j->remaining != 0 || j->completion < j->arrival + j->burst)
            return 0;
        if (j->turnaround != j->completion - j->arrival)
            return 0;
        if (j->waiting != j->turnaround - j->burst || j->waiting < 0)
            return 0;
        sw += j->waiting;
        st += j->turnaround;
    }
    /* last slice must end at the latest completion */
    int maxc = 0;
    for (size_t i = 0; i < jl->count; i++)
        if (jl->items[i].completion > maxc)
            maxc = jl->items[i].completion;
    if (maxc != r->total_time)
        return 0;
    if (r->slices[r->count - 1].job_id == GANTT_IDLE)        /* never ends idle */
        return 0;
    return fabs(r->avg_waiting - sw / (double)jl->count) < 1e-9 &&
           fabs(r->avg_turnaround - st / (double)jl->count) < 1e-9;
}

static void test_random(void)
{
    section("Randomised invariants (500 job sets x 3 algorithms, incl. RR quanta 1..7)");
    int bad = 0;
    for (int iter = 0; iter < 500; iter++) {
        job_list jl; job_list_init(&jl);
        int n = rnd(1, 12);
        int maxarr = rnd(0, 1) ? 5 : 40;                    /* dense and sparse arrivals */
        for (int i = 0; i < n; i++)
            job_list_add(&jl, rnd(1, 1000) + i * 1000, rnd(0, maxarr), rnd(1, 9), rnd(0, 4));

        sched_result r;
        if (sched_fcfs(&jl, &r) != SCHED_OK || !validate_schedule(&jl, &r)) bad++;
        sched_result_free(&r);
        if (sched_priority(&jl, &r) != SCHED_OK || !validate_schedule(&jl, &r)) bad++;
        sched_result_free(&r);
        int q = 1 + iter % 7;
        if (sched_round_robin(&jl, q, &r) != SCHED_OK || !validate_schedule(&jl, &r)) bad++;
        sched_result_free(&r);
        job_list_free(&jl);
    }
    CHECK(bad == 0);
    if (bad)
        printf("  %d invalid schedules\n", bad);

    section("Extreme values do not overflow");
    job_list jl; job_list_init(&jl);
    for (int i = 1; i <= JOBS_MAX; i++)
        job_list_add(&jl, i, JOB_VALUE_MAX, JOB_VALUE_MAX, 0);
    sched_result r;
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    CHECK(r.total_time == JOB_VALUE_MAX + JOBS_MAX * JOB_VALUE_MAX);
    CHECK(validate_schedule(&jl, &r));
    sched_result_free(&r);
    CHECK(sched_round_robin(&jl, 999983, &r) == SCHED_OK);
    CHECK(validate_schedule(&jl, &r));
    sched_result_free(&r);
    job_list_free(&jl);
}

static void test_output(void)
{
    section("Gantt / report output");
    job_list jl; job_list_init(&jl);
    sched_result r;
    add(&jl, 1, 2, 3, 0); add(&jl, 12, 10, 2, 0);
    CHECK(sched_fcfs(&jl, &r) == SCHED_OK);
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (f) {
        sched_print_report(&jl, &r, f);
        rewind(f);
        char buf[2048] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        CHECK(strstr(buf, "FCFS") != NULL);
        CHECK(strstr(buf, "idle") != NULL);
        CHECK(strstr(buf, "P12") != NULL);
        CHECK(strstr(buf, "Average waiting time:    0.00") != NULL);
        CHECK(strstr(buf, "Average turnaround time: 2.50") != NULL);
        fclose(f);
    }
    sched_result_free(&r);

    /* A long timeline must wrap to several rows without crashing. */
    job_list_free(&jl);
    for (int i = 1; i <= 60; i++)
        add(&jl, i, 0, 3, 0);
    CHECK(sched_round_robin(&jl, 1, &r) == SCHED_OK);
    f = tmpfile();
    if (f) {
        sched_print_gantt(&r, f);
        rewind(f);
        char line[4096];
        int too_long = 0, rows = 0;
        while (fgets(line, sizeof line, f)) {
            rows++;
            if (strlen(line) > 100) too_long = 1;
        }
        CHECK(!too_long);
        CHECK(rows > 4);
        fclose(f);
    }
    sched_result_free(&r);
    f = tmpfile();                                          /* freed/empty result prints a note */
    if (f) {
        sched_print_gantt(&r, f);
        rewind(f);
        char buf[64] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        CHECK(strstr(buf, "empty") != NULL);
        fclose(f);
    }
    job_list_free(&jl);
}

/* ================= parser ================= */

static int parses_to(const char *line, int argc, const char *const *expect)
{
    parsed_cmd c;
    if (parse_line(line, &c) != PARSE_OK)
        return 0;
    int ok = c.argc == argc && c.argv[argc] == NULL;
    for (int i = 0; ok && i < argc; i++)
        ok = strcmp(c.argv[i], expect[i]) == 0;
    parsed_cmd_free(&c);
    return ok;
}

static void test_parser(void)
{
    section("parser: tokens");
    parsed_cmd c;
    CHECK(parse_line("", &c) == PARSE_EMPTY);
    CHECK(parse_line("   \t  ", &c) == PARSE_EMPTY);
    CHECK(parse_line("# just a comment", &c) == PARSE_EMPTY);
    { const char *e[] = {"ls"}; CHECK(parses_to("ls", 1, e)); }
    { const char *e[] = {"ls", "-l", "/tmp"}; CHECK(parses_to("  ls \t -l   /tmp  ", 3, e)); }
    { const char *e[] = {"echo", "a b", "c"}; CHECK(parses_to("echo \"a b\" c", 3, e)); }
    { const char *e[] = {"echo", "a b"}; CHECK(parses_to("echo 'a b'", 2, e)); }
    { const char *e[] = {"echo", "a  \"b\""}; CHECK(parses_to("echo \"a  \\\"b\\\"\"", 2, e)); }
    { const char *e[] = {"echo", "a b"}; CHECK(parses_to("echo a\\ b", 2, e)); }
    { const char *e[] = {"echo", "$HOME", "*", "|", ";"}; CHECK(parses_to("echo $HOME * | ;", 5, e)); }
    { const char *e[] = {"echo", ""}; CHECK(parses_to("echo ''", 2, e)); }
    { const char *e[] = {"echo", "ab"}; CHECK(parses_to("echo a\"\"b", 2, e)); }
    { const char *e[] = {"echo", "x#y"}; CHECK(parses_to("echo x#y", 2, e)); }
    { const char *e[] = {"echo", "hi"}; CHECK(parses_to("echo hi # comment", 2, e)); }
    { const char *e[] = {"echo", "it's"}; CHECK(parses_to("echo \"it's\"", 2, e)); }

    section("parser: errors");
    CHECK(parse_line("echo \"abc", &c) == PARSE_ERR_QUOTE);
    CHECK(c.argv == NULL && c.argc == 0);
    CHECK(parse_line("echo 'abc", &c) == PARSE_ERR_QUOTE);
    CHECK(parse_line("echo abc\\", &c) == PARSE_ERR_ESCAPE);

    char big[2048];
    big[0] = '\0';
    for (int i = 0; i < PARSE_MAX_ARGS; i++)
        strcat(big, "a ");
    CHECK(parse_line(big, &c) == PARSE_OK && c.argc == PARSE_MAX_ARGS);   /* boundary: exactly 64 */
    parsed_cmd_free(&c);
    strcat(big, "a");
    CHECK(parse_line(big, &c) == PARSE_ERR_TOO_MANY);                      /* 65 */

    parsed_cmd_free(&c);                                    /* freeing an empty cmd is safe */
    parsed_cmd_free(NULL);

    section("parser: parse_int_range");
    long v = 99;
    CHECK(parse_int_range("42", 0, 100, &v) == 0 && v == 42);
    CHECK(parse_int_range("-5", -10, 10, &v) == 0 && v == -5);
    CHECK(parse_int_range("+7", 0, 10, &v) == 0 && v == 7);
    CHECK(parse_int_range("0", 0, 0, &v) == 0 && v == 0);   /* boundaries inclusive */
    v = 99;
    CHECK(parse_int_range("", 0, 10, &v) == -1);
    CHECK(parse_int_range("abc", 0, 10, &v) == -1);
    CHECK(parse_int_range("12abc", 0, 100, &v) == -1);
    CHECK(parse_int_range("1.5", 0, 10, &v) == -1);
    CHECK(parse_int_range(" 5", 0, 10, &v) == -1);
    CHECK(parse_int_range("5 ", 0, 10, &v) == -1);
    CHECK(parse_int_range("11", 0, 10, &v) == -1);
    CHECK(parse_int_range("-1", 0, 10, &v) == -1);
    CHECK(parse_int_range("99999999999999999999999", 0, 10, &v) == -1);   /* overflow */
    CHECK(parse_int_range(NULL, 0, 10, &v) == -1);
    CHECK(v == 99);                                         /* untouched on failure */
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    test_jobs();
    test_fcfs();
    test_rr();
    test_priority();
    test_repeatability();
    test_random();
    test_output();
    test_parser();
    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
