/*
 * jobs.c - FR1: Job management (see jobs.h for the data model)
 */
#include <stdlib.h>

#include "jobs.h"

void job_list_init(job_list *jl)
{
    if (jl == NULL)
        return;
    jl->items = NULL;
    jl->count = 0;
    jl->capacity = 0;
}

void job_list_free(job_list *jl)
{
    if (jl == NULL)
        return;
    free(jl->items);
    job_list_init(jl);
}

const char *job_status_str(job_status st)
{
    switch (st) {
    case JOB_OK:            return "ok";
    case JOB_ERR_NULL:      return "internal error: no job list";
    case JOB_ERR_ID:        return "job id must be an integer in 1..1000000";
    case JOB_ERR_ARRIVAL:   return "arrival time must be an integer in 0..1000000";
    case JOB_ERR_BURST:     return "burst time must be an integer in 1..1000000";
    case JOB_ERR_PRIORITY:  return "priority must be an integer in 0..1000000";
    case JOB_ERR_DUPLICATE: return "a job with this id already exists";
    case JOB_ERR_FULL:      return "job table is full (maximum 100 jobs)";
    case JOB_ERR_NOMEM:     return "out of memory";
    }
    return "unknown error";
}

const job_t *job_list_find(const job_list *jl, int id)
{
    if (jl == NULL)
        return NULL;
    for (size_t i = 0; i < jl->count; i++)
        if (jl->items[i].id == id)
            return &jl->items[i];
    return NULL;
}

job_status job_list_add(job_list *jl, int id, int arrival, int burst, int priority)
{
    if (jl == NULL)
        return JOB_ERR_NULL;
    if (id < 1 || id > JOB_VALUE_MAX)
        return JOB_ERR_ID;
    if (arrival < 0 || arrival > JOB_VALUE_MAX)
        return JOB_ERR_ARRIVAL;
    if (burst < 1 || burst > JOB_VALUE_MAX)
        return JOB_ERR_BURST;
    if (priority < 0 || priority > JOB_VALUE_MAX)
        return JOB_ERR_PRIORITY;
    if (job_list_find(jl, id) != NULL)
        return JOB_ERR_DUPLICATE;
    if (jl->count >= JOBS_MAX)
        return JOB_ERR_FULL;

    if (jl->count == jl->capacity) {
        size_t ncap = jl->capacity ? jl->capacity * 2 : 8;
        if (ncap > JOBS_MAX)
            ncap = JOBS_MAX;
        job_t *p = realloc(jl->items, ncap * sizeof *p);
        if (p == NULL)
            return JOB_ERR_NOMEM;       /* old block is still valid */
        jl->items = p;
        jl->capacity = ncap;
    }

    job_t *j = &jl->items[jl->count++];
    j->id = id;
    j->arrival = arrival;
    j->burst = burst;
    j->priority = priority;
    j->remaining = burst;
    j->completion = JOB_UNSET;
    j->turnaround = JOB_UNSET;
    j->waiting = JOB_UNSET;
    return JOB_OK;
}

void job_list_reset_results(job_list *jl)
{
    if (jl == NULL)
        return;
    for (size_t i = 0; i < jl->count; i++) {
        job_t *j = &jl->items[i];
        j->remaining = j->burst;         /* burst itself is never touched */
        j->completion = JOB_UNSET;
        j->turnaround = JOB_UNSET;
        j->waiting = JOB_UNSET;
    }
}

void job_list_print(const job_list *jl, FILE *out)
{
    if (jl == NULL || jl->count == 0) {
        fputs("No jobs defined. Add one with: jobs add <id> <arrival> <burst> <priority>\n", out);
        return;
    }
    fprintf(out, "%-8s %8s %8s %8s %10s %11s %11s %8s\n",
            "Job", "Arrival", "Burst", "Priority", "Remaining",
            "Completion", "Turnaround", "Waiting");
    for (size_t i = 0; i < jl->count; i++) {
        const job_t *j = &jl->items[i];
        char comp[16] = "-", tat[16] = "-", wt[16] = "-";
        if (j->completion != JOB_UNSET) {
            snprintf(comp, sizeof comp, "%d", j->completion);
            snprintf(tat, sizeof tat, "%d", j->turnaround);
            snprintf(wt, sizeof wt, "%d", j->waiting);
        }
        char name[16];
        snprintf(name, sizeof name, "P%d", j->id);
        fprintf(out, "%-8s %8d %8d %8d %10d %11s %11s %8s\n",
                name, j->arrival, j->burst, j->priority, j->remaining, comp, tat, wt);
    }
}
