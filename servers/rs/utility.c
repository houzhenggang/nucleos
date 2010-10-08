/* This file contains some utility routines for RS.
 *
 * Changes:
 *   Nov 22, 2009: Created    (Cristiano Giuffrida)
 */

#include "inc.h"

#include <servers/ds/ds.h>

/*===========================================================================*
 *				publish_service				     *
 *===========================================================================*/
int publish_service(rp)
struct rproc *rp;				/* pointer to process slot */
{
	/* A new system service has been started. Publish the necessary information. */
	int s;

	/* Register its label with DS. */
	s= ds_publish_u32(rp->r_label, rp->r_proc_nr_e);
	if (s != 0) {
		return s;
	}
	if (rs_verbose) {
		printk("RS: publish_service: DS label registration done: %s -> %d\n", 
			rp->r_label, rp->r_proc_nr_e);
	}

	return(0);
}
