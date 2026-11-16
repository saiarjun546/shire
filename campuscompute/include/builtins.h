/*
 * builtins.h - Shire Shell built-in commands
 *
 * Owner: Person 1
 *
 * Required built-ins: help clear pwd cd jobs schedule sysinfo version exit
 * Person 2 integration: log ipc banker process limit
 */
#ifndef BUILTINS_H
#define BUILTINS_H

#include "shell.h"

/* Returns 1 if `name` is a built-in command. */
int builtin_exists(const char *name);

/* Run built-in argv[0]. Returns its status (0 = success, non-zero = error).
 * Must only be called when builtin_exists(argv[0]) is true. */
int builtin_run(shell_state *sh, int argc, char **argv);

#endif /* BUILTINS_H */
