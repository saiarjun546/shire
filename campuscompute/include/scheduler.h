/*
 * scheduler.h - FR2: CPU scheduling algorithms
 *
 * Owner: Person 1
 *
 * Algorithms: FCFS, Round Robin, non-preemptive Priority.
 * Every algorithm
 *   - first resets the results stored in the job list (so it can be run
 *     repeatedly and in any order on the same list),
 *   - simulates the CPU from time 0, recording IDLE slices when no job
 *     has arrived yet,
 *   - writes remaining/completion/turnaround/waiting back to each job,
 *   - fills a sched_result with the timeline and the two averages.
 *
 *   turnaround = completion - arrival
 *   waiting    = turnaround - burst
 *
 * CONVENTIONS (deterministic tie-breaking)
 *   * Ties on arrival time are broken by the SMALLER JOB ID, never by the
 *     order in which the jobs were typed.
 *   * Priority: SMALLER NUMBER = HIGHER PRIORITY. Among arrived jobs the
 *     order is (priority, arrival, id), all ascending.
 *   * Round Robin: a job that arrives at or before the instant a time
 *     slice ends is placed in the ready queue BEFORE the job that was just
 *     preempted. The preempted job then goes to the back of the queue.
 *   * Consecutive slices of the same job (or consecutive idle time) are
 *     merged in the timeline.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stddef.h>
#include <stdio.h>

#include "jobs.h"

#define GANTT_IDLE (-1)          /* job_id used for idle CPU time */

typedef struct {
    int job_id;                  /* job id, or GANTT_IDLE */
    int start;                   /* inclusive */
    int end;                     /* exclusive, end > start */
} gantt_slice;

typedef struct {
    const char  *algorithm;      /* static string, e.g. "FCFS" */
    gantt_slice *slices;         /* heap array */
    size_t       count;
    size_t       capacity;
    int          total_time;     /* time at which the last job finished */
    double       avg_waiting;
    double       avg_turnaround;
} sched_result;

typedef enum {
    SCHED_OK = 0,
    SCHED_ERR_NULL,          /* NULL argument */
    SCHED_ERR_EMPTY,         /* no jobs to schedule */
    SCHED_ERR_QUANTUM,       /* quantum outside 1..JOB_VALUE_MAX */
    SCHED_ERR_NOMEM
} sched_status;

void sched_result_init(sched_result *r);
void sched_result_free(sched_result *r);      /* safe to call twice */
const char *sched_status_str(sched_status st);

/* Each function returns SCHED_OK or an error; on error `res` holds an
 * empty result and the job results are left cleared. */
sched_status sched_fcfs(job_list *jobs, sched_result *res);
sched_status sched_round_robin(job_list *jobs, int quantum, sched_result *res);
sched_status sched_priority(job_list *jobs, sched_result *res);

/* Print the execution timeline (Gantt chart, wrapped to ~76 columns). */
void sched_print_gantt(const sched_result *res, FILE *out);

/* Print job table, averages and Gantt chart. */
void sched_print_report(const job_list *jobs, const sched_result *res, FILE *out);

#endif /* SCHEDULER_H */
