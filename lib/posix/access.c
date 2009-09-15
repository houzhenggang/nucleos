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

int access(const char *name, int mode)
{
  message m;

  m.m3_i2 = mode;
  _loadname(name, &m);
  return(_syscall(FS_PROC_NR, __NR_access, &m));
}