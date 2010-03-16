/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* The kernel call implemented in this file:
 *   m_type:	SYS_EXIT
 *
 * The parameters for this kernel call are:
 *    m1_i1:	PR_ENDPT		(slot number of exiting process)
 */

#include <kernel/system.h>

#include <nucleos/endpoint.h>

#if USE_EXIT

static void clear_proc(register struct proc *rc);

/*===========================================================================*
 *				do_exit					     *
 *===========================================================================*/
int do_exit(m_ptr)
message *m_ptr;			/* pointer to request message */
{
/* Handle sys_exit. A user process has exited or a system process requests 
 * to exit. Only the PM can request other process slots to be cleared.
 * The routine to clean up a process table slot cancels outstanding timers, 
 * possibly removes the process from the message queues, and resets certain 
 * process table fields to the default values.
 */
  int exit_e;				

  /* Determine what process exited. User processes are handled here. */
  if (PM_PROC_NR == who_p) {
      if (m_ptr->PR_ENDPT != ENDPT_SELF) { 		/* PM tries to exit self */
          if(!isokendpt(m_ptr->PR_ENDPT, &exit_e)) /* get exiting process */
	     return -EINVAL;
          clear_proc(proc_addr(exit_e));	/* exit a user process */
          return 0;				/* report back to PM */
      }
  } 

  /* The PM or some other system process requested to be exited. */
  clear_proc(proc_addr(who_p));
  return(-EDONTREPLY);
}

/*===========================================================================*
 *			         clear_proc				     *
 *===========================================================================*/
static void clear_proc(rc)
register struct proc *rc;		/* slot of process to clean up */
{
  register struct proc *rp;		/* iterate over process table */
  register struct proc **xpp;		/* iterate over caller queue */
  int i;
  int sys_id;

  /* Don't clear if already cleared. */
  if(isemptyp(rc)) return;

  /* Check the table with IRQ hooks to see if hooks should be released. */
  for (i=0; i < NR_IRQ_HOOKS; i++) {
      int proc;
      if (rc->p_endpoint == irq_hooks[i].proc_nr_e) {
        rm_irq_handler(&irq_hooks[i]);	/* remove interrupt handler */
        irq_hooks[i].proc_nr_e = ENDPT_NONE;	/* mark hook as free */
      } 
  }

  /* Remove the process' ability to send and receive messages */
  clear_endpoint(rc);


  /* Turn off any alarm timers at the clock. */   
  reset_timer(&priv(rc)->s_alarm_timer);

  /* Make sure that the exiting process is no longer scheduled,
   * and mark slot as FREE. Also mark saved fpu contents as not significant.
   */
  RTS_LOCK_SETFLAGS(rc, RTS_SLOT_FREE);
  rc->p_misc_flags &= ~MF_FPU_INITIALIZED;

  /* Release the process table slot. If this is a system process, also
   * release its privilege structure.  Further cleanup is not needed at
   * this point. All important fields are reinitialized when the 
   * slots are assigned to another, new process. 
   */
  if (priv(rc)->s_flags & SYS_PROC) priv(rc)->s_proc_nr = ENDPT_NONE;

#if 0
  /* Clean up virtual memory */
  if (rc->p_misc_flags & MF_VM) {
  	vm_map_default(rc);
  }
#endif
}

#endif /* USE_EXIT */

