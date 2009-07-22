/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file handles signals, which are asynchronous events and are generally
 * a messy and unpleasant business.  Signals can be generated by the KILL
 * system call, or from the keyboard (SIGINT) or from the clock (SIGALRM).
 * In all cases control eventually passes to check_sig() to see which processes
 * can be signaled.  The actual signaling is done by sig_proc().
 *
 * The entry points into this file are:
 *   do_sigaction:   perform the SIGACTION system call
 *   do_sigpending:  perform the SIGPENDING system call
 *   do_sigprocmask: perform the SIGPROCMASK system call
 *   do_sigreturn:   perform the SIGRETURN system call
 *   do_sigsuspend:  perform the SIGSUSPEND system call
 *   do_kill:	perform the KILL system call
 *   do_alarm:	perform the ALARM system call by calling set_alarm()
 *   set_alarm:	tell the clock task to start or stop a timer
 *   do_pause:	perform the PAUSE system call
 *   ksig_pending: the kernel notified about pending signals
 *   sig_proc:	interrupt or terminate a signaled process
 *   check_sig: check which processes to signal with sig_proc()
 *   check_pending:  check if a pending signal can now be delivered
 */
#include <nucleos/nucleos.h>
#include "pm.h"
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <nucleos/callnr.h>
#include <nucleos/endpoint.h>
#include <nucleos/com.h>
#include <nucleos/vm.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/sigcontext.h>
#include <string.h>
#include "mproc.h"
#include "param.h"

FORWARD _PROTOTYPE( void unpause, (int pro, int for_trace)		);
FORWARD _PROTOTYPE( void handle_ksig, (int proc_nr, sigset_t sig_map)	);
FORWARD _PROTOTYPE( void cause_sigalrm, (struct timer *tp)		);

/*===========================================================================*
 *				do_sigaction				     *
 *===========================================================================*/
PUBLIC int do_sigaction()
{
  int r;
  struct sigaction svec;
  struct sigaction *svp;

  if (m_in.sig_nr == SIGKILL) return(OK);
  if (m_in.sig_nr < 1 || m_in.sig_nr > _NSIG) return (EINVAL);
  svp = &mp->mp_sigact[m_in.sig_nr];
  if ((struct sigaction *) m_in.sig_osa != (struct sigaction *) NULL) {
	r = sys_datacopy(PM_PROC_NR,(vir_bytes) svp,
		who_e, (vir_bytes) m_in.sig_osa, (phys_bytes) sizeof(svec));
	if (r != OK) return(r);
  }

  if ((struct sigaction *) m_in.sig_nsa == (struct sigaction *) NULL) 
  	return(OK);

  /* Read in the sigaction structure. */
  r = sys_datacopy(who_e, (vir_bytes) m_in.sig_nsa,
		PM_PROC_NR, (vir_bytes) &svec, (phys_bytes) sizeof(svec));
  if (r != OK) return(r);

  if (svec.sa_handler == SIG_IGN) {
	sigaddset(&mp->mp_ignore, m_in.sig_nr);
	sigdelset(&mp->mp_sigpending, m_in.sig_nr);
	sigdelset(&mp->mp_catch, m_in.sig_nr);
	sigdelset(&mp->mp_sig2mess, m_in.sig_nr);
  } else if (svec.sa_handler == SIG_DFL) {
	sigdelset(&mp->mp_ignore, m_in.sig_nr);
	sigdelset(&mp->mp_catch, m_in.sig_nr);
	sigdelset(&mp->mp_sig2mess, m_in.sig_nr);
  } else if (svec.sa_handler == SIG_MESS) {
	if (! (mp->mp_flags & PRIV_PROC)) return(EPERM);
	sigdelset(&mp->mp_ignore, m_in.sig_nr);
	sigaddset(&mp->mp_sig2mess, m_in.sig_nr);
	sigdelset(&mp->mp_catch, m_in.sig_nr);
  } else {
	sigdelset(&mp->mp_ignore, m_in.sig_nr);
	sigaddset(&mp->mp_catch, m_in.sig_nr);
	sigdelset(&mp->mp_sig2mess, m_in.sig_nr);
  }
  mp->mp_sigact[m_in.sig_nr].sa_handler = svec.sa_handler;
  sigdelset(&svec.sa_mask, SIGKILL);
  mp->mp_sigact[m_in.sig_nr].sa_mask = svec.sa_mask;
  mp->mp_sigact[m_in.sig_nr].sa_flags = svec.sa_flags;
  mp->mp_sigreturn = (vir_bytes) m_in.sig_ret;
  return(OK);
}

/*===========================================================================*
 *				do_sigpending                                *
 *===========================================================================*/
PUBLIC int do_sigpending()
{
  mp->mp_reply.reply_mask = (long) mp->mp_sigpending;
  return OK;
}

/*===========================================================================*
 *				do_sigprocmask                               *
 *===========================================================================*/
PUBLIC int do_sigprocmask()
{
/* Note that the library interface passes the actual mask in sigmask_set,
 * not a pointer to the mask, in order to save a copy.  Similarly,
 * the old mask is placed in the return message which the library
 * interface copies (if requested) to the user specified address.
 *
 * The library interface must set SIG_INQUIRE if the 'act' argument
 * is NULL.
 *
 * KILL and STOP can't be masked.
 */

  int i;

  mp->mp_reply.reply_mask = (long) mp->mp_sigmask;

  switch (m_in.sig_how) {
      case SIG_BLOCK:
	sigdelset((sigset_t *)&m_in.sig_set, SIGKILL);
	sigdelset((sigset_t *)&m_in.sig_set, SIGSTOP);
	for (i = 1; i <= _NSIG; i++) {
		if (sigismember((sigset_t *)&m_in.sig_set, i))
			sigaddset(&mp->mp_sigmask, i);
	}
	break;

      case SIG_UNBLOCK:
	for (i = 1; i <= _NSIG; i++) {
		if (sigismember((sigset_t *)&m_in.sig_set, i))
			sigdelset(&mp->mp_sigmask, i);
	}
	check_pending(mp);
	break;

      case SIG_SETMASK:
	sigdelset((sigset_t *) &m_in.sig_set, SIGKILL);
	sigdelset((sigset_t *) &m_in.sig_set, SIGSTOP);
	mp->mp_sigmask = (sigset_t) m_in.sig_set;
	check_pending(mp);
	break;

      case SIG_INQUIRE:
	break;

      default:
	return(EINVAL);
	break;
  }
  return OK;
}

/*===========================================================================*
 *				do_sigsuspend                                *
 *===========================================================================*/
PUBLIC int do_sigsuspend()
{
  mp->mp_sigmask2 = mp->mp_sigmask;	/* save the old mask */
  mp->mp_sigmask = (sigset_t) m_in.sig_set;
  sigdelset(&mp->mp_sigmask, SIGKILL);
  mp->mp_flags |= SIGSUSPENDED;
  check_pending(mp);
  return(SUSPEND);
}

/*===========================================================================*
 *				do_sigreturn				     *
 *===========================================================================*/
PUBLIC int do_sigreturn()
{
/* A user signal handler is done.  Restore context and check for
 * pending unblocked signals.
 */

  int r;

  mp->mp_sigmask = (sigset_t) m_in.sig_set;
  sigdelset(&mp->mp_sigmask, SIGKILL);

  r = sys_sigreturn(who_e, (struct sigmsg *) m_in.sig_context);
  check_pending(mp);
  return(r);
}

/*===========================================================================*
 *				do_kill					     *
 *===========================================================================*/
PUBLIC int do_kill()
{
/* Perform the kill(pid, signo) system call. */

  return check_sig(m_in.pid, m_in.sig_nr);
}

/*===========================================================================*
 *				ksig_pending				     *
 *===========================================================================*/
PUBLIC int ksig_pending()
{
/* Certain signals, such as segmentation violations originate in the kernel.
 * When the kernel detects such signals, it notifies the PM to take further 
 * action. The PM requests the kernel to send messages with the process
 * slot and bit map for all signaled processes. The File System, for example,
 * uses this mechanism to signal writing on broken pipes (SIGPIPE). 
 *
 * The kernel has notified the PM about pending signals. Request pending
 * signals until all signals are handled. If there are no more signals,
 * NONE is returned in the process number field.
 */ 
 int proc_nr_e;
 sigset_t sig_map;

 while (TRUE) {
   int r;
   /* get an arbitrary pending signal */
   if((r=sys_getksig(&proc_nr_e, &sig_map)) != OK)
  	panic(__FILE__,"sys_getksig failed", r);
   if (NONE == proc_nr_e) {		/* stop if no more pending signals */
 	break;
   } else {
 	int proc_nr_p;
 	if(pm_isokendpt(proc_nr_e, &proc_nr_p) != OK)
  		panic(__FILE__,"sys_getksig strange process", proc_nr_e);
   	handle_ksig(proc_nr_e, sig_map);	/* handle the received signal */
	/* If the process still exists to the kernel after the signal
	 * has been handled ...
	 */
        if ((mproc[proc_nr_p].mp_flags & (IN_USE | EXITING)) == IN_USE)
	{
	   if((r=sys_endksig(proc_nr_e)) != OK)	/* ... tell kernel it's done */
  		panic(__FILE__,"sys_endksig failed", r);
	}
   }
 } 
 return(SUSPEND);			/* prevents sending reply */
}

/*===========================================================================*
 *				handle_ksig				     *
 *===========================================================================*/
PRIVATE void handle_ksig(proc_nr_e, sig_map)
int proc_nr_e;
sigset_t sig_map;
{
  register struct mproc *rmp;
  int i, proc_nr;
  pid_t proc_id, id;

  if(pm_isokendpt(proc_nr_e, &proc_nr) != OK || proc_nr < 0) {
	printf("PM: handle_ksig: %d?? not ok\n", proc_nr_e);
	return;
  }
  rmp = &mproc[proc_nr];
  if ((rmp->mp_flags & (IN_USE | EXITING)) != IN_USE) {
	printf("PM: handle_ksig: %d?? exiting / not in use\n", proc_nr_e);
	return;
  }
  proc_id = rmp->mp_pid;
  mp = &mproc[0];			/* pretend signals are from PM */
  mp->mp_procgrp = rmp->mp_procgrp;	/* get process group right */

  /* Check each bit in turn to see if a signal is to be sent.  Unlike
   * kill(), the kernel may collect several unrelated signals for a
   * process and pass them to PM in one blow.  Thus loop on the bit
   * map. For SIGINT, SIGWINCH and SIGQUIT, use proc_id 0 to indicate
   * a broadcast to the recipient's process group.  For SIGKILL, use
   * proc_id -1 to indicate a systemwide broadcast.
   */
  for (i = 1; i <= _NSIG; i++) {
	if (!sigismember(&sig_map, i)) continue;
#if 0
	printf("PM: sig %d for %d from kernel\n", 
		i, proc_nr_e);
#endif
	switch (i) {
	    case SIGINT:
	    case SIGQUIT:
	    case SIGWINCH:
		id = 0; break;	/* broadcast to process group */
	    default:
		id = proc_id;
		break;
	}
	check_sig(id, i);
  }
}

/*===========================================================================*
 *				do_alarm				     *
 *===========================================================================*/
PUBLIC int do_alarm()
{
/* Perform the alarm(seconds) system call. */
  return(set_alarm(who_e, m_in.seconds));
}

/*===========================================================================*
 *				set_alarm				     *
 *===========================================================================*/
PUBLIC int set_alarm(proc_nr_e, sec)
int proc_nr_e;			/* process that wants the alarm */
int sec;			/* how many seconds delay before the signal */
{
/* This routine is used by do_alarm() to set the alarm timer.  It is also used
 * to turn the timer off when a process exits with the timer still on.
 */
  clock_t ticks;	/* number of ticks for alarm */
  clock_t exptime;	/* needed for remaining time on previous alarm */
  clock_t uptime;	/* current system time */
  int remaining;	/* previous time left in seconds */
  int s;
  int proc_nr_n;

  if(pm_isokendpt(proc_nr_e, &proc_nr_n) != OK)
	return EINVAL;

  /* First determine remaining time of previous alarm, if set. */
  if (mproc[proc_nr_n].mp_flags & ALARM_ON) {
  	if ( (s=getuptime(&uptime)) != OK) 
  		panic(__FILE__,"set_alarm couldn't get uptime", s);
  	exptime = *tmr_exp_time(&mproc[proc_nr_n].mp_timer);
  	remaining = (int) ((exptime - uptime + (system_hz-1))/system_hz);
  	if (remaining < 0) remaining = 0;	
  } else {
  	remaining = 0; 
  }

  /* Tell the clock task to provide a signal message when the time comes.
   *
   * Large delays cause a lot of problems.  First, the alarm system call
   * takes an unsigned seconds count and the library has cast it to an int.
   * That probably works, but on return the library will convert "negative"
   * unsigneds to errors.  Presumably no one checks for these errors, so
   * force this call through.  Second, If unsigned and long have the same
   * size, converting from seconds to ticks can easily overflow.  Finally,
   * the kernel has similar overflow bugs adding ticks.
   *
   * Fixing this requires a lot of ugly casts to fit the wrong interface
   * types and to avoid overflow traps.  ALRM_EXP_TIME has the right type
   * (clock_t) although it is declared as long.  How can variables like
   * this be declared properly without combinatorial explosion of message
   * types?
   */
  ticks = (clock_t) (system_hz * (unsigned long) (unsigned) sec);
  if ( (unsigned long) ticks / system_hz != (unsigned) sec)
	ticks = LONG_MAX;	/* eternity (really TMR_NEVER) */

  if (ticks != 0) {
  	pm_set_timer(&mproc[proc_nr_n].mp_timer, ticks,
		cause_sigalrm, proc_nr_e);
  	mproc[proc_nr_n].mp_flags |=  ALARM_ON;
  } else if (mproc[proc_nr_n].mp_flags & ALARM_ON) {
  	pm_cancel_timer(&mproc[proc_nr_n].mp_timer);
  	mproc[proc_nr_n].mp_flags &= ~ALARM_ON;
  }
  return(remaining);
}

/*===========================================================================*
 *				cause_sigalrm				     *
 *===========================================================================*/
PRIVATE void cause_sigalrm(tp)
struct timer *tp;
{
  int proc_nr_n;
  register struct mproc *rmp;

  /* get process from timer */
  if(pm_isokendpt(tmr_arg(tp)->ta_int, &proc_nr_n) != OK) {
	printf("PM: ignoring timer for invalid endpoint %d\n",
		tmr_arg(tp)->ta_int);
	return;
  }

  rmp = &mproc[proc_nr_n];

  if ((rmp->mp_flags & (IN_USE | EXITING)) != IN_USE) return;
  if ((rmp->mp_flags & ALARM_ON) == 0) return;
  rmp->mp_flags &= ~ALARM_ON;
  check_sig(rmp->mp_pid, SIGALRM);
}

/*===========================================================================*
 *				do_pause				     *
 *===========================================================================*/
PUBLIC int do_pause()
{
/* Perform the pause() system call. */

  mp->mp_flags |= PAUSED;
  return(SUSPEND);
}

/*===========================================================================*
 *				sig_proc				     *
 *===========================================================================*/
PUBLIC void sig_proc(rmp, signo)
register struct mproc *rmp;	/* pointer to the process to be signaled */
int signo;			/* signal to send to process (1 to _NSIG) */
{
/* Send a signal to a process.  Check to see if the signal is to be caught,
 * ignored, tranformed into a message (for system processes) or blocked.  
 *  - If the signal is to be transformed into a message, request the KERNEL to
 * send the target process a system notification with the pending signal as an 
 * argument. 
 *  - If the signal is to be caught, request the KERNEL to push a sigcontext 
 * structure and a sigframe structure onto the catcher's stack.  Also, KERNEL 
 * will reset the program counter and stack pointer, so that when the process 
 * next runs, it will be executing the signal handler. When the signal handler 
 * returns,  sigreturn(2) will be called.  Then KERNEL will restore the signal 
 * context from the sigcontext structure.
 * If there is insufficient stack space, kill the process.
 */

  vir_bytes cur_sp;
  int s;
  int slot;
  int sigflags;

  slot = (int) (rmp - mproc);
  if ((rmp->mp_flags & (IN_USE | EXITING)) != IN_USE) {
	printf("PM: signal %d sent to exiting process %d\n", signo, slot);
	panic(__FILE__,"", NO_NUM);
  }
  if (rmp->mp_fs_call != PM_IDLE || rmp->mp_fs_call2 != PM_IDLE)
  {
	sigaddset(&rmp->mp_sigpending, signo);
	rmp->mp_flags |= PM_SIG_PENDING;
	/* keep the process from running */
	sys_nice(rmp->mp_endpoint, PRIO_STOP);
	return;
		
  }
  if ((rmp->mp_flags & TRACED) && signo != SIGKILL) {
	/* A traced process has special handling. */
	unpause(slot, TRUE /*for_trace*/);
	stop_proc(rmp, signo);	/* a signal causes it to stop */
	return;
  }
  /* Some signals are ignored by default. */
  if (sigismember(&rmp->mp_ignore, signo)) { 
  	return;
  }
  if (sigismember(&rmp->mp_sigmask, signo)) {
	/* Signal should be blocked. */
	sigaddset(&rmp->mp_sigpending, signo);
	return;
  }
  sigflags = rmp->mp_sigact[signo].sa_flags;
  if (sigismember(&rmp->mp_catch, signo)) {
	if (rmp->mp_flags & SIGSUSPENDED)
		rmp->mp_sigmsg.sm_mask = rmp->mp_sigmask2;
	else
		rmp->mp_sigmsg.sm_mask = rmp->mp_sigmask;
	rmp->mp_sigmsg.sm_signo = signo;
	rmp->mp_sigmsg.sm_sighandler =
		(vir_bytes) rmp->mp_sigact[signo].sa_handler;
	rmp->mp_sigmsg.sm_sigreturn = rmp->mp_sigreturn;
	rmp->mp_sigmask |= rmp->mp_sigact[signo].sa_mask;

	if (sigflags & SA_NODEFER)
		sigdelset(&rmp->mp_sigmask, signo);
	else
		sigaddset(&rmp->mp_sigmask, signo);

	if (sigflags & SA_RESETHAND) {
		sigdelset(&rmp->mp_catch, signo);
		rmp->mp_sigact[signo].sa_handler = SIG_DFL;
	}
	sigdelset(&rmp->mp_sigpending, signo);

	/* Stop process from running before we fiddle with its stack. */
	sys_nice(rmp->mp_endpoint, PRIO_STOP);
	if(vm_push_sig(rmp->mp_endpoint, &cur_sp) != OK)
		goto doterminate;

        rmp->mp_sigmsg.sm_stkptr = cur_sp;

	/* Check to see if process is hanging on a PAUSE, WAIT or SIGSUSPEND
	 * call.
	 */
	if (rmp->mp_flags & (PAUSED | WAITING | SIGSUSPENDED)) {
		rmp->mp_flags &= ~(PAUSED | WAITING | SIGSUSPENDED);
		setreply(slot, EINTR);

		/* Ask the kernel to deliver the signal */
		s= sys_sigsend(rmp->mp_endpoint, &rmp->mp_sigmsg);
		if (s != OK)
			panic(__FILE__, "sys_sigsend failed", s);

		/* Done */
		return;
	}

	/* Ask FS to unpause the process. Deliver the signal when FS is
	 * ready.
	 */
	unpause(slot, FALSE /*!for_trace*/);
	return;
  }
  else if (sigismember(&rmp->mp_sig2mess, signo)) {

	/* Mark event pending in process slot and send notification. */
	sigaddset(&rmp->mp_sigpending, signo);
	notify(rmp->mp_endpoint);
  	return;
  }

doterminate:
  /* Signal should not or cannot be caught.  Take default action. */
  if (sigismember(&ign_sset, signo)) {
	return;
  }

  /* Terminate process */
  rmp->mp_sigstatus = (char) signo;
  if (sigismember(&core_sset, signo) && slot != FS_PROC_NR) {
	printf("PM: coredump signal %d for %d / %s\n", signo, rmp->mp_pid,
		rmp->mp_name);
	exit_proc(rmp, 0, TRUE /*dump_core*/);
  }
  else {
  	exit_proc(rmp, 0, FALSE /*dump_core*/);
  }
}

/*===========================================================================*
 *				check_sig				     *
 *===========================================================================*/
PUBLIC int check_sig(proc_id, signo)
pid_t proc_id;			/* pid of proc to sig, or 0 or -1, or -pgrp */
int signo;			/* signal to send to process (0 to _NSIG) */
{
/* Check to see if it is possible to send a signal.  The signal may have to be
 * sent to a group of processes.  This routine is invoked by the KILL system
 * call, and also when the kernel catches a DEL or other signal.
 */

  register struct mproc *rmp;
  int count;			/* count # of signals sent */
  int error_code;

  if (signo < 0 || signo > _NSIG) return(EINVAL);

  /* Return EINVAL for attempts to send SIGKILL to INIT alone. */
  if (proc_id == INIT_PID && signo == SIGKILL) return(EINVAL);

  /* Search the proc table for processes to signal.  
   * (See forkexit.c about pid magic.)
   */
  count = 0;
  error_code = ESRCH;
  for (rmp = &mproc[0]; rmp < &mproc[NR_PROCS]; rmp++) {
	if (!(rmp->mp_flags & IN_USE)) continue;

	/* Check for selection. */
	if (proc_id > 0 && proc_id != rmp->mp_pid) continue;
	if (proc_id == 0 && mp->mp_procgrp != rmp->mp_procgrp) continue;
	if (proc_id == -1 && rmp->mp_pid <= INIT_PID) continue;
	if (proc_id < -1 && rmp->mp_procgrp != -proc_id) continue;

	/* Do not kill servers and drivers when broadcasting SIGKILL. */
	if (proc_id == -1 && signo == SIGKILL &&
		(rmp->mp_flags & PRIV_PROC)) continue;

	/* Check for permission. */
	if (mp->mp_effuid != SUPER_USER
	    && mp->mp_realuid != rmp->mp_realuid
	    && mp->mp_effuid != rmp->mp_realuid
	    && mp->mp_realuid != rmp->mp_effuid
	    && mp->mp_effuid != rmp->mp_effuid) {
		error_code = EPERM;
		continue;
	}

	count++;
	if (signo == 0 || (rmp->mp_flags & EXITING)) continue;

	/* 'sig_proc' will handle the disposition of the signal.  The
	 * signal may be caught, blocked, ignored, or cause process
	 * termination, possibly with core dump.
	 */
	sig_proc(rmp, signo);

	if (proc_id > 0) break;	/* only one process being signaled */
  }

  /* If the calling process has killed itself, don't reply. */
  if ((mp->mp_flags & (IN_USE | EXITING)) != IN_USE) return(SUSPEND);
  return(count > 0 ? OK : error_code);
}

/*===========================================================================*
 *				check_pending				     *
 *===========================================================================*/
PUBLIC void check_pending(rmp)
register struct mproc *rmp;
{
  /* Check to see if any pending signals have been unblocked.  The
   * first such signal found is delivered.
   *
   * If multiple pending unmasked signals are found, they will be
   * delivered sequentially.
   *
   * There are several places in this file where the signal mask is
   * changed.  At each such place, check_pending() should be called to
   * check for newly unblocked signals.
   */

  int i;

  for (i = 1; i <= _NSIG; i++) {
	if (sigismember(&rmp->mp_sigpending, i) &&
		!sigismember(&rmp->mp_sigmask, i)) {
		sigdelset(&rmp->mp_sigpending, i);
		sig_proc(rmp, i);
		break;
	}
  }
}

/*===========================================================================*
 *				unpause					     *
 *===========================================================================*/
PRIVATE void unpause(pro, for_trace)
int pro;			/* which process number */
int for_trace;			/* for tracing */
{
/* A signal is to be sent to a process.  If that process is hanging on a
 * system call, the system call must be terminated with EINTR.  Possible
 * calls are PAUSE, WAIT, READ and WRITE, the latter two for pipes and ttys.
 * First check if the process is hanging on an PM call.  If not, tell FS,
 * so it can check for READs and WRITEs from pipes, ttys and the like.
 */
  register struct mproc *rmp;
  int r;

  rmp = &mproc[pro];

  /* Check to see if process is hanging on a PAUSE, WAIT or SIGSUSPEND call. */
  if (rmp->mp_flags & (PAUSED | WAITING | SIGSUSPENDED)) {
	rmp->mp_flags &= ~(PAUSED | WAITING | SIGSUSPENDED);
	setreply(pro, EINTR);
	return;
  }

  /* Process is not hanging on an PM call.  Ask FS to take a look. */
  if (for_trace)
  {
	  if (rmp->mp_fs_call != PM_IDLE)
		panic( __FILE__, "unpause: not idle", rmp->mp_fs_call);
	  rmp->mp_fs_call= PM_UNPAUSE_TR;
  }
  else
  {
	  if (rmp->mp_fs_call2 != PM_IDLE)
		panic( __FILE__, "unpause: not idle", rmp->mp_fs_call2);
	  rmp->mp_fs_call2= PM_UNPAUSE;
  }
  r= notify(FS_PROC_NR);
  if (r != OK) panic("pm", "unpause: unable to notify FS", r);
}
