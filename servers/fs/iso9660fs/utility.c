#include "inc.h"
#include <nucleos/stat.h>
#include <nucleos/string.h>
#include <nucleos/com.h>
#include <nucleos/unistd.h>
#include <nucleos/vfsif.h>

static int panicking;

/*===========================================================================*
 *				no_sys					     *
 *===========================================================================*/
int no_sys()
{
/* Somebody has used an illegal system call number */
  return(-EINVAL);
}

/*===========================================================================*
 *				panic					     *
 *===========================================================================*/
void panic(who, mess, num)
char *who;			/* who caused the panic */
char *mess;			/* panic message string */
int num;			/* number to go with it */
{
/* Something awful has happened.  Panics are caused when an internal
 * inconsistency is detected, e.g., a programming error or illegal value of a
 * defined constant.
 */
  if (panicking) return;	/* do not panic during a sync */
  panicking = TRUE;		/* prevent another panic during the sync */

  printk("FS panic (%s): %s ", who, mess);
  if (num != NO_NUM) printk("%d",num);
  exit(1);
}
