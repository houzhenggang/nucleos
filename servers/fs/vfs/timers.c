/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* VFS_PROC_NR timers library
 */
#include <nucleos/kernel.h>
#include "fs.h"
#include <nucleos/timer.h>
#include <nucleos/syslib.h>
#include <nucleos/com.h>

static timer_t *fs_timers = NULL;

void fs_set_timer(timer_t *tp, int ticks, tmr_func_t watchdog, int arg)
{
	int r;
	clock_t now, old_head = 0, new_head;

	if ((r = getuptime(&now)) != 0)
		panic(__FILE__, "VFS_PROC_NR couldn't get uptime from system task.", NO_NUM);

	tmr_arg(tp)->ta_int = arg;

	old_head = tmrs_settimer(&fs_timers, tp, now+ticks, watchdog, &new_head);

	/* reschedule our synchronous alarm if necessary */
	if (!old_head || old_head > new_head) {
		if (sys_setalarm(new_head, 1) != 0)
			panic(__FILE__, "VFS_PROC_NR set timer "
			"couldn't set synchronous alarm.", NO_NUM);
	}

	return;
}

void fs_expire_timers(clock_t now)
{
	clock_t new_head;
	tmrs_exptimers(&fs_timers, now, &new_head);
	if (new_head > 0) {
		if (sys_setalarm(new_head, 1) != 0)
			panic(__FILE__, "VFS_PROC_NR expire timer couldn't set "
				"synchronous alarm.", NO_NUM);
	}
}

void fs_init_timer(timer_t *tp)
{
	tmr_inittimer(tp);
}

void fs_cancel_timer(timer_t *tp)
{
	clock_t new_head, old_head;
	old_head = tmrs_clrtimer(&fs_timers, tp, &new_head);

	/* if the earliest timer has been removed, we have to set
	 * the synalarm to the next timer, or cancel the synalarm
	 * altogether if th last time has been cancelled (new_head
	 * will be 0 then).
	 */
	if (old_head < new_head || !new_head) {
		if (sys_setalarm(new_head, 1) != 0)
			panic(__FILE__,
			"VFS_PROC_NR expire timer couldn't set synchronous alarm.",
				 NO_NUM);
	}
}
