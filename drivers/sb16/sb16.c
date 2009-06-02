/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include "sb16.h"

/*===========================================================================*
 *				mixer_set
 *===========================================================================*/
PUBLIC int mixer_set(reg, data) 
int reg;
int data;
{
	int i;

	sb16_outb(MIXER_REG, reg);
	for(i = 0; i < 100; i++);
	sb16_outb(MIXER_DATA, data);

	return OK;
}


/*===========================================================================*
 *				sb16_inb
 *===========================================================================*/
PUBLIC int sb16_inb(port)
int port;
{	
	int s;
	unsigned long value;

	if ((s=sys_inb(port, &value)) != OK)
		panic("SB16DSP","sys_inb() failed", s);
	
	return value;
}


/*===========================================================================*
 *				sb16_outb
 *===========================================================================*/
PUBLIC void sb16_outb(port, value)
int port;
int value;
{
	int s;
	
	if ((s=sys_outb(port, value)) != OK)
		panic("SB16DSP","sys_outb() failed", s);
}
