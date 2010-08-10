/* This file contains the definition of the boot image info tables.
 *
 * Changes:
 *   Nov 22, 2009: Created  (Cristiano Giuffrida)
 */
#include "inc.h"

/* Define kernel calls that processes are allowed to make. This is not looking
 * very nice, but we need to define the access rights on a per call basis.
 * 
 * Calls are unordered lists, converted by RS to bitmasks
 * once at runtime.
 */
#define FS_KC SYS_KILL, SYS_VIRCOPY, SYS_SAFECOPYFROM, SYS_SAFECOPYTO, \
	SYS_VIRVCOPY, SYS_UMAP, SYS_GETINFO, SYS_EXIT, SYS_TIMES, SYS_SETALARM, \
	SYS_PRIVCTL, SYS_TRACE , SYS_SETGRANT, SYS_PROFBUF, SYS_SYSCTL, \
	SYS_STRNLEN
#define DRV_KC	FS_KC, SYS_SEGCTL, SYS_IRQCTL, SYS_INT86, SYS_DEVIO, \
	SYS_SDEVIO, SYS_VDEVIO, SYS_SETGRANT, SYS_PROFBUF, SYS_SYSCTL

static int
	fs_kc[] = { FS_KC, SYS_NULL_C },
	pm_kc[] = { SYS_ALL_C, SYS_NULL_C },
	ds_kc[] = { SYS_ALL_C, SYS_NULL_C },
	vm_kc[] = { SYS_ALL_C, SYS_NULL_C },
	drv_kc[] = { DRV_KC, SYS_NULL_C },
	tty_kc[] = { DRV_KC, SYS_PHYSCOPY, SYS_ABORT, SYS_IOPENABLE,
	    SYS_READBIOS, SYS_NULL_C },
	mem_kc[] = { DRV_KC, SYS_PHYSCOPY, SYS_PHYSVCOPY, SYS_IOPENABLE, SYS_NULL_C },
	rusr_kc[] = { SYS_NULL_C },

	no_kc[] = { SYS_NULL_C }; /* no kernel call */

/* Definition of the boot image priv table. */
struct boot_image_priv boot_image_priv_table[] = {
  /*endpoint,     label,      flags,  traps,  ipcto,  kcalls  */
  { VM_PROC_NR,   "vm",       VM_F,   SRV_T,  SRV_M,  vm_kc   },
  { PM_PROC_NR,   "pm",       SRV_F,  SRV_T,  SRV_M,  pm_kc   },
  { VFS_PROC_NR,   "vfs",      SRV_F,  SRV_T,  SRV_M,  fs_kc   },
  { DS_PROC_NR,   "ds",       SRV_F,  SRV_T,  SRV_M,  ds_kc   },
  { TTY_PROC_NR,  "tty",      SRV_F,  SRV_T,  SRV_M,  tty_kc  },
  { MEM_PROC_NR,  "memory",   SRV_F,  SRV_T,  SRV_M,  mem_kc  },
  { LOG_PROC_NR,  "log",      SRV_F,  SRV_T,  SRV_M,  drv_kc  },
  { MFS_PROC_NR,  "fs_imgrd", SRV_F,  SRV_T,  SRV_M,  fs_kc   },
  { PFS_PROC_NR,  "pfs",      SRV_F,  SRV_T,  SRV_M,  fs_kc   },
  { INIT_PROC_NR, "init",     RUSR_F, RUSR_T, RUSR_M, rusr_kc },
  { NULL_BOOT_NR, "",         0,      0,      0,      no_kc   } /* null entry */
};

/* Definition of the boot image sys table. */
struct boot_image_sys boot_image_sys_table[] = {
  /*endpoint,         flags                             */
	{ LOG_PROC_NR,      SRVC_SF                           },
	{ MFS_PROC_NR,      SRVC_SF                           },
  { PFS_PROC_NR,      SRVC_SF                           },
	{ DEFAULT_BOOT_NR,  SRV_SF                            } /* default entry */
};

/* Definition of the boot image dev table. */
struct boot_image_dev boot_image_dev_table[] = {
	/*endpoint,         dev_nr,       dev_style,  period  */
	{ TTY_PROC_NR,      TTY_MAJOR,    STYLE_TTY,       0  },
	{ MEM_PROC_NR,      MEMORY_MAJOR, STYLE_DEV,       0  },
	{ LOG_PROC_NR,      LOG_MAJOR,    STYLE_DEV,       0  },
	{ DEFAULT_BOOT_NR,  0,            STYLE_NDEV,      0  } /* default entry */
};

/* The system process table. This table only has entries for system
 * services (servers and drivers), and thus is not directly indexed by
 * slot number.
 */
struct rproc rproc[NR_SYS_PROCS];
struct rproc *rproc_ptr[NR_PROCS];       /* mapping for fast access */

/* Pipe for detection of exec failures. The pipe is close-on-exec, and
 * no data will be written to the pipe if the exec succeeds. After an
 * exec failure, the slot number is written to the pipe. After each exit,
 * a non-blocking read retrieves the slot number from the pipe.
 */
int exec_pipe[2];

/* Enable/disable verbose output. */
long rs_verbose;
