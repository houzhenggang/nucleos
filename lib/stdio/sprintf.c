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
 * sprintf - print formatted output on an array
 */
/* $Header: /cvsup/minix/src/lib/stdio/sprintf.c,v 1.1.1.1 2005/04/21 14:56:36 beng Exp $ */
#include <nucleos/nucleos.h>
#include <stdio.h>
#include <stdarg.h>
#include <nucleos/limits.h>
#include "loc_incl.h"

int
sprintf(char *s, const char *format, ...)
{
	va_list ap;
	int retval;

	va_start(ap, format);

	retval = vsnprintf(s, INT_MAX, format, ap);

	va_end(ap);

	return retval;
}

int
snprintf(char *s, size_t n, const char *format, ...)
{
	va_list ap;
	int retval;

	va_start(ap, format);

	retval = vsnprintf(s, n, format, ap);

	va_end(ap);

	return retval;
}
