/*
 * ipc.c - FR4: Shared-memory IPC between cooperating processes
 *
 * Flow
 * ----
 *  parent : creates shm object + 2 named semaphores, then forks
 *  writer : wait(slot_empty) -> write message -> post(slot_full)   (x N)
 *           finally writes a "done" marker the same way
 *  reader : wait(slot_full)  -> read message  -> post(slot_empty)
 *           stops when it sees the "done" marker
 *  parent : waitpid() both children, then munmap/close/sem_close and
 *           shm_unlink/sem_unlink, and verifies the names are gone.
 *
 * Object names include the parent PID so that two runs never collide and
 * we never touch IPC objects that belong to anybody else.
 */
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc.h"

#define IPC_NAME_LEN 64
#define IPC_TEXT_LEN 128

/* Layout of the shared-memory region. */
typedef struct {
    int seq;                    /* message number                    */
    int done;                   /* 1 = writer has no more messages   */
    pid_t writer_pid;
    char text[IPC_TEXT_LEN];
} shared_slot;

/* sem_wait that retries if interrupted by a signal. */
static int sem_wait_retry(sem_t *s)
{
    while (sem_wait(s) == -1) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

static void writer_process(shared_slot *slot, sem_t *empty, sem_t *full, int n)
{
    for (int i = 1; i <= n; i++) {
        if (sem_wait_retry(empty) == -1) {
            perror("writer: sem_wait");
            _exit(2);
        }
        slot->seq = i;
        slot->done = 0;
        slot->writer_pid = getpid();
        snprintf(slot->text, IPC_TEXT_LEN, "job J%02d result=%d (from writer)",
                 i, i * i * 10);
        printf("  [writer PID %d] wrote   #%d: \"%s\"\n", getpid(), i, slot->text);
        fflush(stdout);
        if (sem_post(full) == -1) {
            perror("writer: sem_post");
            _exit(2);
        }
    }
    /* end-of-stream marker */
    if (sem_wait_retry(empty) == -1)
        _exit(2);
    slot->done = 1;
    sem_post(full);
    printf("  [writer PID %d] sent end-of-stream marker, exiting\n", getpid());
    fflush(stdout);
    _exit(0);
}

static void reader_process(shared_slot *slot, sem_t *empty, sem_t *full, int expected)
{
    int received = 0;
    for (;;) {
        if (sem_wait_retry(full) == -1) {
            perror("reader: sem_wait");
            _exit(2);
        }
        if (slot->done) {
            sem_post(empty);
            break;
        }
        printf("  [reader PID %d] read    #%d: \"%s\" (writer PID %d)\n",
               getpid(), slot->seq, slot->text, slot->writer_pid);
        fflush(stdout);
        received++;
        if (sem_post(empty) == -1) {
            perror("reader: sem_post");
            _exit(2);
        }
    }
    printf("  [reader PID %d] received %d/%d messages, exiting\n",
           getpid(), received, expected);
    fflush(stdout);
    _exit(received == expected ? 0 : 1);
}

/* Report how a child ended. Returns 0 only for a normal exit with status 0. */
static int report_child(const char *role, pid_t pid, int status)
{
    if (WIFEXITED(status)) {
        printf("  [parent] %s PID %d exited with status %d\n",
               role, pid, WEXITSTATUS(status));
        return WEXITSTATUS(status) == 0 ? 0 : -1;
    }
    if (WIFSIGNALED(status))
        printf("  [parent] %s PID %d killed by signal %d (%s)\n",
               role, pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
    return -1;
}

int ipc_run(int n_messages)
{
    if (n_messages < 1 || n_messages > IPC_MAX_MESSAGES) {
        fprintf(stderr, "ipc: message count must be 1-%d\n", IPC_MAX_MESSAGES);
        return -1;
    }

    char shm_name[IPC_NAME_LEN], empty_name[IPC_NAME_LEN], full_name[IPC_NAME_LEN];
    pid_t me = getpid();
    snprintf(shm_name, sizeof(shm_name), "/cc_shm_%d", (int)me);
    snprintf(empty_name, sizeof(empty_name), "/cc_sem_empty_%d", (int)me);
    snprintf(full_name, sizeof(full_name), "/cc_sem_full_%d", (int)me);

    int shm_fd = -1, rc = 0;
    shared_slot *slot = MAP_FAILED;
    sem_t *sem_empty = SEM_FAILED, *sem_full = SEM_FAILED;
    pid_t writer = -1, reader = -1;

    printf("\n=== FR4: Shared-Memory IPC ===\n");

    /* 1. create shared memory object (O_EXCL: fail rather than reuse someone else's) */
    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1) {
        perror("ipc: shm_open");
        return -1;
    }
    printf("  [parent PID %d] created shared memory  %s\n", (int)me, shm_name);

    /* 2. size it */
    if (ftruncate(shm_fd, sizeof(shared_slot)) == -1) {
        perror("ipc: ftruncate");
        rc = -1;
        goto cleanup;
    }
    /* 3. map it */
    slot = mmap(NULL, sizeof(shared_slot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (slot == MAP_FAILED) {
        perror("ipc: mmap");
        rc = -1;
        goto cleanup;
    }
    memset(slot, 0, sizeof(*slot));
    printf("  [parent] sized to %zu bytes and mapped at %p\n", sizeof(shared_slot), (void *)slot);

    /* 4. named semaphores */
    sem_empty = sem_open(empty_name, O_CREAT | O_EXCL, 0600, 1);
    if (sem_empty == SEM_FAILED) {
        perror("ipc: sem_open(empty)");
        rc = -1;
        goto cleanup;
    }
    sem_full = sem_open(full_name, O_CREAT | O_EXCL, 0600, 0);
    if (sem_full == SEM_FAILED) {
        perror("ipc: sem_open(full)");
        rc = -1;
        goto cleanup;
    }
    printf("  [parent] created semaphores %s (=1) and %s (=0)\n\n", empty_name, full_name);

    /* 5. fork the two cooperating processes */
    fflush(stdout);                       /* avoid duplicated stdio buffers in children */
    writer = fork();
    if (writer == -1) {
        perror("ipc: fork(writer)");
        rc = -1;
        goto cleanup;
    }
    if (writer == 0)
        writer_process(slot, sem_empty, sem_full, n_messages);

    reader = fork();
    if (reader == -1) {
        perror("ipc: fork(reader)");
        kill(writer, SIGTERM);            /* writer would block forever without a reader */
        waitpid(writer, NULL, 0);
        writer = -1;
        rc = -1;
        goto cleanup;
    }
    if (reader == 0)
        reader_process(slot, sem_empty, sem_full, n_messages);

    /* 6. collect both children */
    int st;
    if (waitpid(writer, &st, 0) == -1 || report_child("writer", writer, st) != 0)
        rc = -1;
    if (waitpid(reader, &st, 0) == -1 || report_child("reader", reader, st) != 0)
        rc = -1;

cleanup:
    printf("\n  [parent] cleaning up IPC resources created by this run...\n");
    if (slot != MAP_FAILED && munmap(slot, sizeof(shared_slot)) == -1)
        perror("ipc: munmap");
    if (shm_fd != -1) {
        close(shm_fd);
        if (shm_unlink(shm_name) == -1)
            perror("ipc: shm_unlink");
        else
            printf("    munmap + close + shm_unlink(%s)     OK\n", shm_name);
    }
    if (sem_empty != SEM_FAILED) {
        sem_close(sem_empty);
        if (sem_unlink(empty_name) == -1)
            perror("ipc: sem_unlink(empty)");
        else
            printf("    sem_close + sem_unlink(%s)  OK\n", empty_name);
    }
    if (sem_full != SEM_FAILED) {
        sem_close(sem_full);
        if (sem_unlink(full_name) == -1)
            perror("ipc: sem_unlink(full)");
        else
            printf("    sem_close + sem_unlink(%s)   OK\n", full_name);
    }

    /* 7. prove the names no longer exist */
    int fd = shm_open(shm_name, O_RDONLY, 0);
    sem_t *s = sem_open(empty_name, 0);
    sem_t *s2 = sem_open(full_name, 0);
    int gone = (fd == -1 && s == SEM_FAILED && s2 == SEM_FAILED);
    if (fd != -1) close(fd);
    if (s != SEM_FAILED) sem_close(s);
    if (s2 != SEM_FAILED) sem_close(s2);
    printf("  [parent] verification: shared memory and semaphores %s\n",
           gone ? "no longer exist (cleanup confirmed)" : "STILL EXIST");
    if (!gone)
        rc = -1;

    printf("  IPC result: %s\n", rc == 0 ? "SUCCESS" : "FAILED");
    return rc;
}

int ipc_demo(void)
{
    return ipc_run(5);
}
