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
posix/cfsetospeed

Created:	June 11, 1993 by Philip Homburg
*/

#include <nucleos/termios.h>

int cfsetospeed(struct termios *termios_p, speed_t speed)
{
  termios_p->c_ospeed= speed;
  return 0;
}