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
 * gets.c - read a line from a stream
 */
/* $Header: /cvsup/minix/src/lib/stdio/gets.c,v 1.1.1.1 2005/04/21 14:56:35 beng Exp $ */

#include	<stdio.h>

char *
gets(char *s)
{
	register FILE *stream = stdin;
	register int ch;
	register char *ptr;

	ptr = s;
	while ((ch = getc(stream)) != EOF && ch != '\n')
		*ptr++ = ch;

	if (ch == EOF) {
		if (feof(stream)) {
			if (ptr == s) return NULL;
		} else return NULL;
	}

	*ptr = '\0';
	return s;
}
