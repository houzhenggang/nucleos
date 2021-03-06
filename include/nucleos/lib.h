/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* The <nucleos/lib.h> header is the master header used by the library.
 * All the C files in the lib subdirectories include it.
 */

#ifndef _NUCLEOS_LIB_H
#define _NUCLEOS_LIB_H

/* The following are so basic, all the lib files get them automatically. */
#include <nucleos/types.h>
#include <nucleos/limits.h>
#include <nucleos/errno.h>
#include <nucleos/const.h>
#include <nucleos/com.h>
#include <nucleos/type.h>
#include <nucleos/unistd.h>
#include <nucleos/string.h>
#include <nucleos/kipc.h>
#include <asm/syscall.h>

#endif /* _NUCLEOS_LIB_H */
