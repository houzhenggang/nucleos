/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __SERVERS_PM_PROTO_H
#define __SERVERS_PM_PROTO_H

/* Function prototypes. */
struct mproc;
struct stat;
struct mem_map;
struct memory;

#include <timers.h>

/* break.c */
_PROTOTYPE( int do_brk, (void)						);

/* devio.c */
_PROTOTYPE( int do_dev_io, (void) );
_PROTOTYPE( int do_dev_io, (void) );

/* dma.c */
_PROTOTYPE( int do_adddma, (void)					);
_PROTOTYPE( int do_deldma, (void)					);
_PROTOTYPE( int do_getdma, (void)					);
_PROTOTYPE( void release_dma, (endpoint_t proc_e, phys_clicks base,
						phys_clicks size)	);

/* dmp.c */
_PROTOTYPE( int do_fkey_pressed, (void)						);

/* exec.c */
_PROTOTYPE( int do_exec, (void)						);
_PROTOTYPE( int exec_newmem, (void)					);
_PROTOTYPE( int do_execrestart, (void)					);
_PROTOTYPE( void exec_restart, (struct mproc *rmp, int result)		);

/* forkexit.c */
_PROTOTYPE( int do_fork, (void)						);
_PROTOTYPE( int do_fork_nb, (void)					);
_PROTOTYPE( int do_exit, (void)						);
_PROTOTYPE( int do_waitpid, (void)					);
_PROTOTYPE( void exit_proc, (struct mproc *rmp, int exit_status,
	int exit_type)							);
_PROTOTYPE( void exit_restart, (struct mproc *rmp, int reply_type)	);

/* getset.c */
_PROTOTYPE( int do_getset, (void)					);

/* kputc.c */
_PROTOTYPE( void diag_repl, (void)					);

/* main.c */
_PROTOTYPE( int main, (void)						);

/* misc.c */
_PROTOTYPE( int do_reboot, (void)					);
_PROTOTYPE( int do_procstat, (void)					);
_PROTOTYPE( int do_sysuname, (void)					);
_PROTOTYPE( int do_getsysinfo, (void)					);
_PROTOTYPE( int do_getsysinfo_up, (void)					);
_PROTOTYPE( int do_getprocnr, (void)					);
_PROTOTYPE( int do_getpuid, (void)					);
_PROTOTYPE( int do_svrctl, (void)					);
_PROTOTYPE( int do_allocmem, (void)					);
_PROTOTYPE( int do_freemem, (void)					);
_PROTOTYPE( int do_getsetpriority, (void)					);

_PROTOTYPE( void setreply, (int proc_nr, int result)			);

/* profile.c */
_PROTOTYPE( int do_sprofile, (void)                                    );
_PROTOTYPE( int do_cprofile, (void)                                    );

/* signal.c */
_PROTOTYPE( int do_alarm, (void)					);
_PROTOTYPE( int do_kill, (void)						);
_PROTOTYPE( int ksig_pending, (void)					);
_PROTOTYPE( int do_pause, (void)					);
_PROTOTYPE( int set_alarm, (int proc_nr, int sec)			);
_PROTOTYPE( int check_sig, (pid_t proc_id, int signo)			);
_PROTOTYPE( void sig_proc, (struct mproc *rmp, int sig_nr)		);
_PROTOTYPE( int do_sigaction, (void)					);
_PROTOTYPE( int do_sigpending, (void)					);
_PROTOTYPE( int do_sigprocmask, (void)					);
_PROTOTYPE( int do_sigreturn, (void)					);
_PROTOTYPE( int do_sigsuspend, (void)					);
_PROTOTYPE( void check_pending, (struct mproc *rmp)			);

/* time.c */
_PROTOTYPE( int do_stime, (void)					);
_PROTOTYPE( int do_time, (void)						);
_PROTOTYPE( int do_times, (void)					);
_PROTOTYPE( int do_gettimeofday, (void)					);

/* timers.c */
_PROTOTYPE( void pm_set_timer, (timer_t *tp, int delta, 
	tmr_func_t watchdog, int arg));
_PROTOTYPE( void pm_expire_timers, (clock_t now));
_PROTOTYPE( void pm_cancel_timer, (timer_t *tp));

/* trace.c */
_PROTOTYPE( int do_trace, (void)					);
_PROTOTYPE( void stop_proc, (struct mproc *rmp, int sig_nr)		);

/* utility.c */
_PROTOTYPE( pid_t get_free_pid, (void)					);
_PROTOTYPE( int no_sys, (void)						);
_PROTOTYPE( void panic, (char *who, char *mess, int num)		);
_PROTOTYPE( char *find_param, (const char *key));
_PROTOTYPE( int proc_from_pid, (pid_t p));
_PROTOTYPE( int pm_isokendpt, (int ep, int *proc));

#endif /*  __SERVERS_PM_PROTO_H */
