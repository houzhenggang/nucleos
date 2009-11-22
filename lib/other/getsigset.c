/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/lib.h>
#include <nucleos/unistd.h>
#include <nucleos/signal.h>

/**
 * @brief
 * @param sp  where to put it
 */
int getsigset(sigset_t *sp)
{
	message m;
	m.m2_i1 = SELF;			/* request own signal set */

	if (ksyscall(PM_PROC_NR, KCNR_PROCSTAT, &m) < 0)
		return(-1);

	*sp = m.m2_l1;

	return(0);
}

