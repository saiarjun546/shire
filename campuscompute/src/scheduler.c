/*
 * scheduler.c - FR2: FCFS, Round Robin and non-preemptive Priority
 *
 * See scheduler.h for the tie-breaking conventions. All algorithms work on
 * an index array, so the caller's job order is never changed.
 *
 * Integer overflow: every input value is <= JOB_VALUE_MAX (1e6) and there
 * are at most JOBS_MAX (100) jobs, so the largest possible clock value is
 * 1e6 + 100 * 1e6, which fits comfortably in an int.
 */
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"

#define GANTT_LINE_WIDTH 76

/* ---------- result handling ---------- */

void sched_result_init(sched_result *r)
{
    if (r == NULL)
        return;
    r->algorithm = "";
    r->slices = NULL;
    r->count = 0;
    r->capacity = 0;
    r->total_time = 0;
    r->avg_waiting = 0.0;
    r->avg_turnaround = 0.0;
}

void sched_result_free(sched_result *r)
{
    if (r == NULL)
        return;
    free(r->slices);
    sched_result_init(r);
}

const char *sched_status_str(sched_status st)
{
    switch (st) {
    case SCHED_OK:          return "ok";
    case SCHED_ERR_NULL:    return "internal error: missing argument";
    case SCHED_ERR_EMPTY:   return "no jobs to schedule (add some with 'jobs add')";
    case SCHED_ERR_QUANTUM: return "time quantum must be an integer in 1..1000000";
    case SCHED_ERR_NOMEM:   return "out of memory";
    }
    return "unknown error";
}

/* Append [start,end) for job_id, merging with the previous slice when it
 * is the same job (or idle) and contiguous. */
static sched_status add_slice(sched_result *r, int job_id, int start, int end)
{
    if (end <= start)
        return SCHED_OK;
    if (r->count > 0) {
        gantt_slice *last = &r->slices[r->count - 1];
        if (last->job_id == job_id && last->end == start) {
            last->end = end;
            return SCHED_OK;
        }
    }
    if (r->count == r->capacity) {
        size_t ncap = r->capacity ? r->capacity * 2 : 16;
        gantt_slice *p = realloc(r->slices, ncap * sizeof *p);
        if (p == NULL)
            return SCHED_ERR_NOMEM;
        r->slices = p;
        r->capacity = ncap;
    }
    r->slices[r->count].job_id = job_id;
    r->slices[r->count].start = start;
    r->slices[r->count].end = end;
    r->count++;
    return SCHED_OK;
}

/* ---------- shared helpers ---------- */

typedef struct {
    int    arrival;
    int    id;
    size_t idx;
} arrival_key;

static int cmp_arrival_key(const void *a, const void *b)
{
    const arrival_key *x = a, *y = b;
    if (x->arrival != y->arrival)
        return x->arrival < y->arrival ? -1 : 1;
    if (x->id != y->id)
        return x->id < y->id ? -1 : 1;
    return 0;
}

/* Fill order[0..n) with job indices sorted by (arrival, id). */
static int make_arrival_order(const job_list *jl, size_t *order)
{
    arrival_key *keys = malloc(jl->count * sizeof *keys);
    if (keys == NULL)
        return -1;
    for (size_t i = 0; i < jl->count; i++) {
        keys[i].arrival = jl->items[i].arrival;
        keys[i].id = jl->items[i].id;
        keys[i].idx = i;
    }
    qsort(keys, jl->count, sizeof *keys, cmp_arrival_key);
    for (size_t i = 0; i < jl->count; i++)
        order[i] = keys[i].idx;
    free(keys);
    return 0;
}

static void job_finish(job_t *j, int time)
{
    j->remaining = 0;
    j->completion = time;
    j->turnaround = j->completion - j->arrival;
    j->waiting = j->turnaround - j->burst;
}

static void compute_averages(const job_list *jl, sched_result *r)
{
    double w = 0.0, t = 0.0;
    for (size_t i = 0; i < jl->count; i++) {
        w += jl->items[i].waiting;
        t += jl->items[i].turnaround;
    }
    r->avg_waiting = w / (double)jl->count;
    r->avg_turnaround = t / (double)jl->count;
}

/* Common entry checks + reset. Returns SCHED_OK when it is safe to run. */
static sched_status begin(job_list *jobs, sched_result *res, const char *name)
{
    if (jobs == NULL || res == NULL)
        return SCHED_ERR_NULL;
    sched_result_init(res);
    job_list_reset_results(jobs);
    if (jobs->count == 0)
        return SCHED_ERR_EMPTY;
    res->algorithm = name;
    return SCHED_OK;
}

/* Common exit: on failure leave an empty result and cleared job results. */
static sched_status end(job_list *jobs, sched_result *res, sched_status st, int total)
{
    if (st != SCHED_OK) {
        sched_result_free(res);
        job_list_reset_results(jobs);
        return st;
    }
    res->total_time = total;
    compute_averages(jobs, res);
    return SCHED_OK;
}

/* ---------- FCFS ---------- */

sched_status sched_fcfs(job_list *jobs, sched_result *res)
{
    sched_status st = begin(jobs, res, "FCFS");
    if (st != SCHED_OK)
        return st;

    size_t n = jobs->count;
    size_t *order = malloc(n * sizeof *order);
    if (order == NULL || make_arrival_order(jobs, order) != 0) {
        free(order);
        return end(jobs, res, SCHED_ERR_NOMEM, 0);
    }

    int time = 0;
    for (size_t k = 0; k < n && st == SCHED_OK; k++) {
        job_t *j = &jobs->items[order[k]];
        if (time < j->arrival) {                 /* CPU idle until arrival */
            st = add_slice(res, GANTT_IDLE, time, j->arrival);
            time = j->arrival;
        }
        if (st == SCHED_OK)
            st = add_slice(res, j->id, time, time + j->burst);
        time += j->burst;
        job_finish(j, time);
    }
    free(order);
    return end(jobs, res, st, time);
}

/* ---------- Non-preemptive Priority ---------- */

/* Returns 1 if job a should run before job b. */
static int priority_before(const job_t *a, const job_t *b)
{
    if (a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

sched_status sched_priority(job_list *jobs, sched_result *res)
{
    sched_status st = begin(jobs, res, "Priority (non-preemptive)");
    if (st != SCHED_OK)
        return st;

    size_t n = jobs->count;
    char *done = calloc(n, 1);
    if (done == NULL)
        return end(jobs, res, SCHED_ERR_NOMEM, 0);

    int time = 0;
    size_t finished = 0;
    while (finished < n && st == SCHED_OK) {
        const job_t *best = NULL;
        size_t best_i = 0;
        int next_arrival = -1;

        for (size_t i = 0; i < n; i++) {
            if (done[i])
                continue;
            const job_t *j = &jobs->items[i];
            if (j->arrival <= time) {
                if (best == NULL || priority_before(j, best)) {
                    best = j;
                    best_i = i;
                }
            } else if (next_arrival < 0 || j->arrival < next_arrival) {
                next_arrival = j->arrival;
            }
        }
        if (best == NULL) {                      /* nobody ready: CPU idles */
            st = add_slice(res, GANTT_IDLE, time, next_arrival);
            time = next_arrival;
            continue;                            /* no job finished this pass */
        }
        job_t *j = &jobs->items[best_i];
        st = add_slice(res, j->id, time, time + j->burst);
        time += j->burst;
        job_finish(j, time);
        done[best_i] = 1;
        finished++;
    }
    free(done);
    return end(jobs, res, st, time);
}

/* ---------- Round Robin ---------- */

sched_status sched_round_robin(job_list *jobs, int quantum, sched_result *res)
{
    sched_status st = begin(jobs, res, "Round Robin");
    if (st != SCHED_OK)
        return st;
    if (quantum < 1 || quantum > JOB_VALUE_MAX)
        return end(jobs, res, SCHED_ERR_QUANTUM, 0);

    size_t n = jobs->count;
    size_t *order = malloc(n * sizeof *order);
    size_t *queue = malloc(n * sizeof *queue);   /* circular; a job is queued at most once */
    if (order == NULL || queue == NULL || make_arrival_order(jobs, order) != 0) {
        free(order);
        free(queue);
        return end(jobs, res, SCHED_ERR_NOMEM, 0);
    }

    size_t head = 0, qlen = 0, next = 0, finished = 0;
    int time = 0;

    while (finished < n && st == SCHED_OK) {
        /* admit everything that has arrived by now */
        while (next < n && jobs->items[order[next]].arrival <= time)
            queue[(head + qlen++) % n] = order[next++];

        if (qlen == 0) {                         /* ready queue empty: idle */
            int arr = jobs->items[order[next]].arrival;
            st = add_slice(res, GANTT_IDLE, time, arr);
            time = arr;
            continue;
        }

        size_t cur = queue[head];
        head = (head + 1) % n;
        qlen--;
        job_t *j = &jobs->items[cur];

        int run = j->remaining < quantum ? j->remaining : quantum;
        st = add_slice(res, j->id, time, time + run);
        time += run;
        j->remaining -= run;

        /* arrivals up to and including the end of the slice go first ... */
        while (next < n && jobs->items[order[next]].arrival <= time)
            queue[(head + qlen++) % n] = order[next++];

        /* ... then the preempted job goes to the back */
        if (j->remaining > 0)
            queue[(head + qlen++) % n] = cur;
        else {
            job_finish(j, time);
            finished++;
        }
    }
    free(order);
    free(queue);
    return end(jobs, res, st, time);
}

/* ---------- output ---------- */

static int digits(int v)
{
    int d = 1;
    while (v >= 10) {
        v /= 10;
        d++;
    }
    return d;
}

static void slice_label(const gantt_slice *s, char *buf, size_t n)
{
    if (s->job_id == GANTT_IDLE)
        snprintf(buf, n, "idle");
    else
        snprintf(buf, n, "P%d", s->job_id);
}

/* Inner width of a Gantt cell: wide enough for the label and both times. */
static int cell_width(const gantt_slice *s)
{
    char label[24];
    slice_label(s, label, sizeof label);
    int w = (int)strlen(label) + 2;
    if (digits(s->start) > w)
        w = digits(s->start);
    if (digits(s->end) > w)
        w = digits(s->end);
    return w;
}

void sched_print_gantt(const sched_result *res, FILE *out)
{
    if (res == NULL || res->count == 0) {
        fputs("(empty timeline)\n", out);
        return;
    }
    size_t i = 0;
    while (i < res->count) {
        size_t j = i;
        int width = 1;
        while (j < res->count) {                 /* choose a row of cells */
            int w = cell_width(&res->slices[j]) + 1;
            if (j > i && width + w > GANTT_LINE_WIDTH)
                break;
            width += w;
            j++;
        }
        for (size_t k = i; k < j; k++) {         /* bar line */
            char label[24];
            int w = cell_width(&res->slices[k]);
            slice_label(&res->slices[k], label, sizeof label);
            int len = (int)strlen(label);
            int left = (w - len) / 2;
            fprintf(out, "|%*s%s%*s", left, "", label, w - len - left, "");
        }
        fputs("|\n", out);
        for (size_t k = i; k < j; k++)           /* time axis */
            fprintf(out, "%-*d", cell_width(&res->slices[k]) + 1, res->slices[k].start);
        fprintf(out, "%d\n", res->slices[j - 1].end);
        i = j;
        if (i < res->count)
            fputc('\n', out);
    }
}

void sched_print_report(const job_list *jobs, const sched_result *res, FILE *out)
{
    fprintf(out, "\n=== FR2: %s scheduling ===\n", res->algorithm);
    job_list_print(jobs, out);
    fprintf(out, "\nAverage waiting time:    %.2f\n", res->avg_waiting);
    fprintf(out, "Average turnaround time: %.2f\n", res->avg_turnaround);
    fprintf(out, "Total time:              %d\n", res->total_time);
    fputs("\nGantt chart:\n", out);
    sched_print_gantt(res, out);
}
