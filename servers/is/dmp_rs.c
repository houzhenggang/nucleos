/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* This file contains procedures to dump RS data structures.
 *
 * The entry points into this file are
 *   rproc_dump:   	display RS system process table	  
 *
 * Created:
 *   Oct 03, 2005:	by Jorrit N. Herder
 */
#include "inc.h"
#include <nucleos/kernel.h>
#include <nucleos/timer.h>
#include <servers/rs/rs.h>
#include <servers/rs/const.h>
#include <servers/rs/type.h>
#include <kernel/priv.h>

struct rproc rproc[NR_SYS_PROCS];

static char *s_flags_str(int flags);

/*===========================================================================*
 *				rproc_dmp				     *
 *===========================================================================*/
void rproc_dmp()
{
  struct rproc *rp;
  int i,j, n=0;
  static int prev_i=0;

  getsysinfo(RS_PROC_NR, SI_PROC_TAB, rproc);

  printk("Reincarnation Server (RS) system process table dump\n");
  printk("----label---- endpoint- -pid- flags -dev- -T- alive_tm starts command\n");
  for (i=prev_i; i<NR_SYS_PROCS; i++) {
  	rp = &rproc[i];
  	if (!(rp->r_flags & RS_IN_USE)) continue;
  	if (++n > 22) break;
  	printk("%13s %9d %5d %5s %3d/%1d %3u %8u %5dx %s",
  		rp->r_label, rp->r_proc_nr_e, rp->r_pid,
		s_flags_str(rp->r_flags), rp->r_dev_nr, rp->r_dev_style,
		rp->r_period, rp->r_alive_tm, rp->r_restarts,
		rp->r_cmd
  	);
	printk("\n");
  }
  if (i >= NR_SYS_PROCS) i = 0;
  else printk("--more--\r");
  prev_i = i;
}


static char *s_flags_str(int flags)
{
	static char str[10];
	str[0] = (flags & RS_IN_USE) 	    ? 'U' : '-';
	str[1] = (flags & RS_EXITING)       ? 'E' : '-';
	str[2] = (flags & RS_REFRESHING)    ? 'R' : '-';
	str[3] = (flags & RS_NOPINGREPLY)   ? 'N' : '-';
	str[4] = '-';
	str[5] = '\0';

	return(str);
}

