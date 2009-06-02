/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* adddma.c 
 */

#include <lib.h>
#define adddma	_adddma
#include <unistd.h>
#include <stdarg.h>

int adddma(proc_e, start, size)
endpoint_t proc_e;
phys_bytes start;
phys_bytes size;
{
  message m;

  m.m2_i1= proc_e;
  m.m2_l1= start;
  m.m2_l2= size;

  return _syscall(MM, ADDDMA, &m);
}
