/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/kernel.h>
#include "timers.h"

/*===========================================================================*
 *				tmrs_clrtimer				     *
 *===========================================================================*/
clock_t tmrs_clrtimer(tmrs, tp, next_time)
timer_t **tmrs;				/* pointer to timers queue */
timer_t *tp;				/* timer to be removed */
clock_t *next_time;
{
/* Deactivate a timer and remove it from the timers queue. 
 */
  timer_t **atp;
  struct proc *p;
  clock_t prev_time;

  if(*tmrs)
  	prev_time = (*tmrs)->tmr_exp_time;
  else
  	prev_time = 0;

  tp->tmr_exp_time = TMR_NEVER;

  for (atp = tmrs; *atp != NULL; atp = &(*atp)->tmr_next) {
	if (*atp == tp) {
		*atp = tp->tmr_next;
		break;
	}
  }

  if(next_time) {
  	if(*tmrs)
  		*next_time = (*tmrs)->tmr_exp_time;
  	else	
  		*next_time = 0;
  }

  return prev_time;
}

