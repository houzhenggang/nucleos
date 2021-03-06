/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/syslib.h>

/*===========================================================================*
 *                                sys_out				     *
 *===========================================================================*/
int sys_out(port, value, type)
int port; 				/* port address to write to */
unsigned long value;			/* value to write */
int type;				/* byte, word, long */
{
    kipc_msg_t m_io;

    m_io.DIO_REQUEST = _DIO_OUTPUT | type;
    m_io.DIO_PORT = port;
    m_io.DIO_VALUE = value;

    return ktaskcall(SYSTASK, SYS_DEVIO, &m_io);
}

