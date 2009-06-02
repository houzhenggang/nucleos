/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include "syslib.h"

PUBLIC int sys_stime(boottime)
time_t boottime;		/* New boottime */
{
  message m;
  int r;

  m.T_BOOTTIME = boottime;
  r = _taskcall(SYSTASK, SYS_STIME, &m);
  return(r);
}
