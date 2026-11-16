/*
 * ipc.h - FR4: Inter-process communication using POSIX shared memory
 *
 * Owner: Person 2
 *
 * A writer process and a reader process exchange messages through a
 * POSIX shared-memory object (shm_open + ftruncate + mmap). Two named
 * POSIX semaphores (sem_open) coordinate them:
 *   slot_empty (starts at 1) - writer may write
 *   slot_full  (starts at 0) - reader may read
 * All objects created by the program are removed before returning.
 */
#ifndef IPC_H
#define IPC_H

#define IPC_MAX_MESSAGES 50

/* Exchange n_messages messages (1..IPC_MAX_MESSAGES).
 * Returns 0 on success, -1 on error. */
int ipc_run(int n_messages);

/* Default demo: 5 messages. */
int ipc_demo(void);

#endif /* IPC_H */
