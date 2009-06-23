/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __SERVERS_PM_GLO_H
#define __SERVERS_PM_GLO_H

/* EXTERN should be extern except in table.c */
#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

/* Global variables. */
EXTERN struct mproc *mp;	/* ptr to 'mproc' slot of current process */
EXTERN int procs_in_use;	/* how many processes are marked as IN_USE */
EXTERN char monitor_params[128*sizeof(char *)];	/* boot monitor parameters */
EXTERN struct kinfo kinfo;	/* kernel information */

/* Misc.c */
extern struct utsname uts_val;	/* uname info */

/* The parameters of the call are kept here. */
EXTERN message m_in;		/* the incoming message itself is kept here. */
EXTERN int who_p, who_e;	/* caller's proc number, endpoint */
EXTERN int call_nr;		/* system call number */

extern _PROTOTYPE (int (*call_vec[]), (void) );	/* system call handlers */
extern char core_name[];	/* file name where core images are produced */
EXTERN sigset_t core_sset;	/* which signals cause core images */
EXTERN sigset_t ign_sset;	/* which signals are by default ignored */

EXTERN time_t boottime;		/* time when the system was booted (for
				 * reporting to FS)
				 */
EXTERN u32_t system_hz;		/* System clock frequency. */
EXTERN int report_reboot;	/* During reboot to report to FS that we are 
				 * rebooting.
				 */
EXTERN int abort_flag;
EXTERN char monitor_code[256];
		
#endif /*  __SERVERS_PM_GLO_H */
