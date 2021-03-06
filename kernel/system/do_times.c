/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* The kernel call implemented in this file:
 *   m_type:	SYS_TIMES
 *
 * The parameters for this kernel call are:
 *    m_data1:	T_ENDPT		(get info for this process)	
 *    m_data1:	T_USER_TIME		(return values ...)	
 *    m_data2:	T_SYSTEM_TIME	
 *    m_data3:	T_BOOTTIME
 *    m_data5:	T_BOOT_TICKS	
 */

#include <kernel/system.h>

#include <nucleos/endpoint.h>

#if USE_TIMES

/*===========================================================================*
 *				do_times				     *
 *===========================================================================*/
int do_times(m_ptr)
register kipc_msg_t *m_ptr;	/* pointer to request message */
{
/* Handle sys_times().  Retrieve the accounting information. */
  register struct proc *rp;
  int proc_nr, e_proc_nr;

  /* Insert the times needed by the SYS_TIMES kernel call in the message. 
   * The clock's interrupt handler may run to update the user or system time
   * while in this code, but that cannot do any harm.
   */
  e_proc_nr = (m_ptr->T_ENDPT == ENDPT_SELF) ? m_ptr->m_source : m_ptr->T_ENDPT;
  if(e_proc_nr != ENDPT_NONE && isokendpt(e_proc_nr, &proc_nr)) {
      rp = proc_addr(proc_nr);
      m_ptr->T_USER_TIME   = rp->p_user_time;
      m_ptr->T_SYSTEM_TIME = rp->p_sys_time;
  }
  m_ptr->T_BOOT_TICKS = get_uptime();  
  m_ptr->T_BOOTTIME = boottime;  
  return 0;
}

#endif /* USE_TIMES */

