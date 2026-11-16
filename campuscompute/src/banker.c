/*
 * banker.c - FR5: Banker's safety algorithm
 *
 * Safety algorithm
 * ----------------
 *  1. Work = Available, Finish[i] = false for all i
 *  2. Find i with Finish[i] == false and Need[i] <= Work (every resource)
 *  3. Pretend Pi runs to completion: Work += Allocation[i], Finish[i] = true
 *  4. Repeat 2-3 until no such i exists
 *  5. Safe  <=> every Finish[i] is true; the order chosen is a safe sequence
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "banker.h"

/* ---------- helpers ---------- */

static void print_vec(const int *v, int n)
{
    printf("[");
    for (int j = 0; j < n; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf("]");
}

static int vec_leq(const int *a, const int *b, int n)
{
    for (int j = 0; j < n; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

/* ---------- validation ---------- */

int banker_validate(banker_state *s, char *err, size_t errlen)
{
    if (s->n_proc < 1 || s->n_proc > BANKER_MAX_P) {
        snprintf(err, errlen, "number of processes must be 1-%d (got %d)",
                 BANKER_MAX_P, s->n_proc);
        return -1;
    }
    if (s->n_res < 1 || s->n_res > BANKER_MAX_R) {
        snprintf(err, errlen, "number of resource types must be 1-%d (got %d)",
                 BANKER_MAX_R, s->n_res);
        return -1;
    }
    for (int j = 0; j < s->n_res; j++) {
        if (s->available[j] < 0) {
            snprintf(err, errlen, "Available[R%d] is negative (%d)", j, s->available[j]);
            return -1;
        }
    }
    for (int i = 0; i < s->n_proc; i++) {
        for (int j = 0; j < s->n_res; j++) {
            if (s->maximum[i][j] < 0 || s->allocation[i][j] < 0) {
                snprintf(err, errlen, "negative value for P%d, R%d", i, j);
                return -1;
            }
            if (s->allocation[i][j] > s->maximum[i][j]) {
                snprintf(err, errlen,
                         "Allocation > Maximum for P%d, R%d (%d > %d): a process "
                         "cannot hold more than it declared", i, j,
                         s->allocation[i][j], s->maximum[i][j]);
                return -1;
            }
            s->need[i][j] = s->maximum[i][j] - s->allocation[i][j];
        }
    }
    return 0;
}

/* ---------- core algorithm ---------- */

int banker_is_safe(const banker_state *s, int safe_seq[], int finished[])
{
    int work[BANKER_MAX_R];
    int finish[BANKER_MAX_P] = {0};
    int count = 0;

    memcpy(work, s->available, sizeof(int) * s->n_res);

    int progress = 1;
    while (count < s->n_proc && progress) {
        progress = 0;
        for (int i = 0; i < s->n_proc; i++) {
            if (!finish[i] && vec_leq(s->need[i], work, s->n_res)) {
                for (int j = 0; j < s->n_res; j++)
                    work[j] += s->allocation[i][j];
                finish[i] = 1;
                safe_seq[count++] = i;
                progress = 1;
            }
        }
    }
    if (finished)
        memcpy(finished, finish, sizeof(int) * s->n_proc);
    return count == s->n_proc;
}

/* ---------- printing ---------- */

void banker_print_state(const banker_state *s)
{
    int w = s->n_res * 3 + 2;
    printf("  %-5s %-*s %-*s %-*s\n", "Proc", w, "Allocation", w, "Maximum", w, "Need");
    for (int i = 0; i < s->n_proc; i++) {
        printf("  P%-4d ", i);
        for (int k = 0; k < 3; k++) {
            const int *row = k == 0 ? s->allocation[i] : k == 1 ? s->maximum[i] : s->need[i];
            for (int j = 0; j < s->n_res; j++)
                printf("%3d", row[j]);
            printf("  ");
        }
        printf("\n");
    }
    printf("  Available: ");
    print_vec(s->available, s->n_res);
    printf("\n");
}

/* Same algorithm as banker_is_safe(), but prints every decision. */
int banker_check_safety(banker_state *s)
{
    char err[200];
    if (banker_validate(s, err, sizeof(err)) != 0) {
        printf("  INVALID STATE: %s\n", err);
        return -1;
    }

    printf("  Processes: %d   Resource types: %d\n", s->n_proc, s->n_res);
    printf("  Need = Maximum - Allocation\n\n");
    banker_print_state(s);
    printf("\n  Tie-breaking: scan P0..P%d in order, pick the first that fits; repeat.\n\n",
           s->n_proc - 1);

    int work[BANKER_MAX_R], finish[BANKER_MAX_P] = {0}, seq[BANKER_MAX_P], count = 0;
    memcpy(work, s->available, sizeof(int) * s->n_res);
    printf("  Work = Available = ");
    print_vec(work, s->n_res);
    printf("\n");

    int pass = 0, progress = 1;
    while (count < s->n_proc && progress) {
        progress = 0;
        printf("  -- pass %d --\n", ++pass);
        for (int i = 0; i < s->n_proc; i++) {
            if (finish[i])
                continue;
            printf("    P%d: Need ", i);
            print_vec(s->need[i], s->n_res);
            if (vec_leq(s->need[i], work, s->n_res)) {
                printf(" <= Work ");
                print_vec(work, s->n_res);
                for (int j = 0; j < s->n_res; j++)
                    work[j] += s->allocation[i][j];
                finish[i] = 1;
                seq[count++] = i;
                progress = 1;
                printf("  -> P%d can finish, releases ", i);
                print_vec(s->allocation[i], s->n_res);
                printf(", Work = ");
                print_vec(work, s->n_res);
                printf("\n");
            } else {
                printf(" >  Work ");
                print_vec(work, s->n_res);
                printf("  -> must wait\n");
            }
        }
    }

    /* Cross-check against the non-printing implementation. */
    int seq2[BANKER_MAX_P];
    int safe = banker_is_safe(s, seq2, NULL);
    if (safe != (count == s->n_proc))
        fprintf(stderr, "  internal error: trace and algorithm disagree\n");

    printf("\n");
    if (count == s->n_proc) {
        printf("  RESULT: SAFE state. Safe sequence: < ");
        for (int k = 0; k < count; k++)
            printf("P%d%s", seq[k], k + 1 < count ? ", " : " ");
        printf(">\n");
        return 1;
    }
    printf("  RESULT: UNSAFE state. No process among { ");
    for (int i = 0; i < s->n_proc; i++)
        if (!finish[i])
            printf("P%d ", i);
    printf("} can get its full Need with Work = ");
    print_vec(work, s->n_res);
    printf(".\n  These processes could deadlock if they all request their maximum.\n");
    return 0;
}

/* ---------- file loading ---------- */

/* Read the next integer, skipping whitespace and '#' comments.
 * Returns 1 on success, 0 at EOF, -1 on a non-numeric token. */
static int next_int(FILE *fp, int *out)
{
    int c;
    for (;;) {
        c = fgetc(fp);
        if (c == EOF)
            return 0;
        if (c == '#') {
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;
            continue;
        }
        if (!isspace(c))
            break;
    }
    ungetc(c, fp);
    if (fscanf(fp, "%d", out) != 1)
        return -1;
    return 1;
}

static int read_value(FILE *fp, int *out, const char *what, const char *path)
{
    int r = next_int(fp, out);
    if (r == 1)
        return 0;
    fprintf(stderr, "banker: %s: %s while reading %s\n", path,
            r == 0 ? "unexpected end of file" : "non-numeric value", what);
    return -1;
}

int banker_load_file(const char *path, banker_state *s)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        perror(path);
        return -1;
    }
    memset(s, 0, sizeof(*s));
    char what[64];
    int rc = -1;

    if (read_value(fp, &s->n_proc, "number of processes", path) ||
        read_value(fp, &s->n_res, "number of resources", path))
        goto out;
    if (s->n_proc < 1 || s->n_proc > BANKER_MAX_P || s->n_res < 1 || s->n_res > BANKER_MAX_R) {
        fprintf(stderr, "banker: %s: need 1-%d processes and 1-%d resources (got %d, %d)\n",
                path, BANKER_MAX_P, BANKER_MAX_R, s->n_proc, s->n_res);
        goto out;
    }
    for (int j = 0; j < s->n_res; j++) {
        snprintf(what, sizeof(what), "Available[R%d]", j);
        if (read_value(fp, &s->available[j], what, path))
            goto out;
    }
    for (int i = 0; i < s->n_proc; i++)
        for (int j = 0; j < s->n_res; j++) {
            snprintf(what, sizeof(what), "Maximum[P%d][R%d]", i, j);
            if (read_value(fp, &s->maximum[i][j], what, path))
                goto out;
        }
    for (int i = 0; i < s->n_proc; i++)
        for (int j = 0; j < s->n_res; j++) {
            snprintf(what, sizeof(what), "Allocation[P%d][R%d]", i, j);
            if (read_value(fp, &s->allocation[i][j], what, path))
                goto out;
        }
    int extra;
    if (next_int(fp, &extra) != 0) {
        fprintf(stderr, "banker: %s: extra data after the Allocation matrix\n", path);
        goto out;
    }
    rc = 0;
out:
    fclose(fp);
    return rc;
}

/* ---------- built-in test cases ---------- */

/* Classic textbook state: 5 processes, 3 resource types (A, B, C). */
static void load_classic(banker_state *s)
{
    static const int max[5][3]   = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};
    static const int alloc[5][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    memset(s, 0, sizeof(*s));
    s->n_proc = 5;
    s->n_res = 3;
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) {
            s->maximum[i][j] = max[i][j];
            s->allocation[i][j] = alloc[i][j];
        }
}

int banker_demo_safe(void)
{
    banker_state s;
    load_classic(&s);
    s.available[0] = 3; s.available[1] = 3; s.available[2] = 2;
    printf("\n=== FR5: Banker's Algorithm - test case 1 (expected SAFE) ===\n");
    return banker_check_safety(&s);
}

int banker_demo_unsafe(void)
{
    banker_state s;
    load_classic(&s);
    /* Same processes, but only [0 1 1] free: P3 and P1 can finish,
     * then P0, P2 and P4 are stuck. */
    s.available[0] = 0; s.available[1] = 1; s.available[2] = 1;
    printf("\n=== FR5: Banker's Algorithm - test case 2 (expected UNSAFE) ===\n");
    return banker_check_safety(&s);
}
