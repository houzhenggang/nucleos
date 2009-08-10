/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
#include <nucleos/types.h>
#include <stdlib.h>
#include <signal.h>
#include <nucleos/limits.h>

extern pid_t _fork(void);
extern pid_t _wait(int *);
extern void _exit(int);
extern void _execve(const char *path, const char ** argv, const char ** envp);
extern int _close(int);

#define	FAIL	127

extern const char ***_penviron;
static const char *exec_tab[] = {
	"sh",			/* argv[0] */
	"-c",			/* argument to the shell */
	NULL,			/* to be filled with user command */
	NULL			/* terminating NULL */
	};

int
system(const char *str)
{
	int pid, exitstatus, waitval;
	int i;

	if ((pid = _fork()) < 0) return str ? -1 : 0;

	if (pid == 0) {
		for (i = 3; i <= OPEN_MAX; i++)
			_close(i);
		if (!str) str = "cd .";		/* just testing for a shell */
		exec_tab[2] = str;		/* fill in command */
		_execve("/bin/sh", exec_tab, *_penviron);
		/* get here if execve fails ... */
		_exit(FAIL);	/* see manual page */
	}
	while ((waitval = _wait(&exitstatus)) != pid) {
		if (waitval == -1) break;
	}
	if (waitval == -1) {
		/* no child ??? or maybe interrupted ??? */
		exitstatus = -1;
	}
	if (!str) {
		if (exitstatus == FAIL << 8)		/* execve() failed */
			exitstatus = 0;
		else exitstatus = 1;			/* /bin/sh exists */
	}
	return exitstatus;
}
