/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file contains the table used to map system call numbers onto the
 * routines that perform them.
 */
#include "pm.h"
#include <nucleos/unistd.h>
#include <nucleos/signal.h>
#include <servers/pm/mproc.h>
#include "param.h"

/* Global variables. */
struct mproc *mp;        /* ptr to 'mproc' slot of current process */
int procs_in_use;        /* how many processes are marked as IN_USE */
char monitor_params[128*sizeof(char *)]; /* boot monitor parameters */
struct kinfo kinfo;      /* kernel information */

/* The parameters of the call are kept here. */
message m_in;            /* the incoming message itself is kept here. */
int who_p, who_e;        /* caller's proc number, endpoint */
int call_nr;             /* system call number */
sigset_t core_sset;      /* which signals cause core images */
sigset_t ign_sset;       /* which signals are by default ignored */
time_t boottime;         /* time when the system was booted (for reporting to FS_PROC_NR) */
u32_t system_hz;         /* System clock frequency. */
int report_reboot;       /* During reboot to report to FS_PROC_NR that we are rebooting. */
int abort_flag;
char monitor_code[256];

#define SCALL_HANDLER(syscall, handler) \
	[ __NNR_ ## syscall ] = handler

int (*call_vec[])(void) = {
	no_sys,		/*  0 = unused	*/
	do_exit,	/*  1 = exit	*/
	do_fork,	/*  2 = fork	*/
	no_sys,		/*  3 = read	*/
	no_sys,		/*  4 = write	*/
	no_sys,		/*  5 = open	*/
	no_sys,		/*  6 = close	*/
	do_waitpid,	/*  7 = wait	*/
	no_sys,		/*  8 = creat	*/
	no_sys,		/*  9 = link	*/
	no_sys,		/* 10 = unlink	*/
	do_waitpid,	/* 11 = waitpid	*/
	no_sys,		/* 12 = chdir	*/
	do_time,	/* 13 = time	*/
	no_sys,		/* 14 = mknod	*/
	no_sys,		/* 15 = chmod	*/
	no_sys,		/* 16 = chown	*/
	do_brk,		/* 17 = break	*/
	no_sys,		/* 18 = stat	*/
	no_sys,		/* 19 = lseek	*/
	do_get,		/* 20 = getpid	*/
	no_sys,		/* 21 = mount	*/
	no_sys,		/* 22 = umount	*/
	do_set,		/* 23 = setuid	*/
	do_get,		/* 24 = getuid	*/
	do_stime,	/* 25 = stime	*/
	do_trace,	/* 26 = ptrace	*/
	do_alarm,	/* 27 = alarm	*/
	no_sys,		/* 28 = fstat	*/
	do_pause,	/* 29 = pause	*/
	no_sys,		/* 30 = utime	*/
	no_sys,		/* 31 = (stty)	*/
	no_sys,		/* 32 = (gtty)	*/
	no_sys,		/* 33 = access	*/
	no_sys,		/* 34 = (nice)	*/
	no_sys,		/* 35 = (ftime)	*/
	no_sys,		/* 36 = sync	*/
	do_kill,	/* 37 = kill	*/
	no_sys,		/* 38 = rename	*/
	no_sys,		/* 39 = mkdir	*/
	no_sys,		/* 40 = rmdir	*/
	no_sys,		/* 41 = dup	*/
	no_sys,		/* 42 = pipe	*/
	do_times,	/* 43 = times	*/
	no_sys,		/* 44 = (prof)	*/
	no_sys,		/* 45 = unused	*/
	do_set,		/* 46 = setgid	*/
	do_get,		/* 47 = getgid	*/
	no_sys,		/* 48 = (signal)*/
	no_sys,		/* 49 = unused	*/
	no_sys,		/* 50 = unused	*/
	no_sys,		/* 51 = (acct)	*/
	no_sys,		/* 52 = (phys)	*/
	no_sys,		/* 53 = (lock)	*/
	no_sys,		/* 54 = ioctl	*/
	no_sys,		/* 55 = fcntl	*/
	no_sys,		/* 56 = (mpx)	*/
	no_sys,		/* 57 = unused	*/
	no_sys,		/* 58 = unused	*/
	do_exec,	/* 59 = execve	*/
	no_sys,		/* 60 = umask	*/
	no_sys,		/* 61 = chroot	*/
	do_set,		/* 62 = setsid	*/
	do_get,		/* 63 = getpgrp	*/
	do_itimer,	/* 64 = itimer */
	no_sys,		/* 65 = unused	*/
	no_sys, 	/* 66 = unused  */
	no_sys,		/* 67 = unused	*/
	no_sys,		/* 68 = unused  */
	no_sys,		/* 69 = unused	*/
	no_sys,		/* 70 = unused	*/
	do_sigaction,	/* 71 = sigaction   */
	do_sigsuspend,	/* 72 = sigsuspend  */
	do_sigpending,	/* 73 = sigpending  */
	do_sigprocmask,	/* 74 = sigprocmask */
	do_sigreturn,	/* 75 = sigreturn   */
	do_reboot,	/* 76 = reboot	*/
	do_svrctl,	/* 77 = svrctl	*/
	do_sysuname,	/* 78 = sysuname */
	do_getsysinfo,	/* 79 = getsysinfo */
	no_sys,		/* 80 = (getdents) */
	no_sys, 	/* 81 = unused */
	no_sys, 	/* 82 = (fstatfs) */
	no_sys, 	/* 83 = unused */
	no_sys, 	/* 84 = unused */
	no_sys,		/* 85 = (select) */
	no_sys,		/* 86 = (fchdir) */
	no_sys,		/* 87 = (fsync) */
	do_getsetpriority,	/* 88 = getpriority */
	do_getsetpriority,	/* 89 = setpriority */
	do_time,	/* 90 = gettimeofday */
	do_set,		/* 91 = seteuid	*/
	do_set,		/* 92 = setegid	*/
	no_sys,		/* 93 = (truncate) */
	no_sys,		/* 94 = (ftruncate) */
	no_sys,		/* 95 = (fchmod) */
	no_sys,		/* 96 = (fchown) */
	do_getsysinfo_up,/* 97 = getsysinfo_up */
	do_sprofile,	/* 98 = sprofile */
	do_cprofile,	/* 99 = cprofile */
	no_sys,		/* 100 = unused */
	no_sys,		/* 101 = unused */
	no_sys,		/* 102 = unused */
	no_sys,		/* 103 = unused */
	no_sys,		/* 104 = unused */
	no_sys,		/* 105 = unused */
	no_sys,		/* 106 = unused */
	no_sys,		/* 107 = unused */
	do_adddma,	/* 108 = adddma */
	do_deldma,	/* 109 = deldma */
	do_getdma,	/* 110 = getdma */

	/* Nucleos syscalls */
	SCALL_HANDLER(alarm,		do_alarm),
	SCALL_HANDLER(brk,		sys_brk),
	SCALL_HANDLER(cprof,		do_cprofile),
	SCALL_HANDLER(exec,		do_exec),
	SCALL_HANDLER(exit,		do_exit),
	SCALL_HANDLER(fork,		do_fork),
	SCALL_HANDLER(getegid,		sys_getegid),
	SCALL_HANDLER(geteuid,		sys_geteuid),
	SCALL_HANDLER(getgid,		sys_getgid),
	SCALL_HANDLER(getitimer,	sys_getitimer),
	SCALL_HANDLER(getpgrp,		sys_getpgrp),
	SCALL_HANDLER(getpid,		sys_getpid),
	SCALL_HANDLER(getppid,		sys_getppid),
	SCALL_HANDLER(getpriority,	sys_getpriority),
	SCALL_HANDLER(gettimeofday,	sys_gettimeofday),
	SCALL_HANDLER(getuid,		sys_getuid),
	SCALL_HANDLER(kill,		do_kill),
	SCALL_HANDLER(pause,		do_pause),
	SCALL_HANDLER(ptrace,		no_sys),
	SCALL_HANDLER(reboot,		do_reboot),
	SCALL_HANDLER(setegid,		sys_setegid),
	SCALL_HANDLER(seteuid,		sys_seteuid),
	SCALL_HANDLER(setgid,		sys_setgid),
	SCALL_HANDLER(setitimer,	sys_setitimer),
	SCALL_HANDLER(setpriority,	sys_setpriority),
	SCALL_HANDLER(setsid,		sys_setsid),
	SCALL_HANDLER(setuid,		sys_setuid),
	SCALL_HANDLER(sigaction,	do_sigaction),
	SCALL_HANDLER(signal,		no_sys),	/* n/a: implemented via sigaction */
	SCALL_HANDLER(sigpending,	sys_sigpending),
	SCALL_HANDLER(sigprocmask,	sys_sigprocmask),
	SCALL_HANDLER(sigreturn,	scall_sigreturn),
	SCALL_HANDLER(sigsuspend,	sys_sigsuspend),
	SCALL_HANDLER(sprof,		do_sprofile),
	SCALL_HANDLER(stime,		scall_stime),
	SCALL_HANDLER(uname,		sys_uname),
	SCALL_HANDLER(time,		sys_time),
	SCALL_HANDLER(times,		scall_times),
	SCALL_HANDLER(wait,		do_waitpid),
	SCALL_HANDLER(waitpid,		do_waitpid),
};
/* This should not fail with "array size is negative": */
extern int dummy[sizeof(call_vec) <= NR_syscalls * sizeof(call_vec[0]) ? 1 : -1];
