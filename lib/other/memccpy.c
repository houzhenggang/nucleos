/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <lib.h>
/* memccpy - copy bytes up to a certain char
 *
 * CHARBITS should be defined only if the compiler lacks "unsigned char".
 * It should be a mask, e.g. 0377 for an 8-bit machine.
 */
#include <nucleos/stddef.h>

void *memccpy(void *dst, const void *src, int ucharstop, size_t size);

#ifndef CHARBITS
#	define	UNSCHAR(c)	((unsigned char)(c))
#else
#	define	UNSCHAR(c)	((c)&CHARBITS)
#endif

void *memccpy(dst, src, ucharstop, size)
void * dst;
const void * src;
int ucharstop;
size_t size;
{
  register char *d;
  register const char *s;
  register size_t n;
  register int uc;

  if (size <= 0) return( (void *) NULL);

  s = (char *) src;
  d = (char *) dst;
  uc = UNSCHAR(ucharstop);
  for (n = size; n > 0; n--)
	if (UNSCHAR(*d++ = *s++) == (char) uc) return( (void *) d);

  return( (void *) NULL);
}
