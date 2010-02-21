/* Global constants used in RS.
 */
#ifndef __SERVERS_RS_CONST_H
#define __SERVERS_RS_CONST_H

/* Space reserved for program and arguments. */
#define MAX_COMMAND_LEN		512	/* maximum argument string length */
#define MAX_LABEL_LEN		16	/* Unique name of (this instance of)
					 * the driver
					 */
#define MAX_SCRIPT_LEN		256	/* maximum restart script name length */
#define MAX_NR_ARGS		4	/* maximum number of arguments */
#define MAX_RESCUE_DIR_LEN	64	/* maximum rescue dir length */

#define MAX_IPC_LIST		256	/* Max size of list for IPC target
					 * process names
					 */
#define MAX_VM_LIST		256

/* Flag values. */
#define RS_IN_USE	0x001	/* set when process slot is in use */
#define RS_EXITING	0x004	/* set when exit is expected */
#define RS_REFRESHING	0x008	/* set when refresh must be done */
#define RS_NOPINGREPLY	0x010	/* driver failed to reply to a ping request */
#define RS_KILLED	0x020	/* driver is killed */
#define RS_CRASHED	0x040	/* driver crashed */
#define RS_LATEREPLY	0x080	/* no reply sent to RS_DOWN caller yet */
#define RS_SIGNALED	0x100	/* driver crashed */

/* Sys flag values. */
#define SF_CORE_PROC	0x001	/* set for core system processes
				 * XXX FIXME: This should trigger a system
				 * panic when a CORE_PROC service cannot
				 * be restarted. We need better error-handling
				 * in RS to change this.
				 */
#define SF_NEED_COPY	0x004	/* set when process needs copy to restart */
#define SF_USE_COPY	0x008	/* set when process has a copy in memory */

/* Constants determining RS period and binary exponential backoff. */
#define RS_DELTA_T	60			/* check every T ticks */
#define BACKOFF_BITS	(sizeof(long)*8)	/* bits in backoff field */
#define MAX_BACKOFF	30			/* max backoff in RS_DELTA_T */

/* Magic process table addresses. */
#define BEG_RPROC_ADDR	(&rproc[0])
#define END_RPROC_ADDR	(&rproc[NR_SYS_PROCS])
#define NIL_RPROC	((struct mproc *) 0)


/* Definitions for boot info tables. */
#define NULL_BOOT_NR	NR_BOOT_PROCS		/* marks a null boot entry */
#define DEFAULT_BOOT_NR	NR_BOOT_PROCS		/* marks the default boot entry */
#define SYS_ALL_C	(NR_SYS_CALLS+0)	/* specifies all calls */
#define SYS_NULL_C	(NR_SYS_CALLS+1)	/* marks a null call entry */

/* Define privilege flags for the various process types. */
#define SRV_F	(SYS_PROC | PREEMPTIBLE)	/* system services */
#define DSRV_F	(SRV_F | DYN_PRIV_ID | CHECK_IO_PORT | CHECK_IRQ)
						/* dynamic system services */
#define VM_F	(SYS_PROC)			/* vm  */
#define RUSR_F	(BILLABLE | PREEMPTIBLE)	/* root user proc */

/* Define system call traps for the various process types. These call masks
 * determine what system call traps a process is allowed to make.
 */
#define SRV_T	(~0)		/* system services */
#define DSRV_T	SRV_T		/* dynamic system services */
#define RUSR_T	(1 << KIPC_SENDREC)	/* root user proc */

/* Send masks determine to whom processes can send messages or notifications. */
#define SRV_M		(~0)		/* system services */
#define RUSR_M \
	( spi_to(PM_PROC_NR) | spi_to(FS_PROC_NR) | spi_to(RS_PROC_NR) \
	 | spi_to(VM_PROC_NR) )		/* root user proc */

/* Define sys flags for the various process types. */
#define SRV_SF		(SF_CORE_PROC | SF_NEED_COPY)	/* system services */
#define SRVC_SF		(SRV_SF | SF_USE_COPY)		/* system services with a copy */
#define DSRV_SF		(0)				/* dynamic system services */

#endif /* __SERVERS_RS_CONST_H */

