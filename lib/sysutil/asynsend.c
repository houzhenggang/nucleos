/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <assert.h> 
#include <nucleos/types.h>
#include <nucleos/const.h>
#include <nucleos/type.h>

#include <nucleos/fcntl.h>
#include <stdlib.h>
#include <nucleos/unistd.h>
#include <nucleos/syslib.h>
#include <nucleos/sysutil.h>

#include <nucleos/limits.h>
#include <nucleos/errno.h>

#define ASYN_NR	100
static asynmsg_t msgtable[ASYN_NR];
static int first_slot= 0, next_slot= 0;

int asynsend3(dst, mp, fl)
endpoint_t dst;
kipc_msg_t *mp;
int fl;
{
	int r, src_ind, dst_ind;
	unsigned flags;
	static int first = 1;
	static int inside = 0;
	int len;

	/* printk() causes asynsend? */
	if(inside) {
		exit(1);
	}

	inside = 1;

	if(first) {
		int i;
		for(i = 0; i < ASYN_NR; i++) {
			msgtable[i].flags = AMF_EMPTY;
		}
		first = 0;
	}

	/* Update first_slot */
	for (; first_slot < next_slot; first_slot++)
	{
		flags= msgtable[first_slot].flags;
		if ((flags & (AMF_VALID|AMF_DONE)) == (AMF_VALID|AMF_DONE))
		{
			if (msgtable[first_slot].result != 0)
			{
#if 0
				printk(
			"asynsend: found completed entry %d with error %d\n",
					first_slot,
					msgtable[first_slot].result);
#endif
			}
			continue;
		}
		if (flags != AMF_EMPTY)
			break;
	}

	if (first_slot >= next_slot)
	{
		/* Reset first_slot and next_slot */
		next_slot= first_slot= 0;
	}

	if (next_slot >= ASYN_NR)
	{
		/* Tell the kernel to stop processing */
		r= kipc_module_call(KIPC_SENDA, 0, 0, 0);
		if (r != 0)
			panic(__FILE__, "asynsend: send failed", r);

		dst_ind= 0;
		for (src_ind= first_slot; src_ind<next_slot; src_ind++)
		{
			flags= msgtable[src_ind].flags;
			if ((flags & (AMF_VALID|AMF_DONE)) ==
				(AMF_VALID|AMF_DONE))
			{
				if (msgtable[src_ind].result != 0)
				{
#if 0
					printk(
			"asynsend: found completed entry %d with error %d\n",
						src_ind,
						msgtable[src_ind].result);
#endif
				}
				continue;
			}
			if (flags == AMF_EMPTY)
				continue;
#if 0
			printk("asynsend: copying entry %d to %d\n",
				src_ind, dst_ind);
#endif
			if (src_ind != dst_ind)
				msgtable[dst_ind]= msgtable[src_ind];
			dst_ind++;
		}
		first_slot= 0;
		next_slot= dst_ind;
		if (next_slot >= ASYN_NR)
			panic(__FILE__, "asynsend: msgtable full", NO_NUM);
	}

	fl |= AMF_VALID;
	msgtable[next_slot].dst= dst;
	msgtable[next_slot].msg= *mp;
	msgtable[next_slot].flags= fl;		/* Has to be last. The kernel 
					 	 * scans this table while we
						 * are sleeping.
					 	 */
	next_slot++;

	assert(first_slot < ASYN_NR);
	assert(next_slot >= first_slot);
	len = next_slot-first_slot;

	assert(first_slot + len <= ASYN_NR);

	/* Tell the kernel to rescan the table */
	r = kipc_module_call(KIPC_SENDA, len, 0, msgtable+first_slot);

	inside = 0;

	return r;
}

