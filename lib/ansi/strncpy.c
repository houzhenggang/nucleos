/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
#include	<string.h>

char *
strncpy(char *ret, register const char *s2, register size_t n)
{
	register char *s1 = ret;

	if (n>0) {
		while((*s1++ = *s2++) && --n > 0)
			/* EMPTY */ ;
		if ((*--s2 == '\0') && --n > 0) {
			do {
				*s1++ = '\0';
			} while(--n > 0);
		}
	}
	return ret;
}
