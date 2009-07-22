/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*	rewinddir()					Author: Kees J. Bot
 *								24 Apr 1989
 */
#define nil 0
#include <lib.h>
#define rewinddir _rewinddir
#define seekdir	_seekdir
#include <nucleos/types.h>
#include <dirent.h>

void rewinddir(DIR *dp)
{
	(void) seekdir(dp, 0);
}
