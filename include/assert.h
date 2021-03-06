/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* The <assert.h> header contains a macro called "assert" that allows 
 * programmers to put assertions in the code.  These assertions can be verified
 * at run time.  If an assertion fails, an error message is printed and the
 * program aborts.
 * Assertion checking can be disabled by adding the statement
 *
 *	#define NDEBUG
 *
 * to the program before the 
 *
 *	#include <assert.h>
 *
 * statement.
 */

#undef assert

#ifdef NDEBUG
/* Debugging disabled -- do not evaluate assertions. */
#define assert(expr)  ((void) 0)
#else
/* Debugging enabled -- verify assertions at run time. */
#define	__makestr(x)	# x
#define	__xstr(x)	__makestr(x)

void __bad_assertion(const char *_mess);

#define	assert(expr)	((expr)? (void)0 : \
				__bad_assertion("Assertion \"" #expr \
				    "\" failed, file " __xstr(__FILE__) \
				    ", line " __xstr(__LINE__) "\n"))
#endif
