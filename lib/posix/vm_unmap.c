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
#include <nucleos/mman.h>
#include <nucleos/string.h>
#include <nucleos/errno.h>
#include <stdarg.h>

int vm_unmap(int endpt, void *addr)
{
	message m;

	m.VMUN_ENDPT = endpt;
	m.VMUN_ADDR = (long) addr;

	return ksyscall(VM_PROC_NR, VM_SHM_UNMAP, &m);
}