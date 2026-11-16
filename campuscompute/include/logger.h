/*
 * logger.h - FR3: Concurrent activity logger (bounded producer-consumer queue)
 *
 * Owner: Person 2
 *
 * Producer threads generate log events and push them into a fixed-capacity
 * queue. Consumer threads pop events and write them to a log file.
 * Shared queue state is protected by a pthread mutex; "queue full" and
 * "queue empty" are coordinated with two condition variables.
 */
#ifndef LOGGER_H
#define LOGGER_H

#define LOGGER_DEFAULT_FILE      "campuscompute.log"
#define LOGGER_MAX_CAPACITY      64
#define LOGGER_MAX_THREADS       16
#define LOGGER_MAX_MSGS          1000

/*
 * Run the logger with custom settings.
 * Returns 0 on success, -1 on invalid arguments or a system-call failure.
 */
int logger_run(int producers, int consumers, int msgs_per_producer,
               int capacity, const char *log_path);

/* Default demo: 3 producers, 2 consumers, 5 messages each, capacity 4. */
int logger_demo(void);

#endif /* LOGGER_H */
