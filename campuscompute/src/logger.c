/*
 * logger.c - FR3: Concurrent activity logger
 *
 * Design
 * ------
 *  - bounded_queue is a circular buffer: front = next slot to read,
 *    rear = next slot to write, count = occupied slots.
 *  - One mutex protects every field of the queue.
 *  - not_full  : producers wait on this while count == capacity.
 *  - not_empty : consumers wait on this while count == 0.
 *  - Every wait is inside a while loop, because pthread_cond_wait() can
 *    wake up spuriously and another thread may have taken the slot first.
 *  - Shutdown: after all producers are joined, main sets shutdown = 1 and
 *    broadcasts. Consumers drain whatever is left and then exit, so no
 *    message is lost and no thread is left blocked forever.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

#define LOG_MSG_LEN 128

typedef struct {
    char messages[LOGGER_MAX_CAPACITY][LOG_MSG_LEN];
    int capacity;
    int front;
    int rear;
    int count;
    int shutdown;

    /* statistics used as evidence in the summary */
    int max_count_seen;
    long produced;
    long consumed;
    long producer_waits;   /* times a producer blocked because queue was full  */
    long consumer_waits;   /* times a consumer blocked because queue was empty */

    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} bounded_queue;

typedef struct {
    int id;
    int n_msgs;
    bounded_queue *q;
} producer_arg;

typedef struct {
    int id;
    int handled;
    bounded_queue *q;
    FILE *log;
    pthread_mutex_t *log_lock;
} consumer_arg;

/* ---------- small helpers ---------- */

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
}

static void timestamp(char *buf, size_t len)
{
    struct timespec now;
    struct tm tm_now;
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &tm_now);
    size_t n = strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_now);
    snprintf(buf + n, len - n, ".%03ld", now.tv_nsec / 1000000L);
}

/* ---------- bounded queue ---------- */

static int queue_init(bounded_queue *q, int capacity)
{
    memset(q, 0, sizeof(*q));
    q->capacity = capacity;
    if (pthread_mutex_init(&q->lock, NULL) != 0)
        return -1;
    if (pthread_cond_init(&q->not_full, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        return -1;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_cond_destroy(&q->not_full);
        pthread_mutex_destroy(&q->lock);
        return -1;
    }
    return 0;
}

static void queue_destroy(bounded_queue *q)
{
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->lock);
}

/* Blocks while the queue is full. Prints the action while holding the lock
 * so the printed occupancy is exactly the occupancy at that moment. */
static void queue_put(bounded_queue *q, const char *msg, int producer_id)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == q->capacity && !q->shutdown) {
        q->producer_waits++;
        printf("  [producer P%d] queue FULL (%d/%d) -> waiting\n",
               producer_id, q->count, q->capacity);
        pthread_cond_wait(&q->not_full, &q->lock);
    }

    snprintf(q->messages[q->rear], LOG_MSG_LEN, "%s", msg);
    q->rear = (q->rear + 1) % q->capacity;
    q->count++;
    q->produced++;
    if (q->count > q->max_count_seen)
        q->max_count_seen = q->count;

    printf("  [producer P%d] added   \"%s\"  (queue %d/%d)\n",
           producer_id, msg, q->count, q->capacity);

    pthread_cond_signal(&q->not_empty);   /* wake one waiting consumer */
    pthread_mutex_unlock(&q->lock);
}

/* Returns 0 and fills out_msg if a message was taken.
 * Returns 1 if shutdown was requested and the queue is empty. */
static int queue_get(bounded_queue *q, char *out_msg, int consumer_id)
{
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->shutdown) {
        q->consumer_waits++;
        pthread_cond_wait(&q->not_empty, &q->lock);
    }

    if (q->count == 0 && q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return 1;
    }

    snprintf(out_msg, LOG_MSG_LEN, "%s", q->messages[q->front]);
    q->front = (q->front + 1) % q->capacity;
    q->count--;
    q->consumed++;

    printf("  [consumer C%d] removed \"%s\"  (queue %d/%d)\n",
           consumer_id, out_msg, q->count, q->capacity);

    pthread_cond_signal(&q->not_full);    /* wake one waiting producer */
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static void queue_shutdown(bounded_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty); /* wake every sleeping consumer */
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

/* ---------- thread functions ---------- */

static void *producer_thread(void *arg)
{
    producer_arg *pa = arg;
    char msg[LOG_MSG_LEN];
    static const char *events[] = {
        "job submitted", "job started", "job finished", "cpu sample", "job queued"
    };

    for (int i = 1; i <= pa->n_msgs; i++) {
        snprintf(msg, sizeof(msg), "P%d#%d %s", pa->id, i,
                 events[(pa->id + i) % 5]);
        queue_put(pa->q, msg, pa->id);
        sleep_ms(10 + 7 * pa->id);   /* producers are fast */
    }
    return NULL;
}

static void *consumer_thread(void *arg)
{
    consumer_arg *ca = arg;
    char msg[LOG_MSG_LEN];
    char ts[40];

    while (queue_get(ca->q, msg, ca->id) == 0) {
        timestamp(ts, sizeof(ts));
        /* The FILE is shared by all consumers, so writes get their own lock. */
        pthread_mutex_lock(ca->log_lock);
        fprintf(ca->log, "[%s] [consumer C%d] %s\n", ts, ca->id, msg);
        fflush(ca->log);
        pthread_mutex_unlock(ca->log_lock);
        ca->handled++;
        sleep_ms(35);                /* consumers are slower -> queue fills up */
    }
    printf("  [consumer C%d] shutdown received, queue empty -> exiting "
           "(handled %d)\n", ca->id, ca->handled);
    return NULL;
}

/* ---------- public API ---------- */

int logger_run(int producers, int consumers, int msgs_per_producer,
               int capacity, const char *log_path)
{
    if (producers < 1 || producers > LOGGER_MAX_THREADS ||
        consumers < 1 || consumers > LOGGER_MAX_THREADS ||
        msgs_per_producer < 1 || msgs_per_producer > LOGGER_MAX_MSGS ||
        capacity < 1 || capacity > LOGGER_MAX_CAPACITY) {
        fprintf(stderr, "logger: invalid arguments (producers/consumers 1-%d, "
                "messages 1-%d, capacity 1-%d)\n",
                LOGGER_MAX_THREADS, LOGGER_MAX_MSGS, LOGGER_MAX_CAPACITY);
        return -1;
    }
    if (log_path == NULL)
        log_path = LOGGER_DEFAULT_FILE;

    bounded_queue q;
    pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_t ptid[LOGGER_MAX_THREADS], ctid[LOGGER_MAX_THREADS];
    producer_arg pargs[LOGGER_MAX_THREADS];
    consumer_arg cargs[LOGGER_MAX_THREADS];
    int p_started = 0, c_started = 0, rc = 0, err;
    char ts[40];

    FILE *log = fopen(log_path, "a");
    if (log == NULL) {
        perror("logger: fopen");
        return -1;
    }
    if (queue_init(&q, capacity) != 0) {
        fprintf(stderr, "logger: failed to initialise mutex/condition variables\n");
        fclose(log);
        return -1;
    }

    timestamp(ts, sizeof(ts));
    fprintf(log, "===== logger run started %s (producers=%d consumers=%d "
            "messages/producer=%d capacity=%d) =====\n",
            ts, producers, consumers, msgs_per_producer, capacity);
    fflush(log);

    printf("\n=== FR3: Producer-Consumer Logger ===\n");
    printf("producers=%d  consumers=%d  messages/producer=%d  queue capacity=%d\n",
           producers, consumers, msgs_per_producer, capacity);
    printf("log file: %s\n\n", log_path);

    /* Start consumers first so they are already waiting on not_empty. */
    for (int i = 0; i < consumers; i++) {
        cargs[i] = (consumer_arg){ .id = i + 1, .handled = 0, .q = &q,
                                   .log = log, .log_lock = &log_lock };
        if ((err = pthread_create(&ctid[i], NULL, consumer_thread, &cargs[i])) != 0) {
            fprintf(stderr, "logger: pthread_create(consumer): %s\n", strerror(err));
            rc = -1;
            break;
        }
        c_started++;
    }
    for (int i = 0; rc == 0 && i < producers; i++) {
        pargs[i] = (producer_arg){ .id = i + 1, .n_msgs = msgs_per_producer, .q = &q };
        if ((err = pthread_create(&ptid[i], NULL, producer_thread, &pargs[i])) != 0) {
            fprintf(stderr, "logger: pthread_create(producer): %s\n", strerror(err));
            rc = -1;
            break;
        }
        p_started++;
    }

    /* Orderly termination: producers finish -> signal shutdown -> consumers drain. */
    for (int i = 0; i < p_started; i++) {
        pthread_join(ptid[i], NULL);
        printf("  [main] joined producer P%d\n", i + 1);
    }
    printf("  [main] all producers done -> sending shutdown signal to consumers\n");
    queue_shutdown(&q);
    for (int i = 0; i < c_started; i++) {
        pthread_join(ctid[i], NULL);
        printf("  [main] joined consumer C%d\n", i + 1);
    }

    timestamp(ts, sizeof(ts));
    fprintf(log, "===== logger run finished %s (produced=%ld consumed=%ld) =====\n",
            ts, q.produced, q.consumed);

    printf("\n--- Logger summary ---\n");
    printf("messages produced      : %ld\n", q.produced);
    printf("messages consumed      : %ld\n", q.consumed);
    printf("max queue occupancy    : %d (capacity %d) -> %s\n", q.max_count_seen,
           q.capacity, q.max_count_seen <= q.capacity ? "never exceeded" : "EXCEEDED");
    printf("producer waits (full)  : %ld\n", q.producer_waits);
    printf("consumer waits (empty) : %ld\n", q.consumer_waits);
    for (int i = 0; i < c_started; i++)
        printf("consumer C%d handled    : %d\n", cargs[i].id, cargs[i].handled);
    printf("threads joined         : %d producers, %d consumers\n", p_started, c_started);
    printf("mutex/condvars         : destroyed\n");
    printf("log entries appended to: %s\n", log_path);

    if (q.produced != q.consumed)
        rc = -1;

    queue_destroy(&q);
    pthread_mutex_destroy(&log_lock);
    if (fclose(log) != 0) {
        perror("logger: fclose");
        rc = -1;
    }
    return rc;
}

int logger_demo(void)
{
    return logger_run(3, 2, 5, 4, LOGGER_DEFAULT_FILE);
}
