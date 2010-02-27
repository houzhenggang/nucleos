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
 * Changes:
 *   Mar 02, 2009:	Extended isolation policies  (Jorrit N. Herder)
 *   Jul 22, 2005:	Created  (Jorrit N. Herder)
 */
#include <nucleos/kernel.h>
#include "inc.h"
#include <ctype.h>
#include <nucleos/fcntl.h>
#include <nucleos/unistd.h>
#include <nucleos/types.h>
#include <nucleos/stat.h>
#include <nucleos/wait.h>
#include <nucleos/vm.h>
#include <nucleos/lib.h>
#include <nucleos/sysutil.h>

/* Prototypes for internal functions that do the hard work. */
static int caller_is_root(endpoint_t endpoint);
static int caller_can_control(endpoint_t endpoint,
	char *label);
static int copy_label(endpoint_t src_e,
	struct rss_label *src_label, char *dst_label, size_t dst_len);
static int start_service(struct rproc *rp, int flags, endpoint_t *ep);
static int stop_service(struct rproc *rp,int how);
static int fork_nb(void);
static int read_exec(struct rproc *rp);
static int copy_exec(struct rproc *rp_src,
	struct rproc *rp_dst);
static void run_script(struct rproc *rp);
static char *get_next_label(char *ptr, char *label, char *caller_label);
static void add_forward_ipc(struct rproc *rp, struct priv *privp);
static void add_backward_ipc(struct rproc *rp, struct priv *privp);
static void init_privs(struct rproc *rp, struct priv *privp);
static void init_pci(struct rproc *rp, int endpoint);
static int set_privs(endpoint, privp, req);

static int shutting_down = FALSE;

/*===========================================================================*
 *				caller_is_root				     *
 *===========================================================================*/
static int caller_is_root(endpoint)
endpoint_t endpoint;				/* caller endpoint */
{
  uid_t euid;

  /* Check if caller has root user ID. */
  euid = getnuid(endpoint);
  if (rs_verbose && euid != 0)
  {
	printf("RS: got unauthorized request from endpoint %d\n", endpoint);
  }
  
  return euid == 0;
}

/*===========================================================================*
 *				caller_can_control			     *
 *===========================================================================*/
static int caller_can_control(endpoint, label)
endpoint_t endpoint;
char *label;
{
  int control_allowed = 0;
  register struct rproc *rp;
  int c;
  char *progname;

  /* Find name of binary for given label. */
  for (rp = BEG_RPROC_ADDR; rp < END_RPROC_ADDR; rp++) {
	if (strcmp(rp->r_label, label) == 0) {
		break;
	}
  }
  if (rp == END_RPROC_ADDR) return 0;
  progname = strrchr(rp->r_argv[0], '/');
  if (progname != NULL)
	progname++;
  else
	progname = rp->r_argv[0];

  /* Check if label is listed in caller's isolation policy. */
  for (rp = BEG_RPROC_ADDR; rp < END_RPROC_ADDR; rp++) {
	if (rp->r_proc_nr_e == endpoint) {
		break;
	}
  }
  if (rp == END_RPROC_ADDR) return 0;
  if (rp->r_nr_control > 0) {
	for (c = 0; c < rp->r_nr_control; c++) {
		if (strcmp(rp->r_control[c], progname) == 0)
			control_allowed = 1;
	}
  }

  if (rs_verbose) {
	printf("RS: allowing %u control over %s via policy: %s\n",
		endpoint, label, control_allowed ? "yes" : "no");
  }
  return control_allowed;
}

/*===========================================================================*
 *				copy_label				     *
 *===========================================================================*/
static int copy_label(src_e, src_label, dst_label, dst_len)
endpoint_t src_e;
struct rss_label *src_label;
char *dst_label;
size_t dst_len;
{
  int s, len;

  len = MIN(dst_len-1, src_label->l_len);

  s = sys_datacopy(src_e, (vir_bytes) src_label->l_addr,
	SELF, (vir_bytes) dst_label, len);
  if (s != 0) return s;

  dst_label[len] = 0;

  if (rs_verbose)
	printf("RS: copy_label: using label (custom) '%s'\n", dst_label);
  return 0;
}

/*===========================================================================*
 *				do_up					     *
 *===========================================================================*/
int do_up(m_ptr)
message *m_ptr;					/* request message pointer */
{
/* A request was made to start a new system service. 
 */
  register struct rproc *rp;			/* system process table */
  int slot_nr;					/* local table entry */
  int arg_count;				/* number of arguments */
  char *cmd_ptr;				/* parse command string */
  char *label;					/* unique name of command */
  enum dev_style dev_style;			/* device style */
  int s;					/* status variable */
  int len;					/* length of string */
  int i;
  int r;
  endpoint_t ep;
  struct rproc *tmp_rp;
  struct rs_start rs_start;

  /* This call requires special privileges. */
  if (!caller_is_root(m_ptr->m_source)) return(-EPERM);

  /* See if there is a free entry in the table with system processes. */
  for (slot_nr = 0; slot_nr < NR_SYS_PROCS; slot_nr++) {
      rp = &rproc[slot_nr];			/* get pointer to slot */
      if (!(rp->r_flags & RS_IN_USE)) 		/* check if available */
	  break;
  }
  if (slot_nr >= NR_SYS_PROCS)
  {
      printf("RS: do_up: system process table full\n");
	return -ENOMEM;
  }

  /* Ok, there is space. Get the request structure. */
  s= sys_datacopy(m_ptr->m_source, (vir_bytes) m_ptr->RS_CMD_ADDR, 
  	SELF, (vir_bytes) &rs_start, sizeof(rs_start));
  if (s != 0) return(s);

  /* Obtain command name and parameters. This is a space-separated string
   * that looks like "/sbin/service arg1 arg2 ...". Arguments are optional.
   */
  if (rs_start.rss_cmdlen > MAX_COMMAND_LEN-1) return(-E2BIG);
  s=sys_datacopy(m_ptr->m_source, (vir_bytes) rs_start.rss_cmd, 
  	SELF, (vir_bytes) rp->r_cmd, rs_start.rss_cmdlen);
  if (s != 0) return(s);
  rp->r_cmd[rs_start.rss_cmdlen] = '\0';	/* ensure it is terminated */
  if (rp->r_cmd[0] != '/') return(-EINVAL);	/* insist on absolute path */

  /* Build argument vector to be passed to execute call. The format of the
   * arguments vector is: path, arguments, NULL. 
   */
  arg_count = 0;				/* initialize arg count */
  rp->r_argv[arg_count++] = rp->r_cmd;		/* start with path */
  cmd_ptr = rp->r_cmd;				/* do some parsing */ 
  while(*cmd_ptr != '\0') {			/* stop at end of string */
      if (*cmd_ptr == ' ') {			/* next argument */
          *cmd_ptr = '\0';			/* terminate previous */
	  while (*++cmd_ptr == ' ') ; 		/* skip spaces */
	  if (*cmd_ptr == '\0') break;		/* no arg following */
	  if (arg_count>MAX_NR_ARGS+1) break;	/* arg vector full */
          rp->r_argv[arg_count++] = cmd_ptr;	/* add to arg vector */
      }
      cmd_ptr ++;				/* continue parsing */
  }
  rp->r_argv[arg_count] = NULL;			/* end with NULL pointer */
  rp->r_argc = arg_count;

  if(rs_start.rss_label.l_len > 0) {
	/* RS_UP caller has supplied a custom label for this module. */
	int s = copy_label(m_ptr->m_source, &rs_start.rss_label,
		rp->r_label, sizeof(rp->r_label));
	if(s != 0)
		return s;
        if(rs_verbose)
	  printf("RS: do_up: using label (custom) '%s'\n", rp->r_label);
  } else {
	/* Default label for the module. */
	label= strrchr(rp->r_argv[0], '/');
	if (label)
		label++;
	else
		label= rp->r_argv[0];
  	len= strlen(label);
  	if (len > MAX_LABEL_LEN-1)
		len= MAX_LABEL_LEN-1;	/* truncate name */
  	memcpy(rp->r_label, label, len);
  	rp->r_label[len]= '\0';
        if(rs_verbose)
          printf("RS: do_up: using label (from binary %s) '%s'\n",
		rp->r_argv[0], rp->r_label);
  }

  if(rs_start.rss_nr_control > 0) {
	int i, s;
	if (rs_start.rss_nr_control > RSS_NR_CONTROL)
	{
		printf("RS: do_up: too many control labels\n");
		return -EINVAL;
	}
	for (i=0; i<rs_start.rss_nr_control; i++) {
		s = copy_label(m_ptr->m_source, &rs_start.rss_control[i],
			rp->r_control[i], sizeof(rp->r_control[i]));
		if(s != 0)
			return s;
	}
	rp->r_nr_control = rs_start.rss_nr_control;

	if (rs_verbose) {
		printf("RS: do_up: control labels:");
		for (i=0; i<rp->r_nr_control; i++)
			printf(" %s", rp->r_control[i]);
		printf("\n");
	}
  }

  /* Check for duplicates */
  for (slot_nr = 0; slot_nr < NR_SYS_PROCS; slot_nr++) {
      tmp_rp = &rproc[slot_nr];			/* get pointer to slot */
      if (!(tmp_rp->r_flags & RS_IN_USE)) 	/* check if available */
	  continue;
      if (tmp_rp == rp)
	  continue;				/* Our slot */
      if (strcmp(tmp_rp->r_label, rp->r_label) == 0)
      {
	  printf("RS: found duplicate label '%s': slot %d\n",
		rp->r_label, slot_nr);
	  return -EBUSY;
      }
  }

  rp->r_script[0]= '\0';
  if (rs_start.rss_scriptlen > MAX_SCRIPT_LEN-1) return(-E2BIG);
  if (rs_start.rss_script != NULL)
  {
	  s=sys_datacopy(m_ptr->m_source, (vir_bytes) rs_start.rss_script, 
		SELF, (vir_bytes) rp->r_script, rs_start.rss_scriptlen);
	  if (s != 0) return(s);
	  rp->r_script[rs_start.rss_scriptlen] = '\0';
  }
  rp->r_uid= rs_start.rss_uid;
  rp->r_nice= rs_start.rss_nice;

  if (rs_start.rss_flags & RF_IPC_VALID)
  {
	if (rs_start.rss_ipclen+1 > sizeof(rp->r_ipc_list))
	{
		printf("rs: ipc list too long for '%s'\n", rp->r_label);
		return -EINVAL;
	}
	s=sys_datacopy(m_ptr->m_source, (vir_bytes) rs_start.rss_ipc, 
		SELF, (vir_bytes) rp->r_ipc_list, rs_start.rss_ipclen);
	if (s != 0) return(s);
	rp->r_ipc_list[rs_start.rss_ipclen]= '\0';
  }
  else
	rp->r_ipc_list[0]= '\0';

  rp->r_sys_flags = DSRV_SF;
  rp->r_exec= NULL;
  if (rs_start.rss_flags & RF_COPY) {
	int exst_cpy;
	struct rproc *rp2;
	exst_cpy = 0;
	
	if(rs_start.rss_flags & RF_REUSE) {
		char *cmd = rp->r_cmd;
                int i;
                
                for(i = 0; i < NR_SYS_PROCS; i++) {
                	rp2 = &rproc[i];
                        if(strcmp(rp->r_cmd, rp2->r_cmd) == 0 &&
                           (rp2->r_sys_flags & SF_USE_COPY)) {
                                /* We have found the same binary that's
                                 * already been copied */
                                 exst_cpy = 1;
                                 break;
                        }
                }
         }                

	if(!exst_cpy)
		s = read_exec(rp);
	else
		s = copy_exec(rp, rp2); 

	if (s != 0)
		return s;

	rp->r_sys_flags |= SF_USE_COPY;
  }

  /* All dynamically created services get the same privilege flags, and
   * allowed traps. Other privilege settings can be specified at runtime.
   * The privilege id is dynamically allocated by the kernel.
   */
  rp->r_priv.s_flags = DSRV_F;           /* privilege flags */
  rp->r_priv.s_trap_mask = DSRV_T;       /* allowed traps */

  /* Copy granted resources */
  if (rs_start.rss_nr_irq > NR_IRQ)
  {
	printf("RS: do_up: too many IRQs requested\n");
	return -EINVAL;
  }
  rp->r_priv.s_nr_irq= rs_start.rss_nr_irq;
  for (i= 0; i<rp->r_priv.s_nr_irq; i++)
  {
	rp->r_priv.s_irq_tab[i]= rs_start.rss_irq[i];
	if(rs_verbose)
		printf("RS: do_up: IRQ %d\n", rp->r_priv.s_irq_tab[i]);
  }

  if (rs_start.rss_nr_io > NR_IO_RANGE)
  {
	printf("RS: do_up: too many I/O ranges requested\n");
	return -EINVAL;
  }
  rp->r_priv.s_nr_io_range= rs_start.rss_nr_io;
  for (i= 0; i<rp->r_priv.s_nr_io_range; i++)
  {
	rp->r_priv.s_io_tab[i].ior_base= rs_start.rss_io[i].base;
	rp->r_priv.s_io_tab[i].ior_limit=
		rs_start.rss_io[i].base+rs_start.rss_io[i].len-1;
	if(rs_verbose)
	   printf("RS: do_up: I/O [%x..%x]\n",
		rp->r_priv.s_io_tab[i].ior_base,
		rp->r_priv.s_io_tab[i].ior_limit);
  }

  if (rs_start.rss_nr_pci_id > RSS_NR_PCI_ID)
  {
	printf("RS: do_up: too many PCI device IDs\n");
	return -EINVAL;
  }
  rp->r_nr_pci_id= rs_start.rss_nr_pci_id;
  for (i= 0; i<rp->r_nr_pci_id; i++)
  {
	rp->r_pci_id[i].vid= rs_start.rss_pci_id[i].vid;
	rp->r_pci_id[i].did= rs_start.rss_pci_id[i].did;
	if(rs_verbose)
	   printf("RS: do_up: PCI %04x/%04x\n",
		rp->r_pci_id[i].vid, rp->r_pci_id[i].did);
  }
  if (rs_start.rss_nr_pci_class > RSS_NR_PCI_CLASS)
  {
	printf("RS: do_up: too many PCI class IDs\n");
	return -EINVAL;
  }
  rp->r_nr_pci_class= rs_start.rss_nr_pci_class;
  for (i= 0; i<rp->r_nr_pci_class; i++)
  {
	rp->r_pci_class[i].class= rs_start.rss_pci_class[i].class;
	rp->r_pci_class[i].mask= rs_start.rss_pci_class[i].mask;
	if(rs_verbose)
	    printf("RS: do_up: PCI class %06x mask %06x\n",
		rp->r_pci_class[i].class, rp->r_pci_class[i].mask);
  }

  /* Copy 'system' call number bits */
  if (sizeof(rs_start.rss_system[0]) == sizeof(rp->r_call_mask[0]) &&
	sizeof(rs_start.rss_system) == sizeof(rp->r_call_mask))
  {
	for (i= 0; i<RSS_NR_SYSTEM; i++)
		rp->r_call_mask[i]= rs_start.rss_system[i];
  }
  else
  {
	printf(
	"RS: do_up: internal inconsistency: bad size of r_call_mask\n");
	memset(rp->r_call_mask, '\0', sizeof(rp->r_call_mask));
  }

  /* Initialize some fields. */
  rp->r_period = rs_start.rss_period;
  rp->r_dev_nr = rs_start.rss_major;
  rp->r_dev_style = STYLE_DEV; 
  rp->r_restarts = -1; 				/* will be incremented */
  rp->r_set_resources= 1;			/* set resources */

  if (sizeof(rp->r_vm) == sizeof(rs_start.rss_vm) &&
      sizeof(rp->r_vm[0]) == sizeof(rs_start.rss_vm[0]))
  {
	  memcpy(rp->r_vm, rs_start.rss_vm, sizeof(rp->r_vm));
  }
  else
  {
	  printf("RS: do_up: internal inconsistency: bad size of r_vm\n");
	  memset(rp->r_vm, '\0', sizeof(rp->r_vm));
  }

  /* All information was gathered. Now try to start the system service. */
  r = start_service(rp, 0, &ep);
  m_ptr->RS_ENDPOINT = ep;
  return r;
}


/*===========================================================================*
 *				do_down					     *
 *===========================================================================*/
int do_down(message *m_ptr)
{
  register struct rproc *rp;
  size_t len;
  int s, proc;
  char label[MAX_LABEL_LEN];

  /* This call requires special privileges. */
  if (!caller_is_root(m_ptr->m_source)) return(-EPERM);

  len= m_ptr->RS_CMD_LEN;
  if (len >= sizeof(label))
	return -EINVAL;		/* Too long */

  s= sys_datacopy(m_ptr->m_source, (vir_bytes) m_ptr->RS_CMD_ADDR, 
  	SELF, (vir_bytes) label, len);
  if (s != 0) return(s);
  label[len]= '\0';

	for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
		if (rp->r_flags & RS_IN_USE && strcmp(rp->r_label, label) == 0) {
			if(rs_verbose)
				printf("RS: stopping '%s' (%d)\n", label, rp->r_pid);

			stop_service(rp,RS_EXITING);

			if (rp->r_pid == -1) {
				/* Process is already gone */
				rp->r_flags = 0;			/* release slot */
				if (rp->r_exec) {
					free(rp->r_exec);
					rp->r_exec= NULL;
				}

				proc = _ENDPOINT_P(rp->r_proc_nr_e);
				rproc_ptr[proc] = NULL;
				return 0;
			}

			/* Late reply - send a reply when process dies. */
			rp->r_flags |= RS_LATEREPLY;
			rp->r_caller = m_ptr->m_source;
			return -EDONTREPLY;
		}
	}

	if(rs_verbose) printf("RS: do_down: '%s' not found\n", label);

	return(-ESRCH);
}


/*===========================================================================*
 *				do_restart				     *
 *===========================================================================*/
int do_restart(message *m_ptr)
{
  register struct rproc *rp;
  size_t len;
  int s, proc, r;
  char label[MAX_LABEL_LEN];
  endpoint_t ep;

  len= m_ptr->RS_CMD_LEN;
  if (len >= sizeof(label))
	return -EINVAL;		/* Too long */

  s= sys_datacopy(m_ptr->m_source, (vir_bytes) m_ptr->RS_CMD_ADDR, 
  	SELF, (vir_bytes) label, len);
  if (s != 0) return(s);
  label[len]= '\0';

  /* This call requires special privileges. */
  if (! (caller_can_control(m_ptr->m_source, label) ||
		caller_is_root(m_ptr->m_source))) {
	return(-EPERM);
  }

  for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
      if ((rp->r_flags & RS_IN_USE) && strcmp(rp->r_label, label) == 0) {
	  if(rs_verbose) printf("RS: restarting '%s' (%d)\n", label, rp->r_pid);
	  if (rp->r_pid >= 0)
	  {
		if(rs_verbose)
		  printf("RS: do_restart: '%s' is (still) running, pid = %d\n",
			rp->r_pid);
		return -EBUSY;
	  }
	  rp->r_flags &= ~(RS_REFRESHING|RS_NOPINGREPLY);
	  r = start_service(rp, 0, &ep);	
	  if (r != 0) printf("do_restart: start_service failed: %d\n", r);
	  m_ptr->RS_ENDPOINT = ep;
	  return(r);
      }
  }
  if(rs_verbose) {
      printf("RS: do_restart: '%s' not found\n", label);
  }
  
  return(-ESRCH);
}


/*===========================================================================*
 *				do_refresh				     *
 *===========================================================================*/
int do_refresh(message *m_ptr)
{
  register struct rproc *rp;
  size_t len;
  int s;
  char label[MAX_LABEL_LEN];

  len= m_ptr->RS_CMD_LEN;
  if (len >= sizeof(label))
	return -EINVAL;		/* Too long */

  s= sys_datacopy(m_ptr->m_source, (vir_bytes) m_ptr->RS_CMD_ADDR, 
  	SELF, (vir_bytes) label, len);
  if (s != 0) return(s);
  label[len]= '\0';

  /* This call requires special privileges. */
  if (! (caller_can_control(m_ptr->m_source, label) ||
		caller_is_root(m_ptr->m_source))) {
	return(-EPERM);
  }

  for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
      if (rp->r_flags & RS_IN_USE && strcmp(rp->r_label, label) == 0) {

	if (rs_verbose) {
	  printf("RS: refreshing %s (%d)\n", rp->r_label, rp->r_pid);
	}
	stop_service(rp,RS_REFRESHING);
  return(0);
}
  }
	if (rs_verbose) {
		  printf("RS: do_refresh: '%s' not found\n", label);
	}

  return(-ESRCH);
}

/*===========================================================================*
 *				do_shutdown				     *
 *===========================================================================*/
int do_shutdown(message *m_ptr)
{
  /* This call requires special privileges. */
  if (m_ptr != NULL && !caller_is_root(m_ptr->m_source)) return(-EPERM);

  /* Set flag so that RS server knows services shouldn't be restarted. */
  shutting_down = TRUE;
  return 0;
}

/*===========================================================================*
 *				do_exit					     *
 *===========================================================================*/
void do_exit(message *m_ptr)
{
  register struct rproc *rp;
  pid_t exit_pid;
  int exit_status, r, slot_nr;
  endpoint_t ep;

  if(rs_verbose)
  printf("RS: got SIGCHLD signal, doing wait to get exited child.\n");

  /* See which child exited and what the exit status is. This is done in a
   * loop because multiple children may have exited, all reported by one 
   * SIGCHLD signal. The WNOHANG options is used to prevent blocking if, 
   * somehow, no exited child can be found. 
   */
  while ( (exit_pid = waitpid(-1, &exit_status, WNOHANG)) != 0 ) {

    if(rs_verbose) {
#if 0
      printf("RS: pid %d, ", exit_pid); 
#endif
      if (WIFSIGNALED(exit_status)) {
#if 0
          printf("killed, signal number %d\n", WTERMSIG(exit_status));
#endif
      } 
      else if (WIFEXITED(exit_status)) {
#if 0
          printf("normal exit, status %d\n", WEXITSTATUS(exit_status));
#endif
      }
    }

	/* Read from the exec pipe */
	for (;;)
	{
		r= read(exec_pipe[0], &slot_nr, sizeof(slot_nr));
		if (r == -1)
		{
			break;	/* No data */
		}
		if (r != sizeof(slot_nr))
		{
			panic("RS", "do_exit: unaligned read from exec pipe",
				r);
		}
		printf("do_exit: got slot %d\n", slot_nr);
		if (slot_nr < 0 || slot_nr >= NR_SYS_PROCS)
		{
			panic("RS", "do_exit: bad slot number from exec pipe",
				slot_nr);
		}
		rp= &rproc[slot_nr];
		rp->r_flags |= RS_EXITING;
	}

      /* Search the system process table to see who exited. 
       * This should always succeed. 
       */
      for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
          if ((rp->r_flags & RS_IN_USE) && rp->r_pid == exit_pid) {
	      int proc;
	      proc = _ENDPOINT_P(rp->r_proc_nr_e);

              rproc_ptr[proc] = NULL;		/* invalidate */
	      rp->r_pid= -1;

	      /* If PCI properties are set, inform the PCI driver. */
              if(rp->r_nr_pci_id || rp->r_nr_pci_class) {
                  pci_del_acl(rp->r_proc_nr_e);
              }

              if ((rp->r_flags & RS_EXITING) || shutting_down) {
		  /* No reply sent to RS_DOWN yet. */
		  if(rp->r_flags & RS_LATEREPLY) {
			message rsm;
			rsm.m_type = 0;
			kipc_send(rp->r_caller, &rsm);
		  }

		  /* Release slot. */
		  rp->r_flags = 0;
		  if (rp->r_exec)
		  {
			free(rp->r_exec);
			rp->r_exec= NULL;
		  }
	      }
	      else if(rp->r_flags & RS_REFRESHING) {
		      rp->r_restarts = -1;		/* reset counter */
		      if (rp->r_script[0] != '\0')
			run_script(rp);
		      else {
		        start_service(rp, 0, &ep); /* direct restart */
			if(m_ptr)
		  		m_ptr->RS_ENDPOINT = ep;
		      }
	      }
	      else {
                  /* Determine what to do. If this is the first unexpected 
		   * exit, immediately restart this service. Otherwise use
		   * a binary exponential backoff.
		   */
#if 0
rp->r_restarts= 0;
#endif
		  if (WIFSIGNALED(exit_status)) {
			switch(WTERMSIG(exit_status))
			{
			case SIGKILL:	rp->r_flags |= RS_KILLED; break;
			default: 	rp->r_flags |= RS_SIGNALED; break;
			}
		  } 
		  else
			rp->r_flags |= RS_CRASHED;

		  if (rp->r_script[0] != '\0') {
			if(rs_verbose)
				printf("RS: running restart script for %s\n",
					rp->r_cmd);
		      run_script(rp);
		  } else if (rp->r_restarts > 0) {
		      printf("RS: restarting %s, restarts %d\n",
				rp->r_cmd, rp->r_backoff);
		      rp->r_backoff = 1 << MIN(rp->r_restarts,(BACKOFF_BITS-2));
		      rp->r_backoff = MIN(rp->r_backoff,MAX_BACKOFF); 
		      if ((rp->r_sys_flags & SF_USE_COPY) && rp->r_backoff > 1)
			rp->r_backoff= 1;
		  }
		  else {
		      printf("RS: restarting %s\n", rp->r_cmd);
		      start_service(rp, 0, &ep);	/* direct restart */
		      if(m_ptr)
		  	      m_ptr->RS_ENDPOINT = ep;
			/* Do this even if no I/O happens with the ioctl, in
			 * order to disambiguate requests with DEV_IOCTL_S.
			 */
		  }
              }
	      break;
	  }
      }
  }
}

/*===========================================================================*
 *				do_period				     *
 *===========================================================================*/
void do_period(m_ptr)
message *m_ptr;
{
  register struct rproc *rp;
  clock_t now = m_ptr->NOTIFY_TIMESTAMP;
  int s;
  endpoint_t ep;

  /* Search system services table. Only check slots that are in use. */
  for (rp=BEG_RPROC_ADDR; rp<END_RPROC_ADDR; rp++) {
      if (rp->r_flags & RS_IN_USE) {

          /* If the service is to be revived (because it repeatedly exited, 
	   * and was not directly restarted), the binary backoff field is  
	   * greater than zero. 
	   */
	  if (rp->r_backoff > 0) {
              rp->r_backoff -= 1;
	      if (rp->r_backoff == 0) {
		  start_service(rp, 0, &ep);
	  	  m_ptr->RS_ENDPOINT = ep;
	      }
	  }

	  /* If the service was signaled with a SIGTERM and fails to respond,
	   * kill the system service with a SIGKILL signal.
	   */
	  else if (rp->r_stop_tm > 0 && now - rp->r_stop_tm > 2*RS_DELTA_T
	   && rp->r_pid > 0) {
              kill(rp->r_pid, SIGKILL);		/* terminate */
	  }
	
	  /* There seems to be no special conditions. If the service has a 
	   * period assigned check its status. 
	   */
	  else if (rp->r_period > 0) {

	      /* Check if an answer to a status request is still pending. If 
	       * the module didn't respond within time, kill it to simulate 
	       * a crash. The failure will be detected and the service will 
	       * be restarted automatically.
	       */
              if (rp->r_alive_tm < rp->r_check_tm) { 
	          if (now - rp->r_alive_tm > 2*rp->r_period &&
		      rp->r_pid > 0 && !(rp->r_flags & RS_NOPINGREPLY)) { 
		      if(rs_verbose)
                           printf("RS: service %d reported late\n",
				rp->r_proc_nr_e); 
		      rp->r_flags |= RS_NOPINGREPLY;
                      kill(rp->r_pid, SIGKILL);		/* simulate crash */
		  }
	      }

	      /* No answer pending. Check if a period expired since the last
	       * check and, if so request the system service's status.
	       */
	      else if (now - rp->r_check_tm > rp->r_period) {
#if 0
		if(rs_verbose)
                  printf("RS: status request sent to %d\n", rp->r_proc_nr_e);
#endif
		  kipc_notify(rp->r_proc_nr_e);		/* request status */
		  rp->r_check_tm = now;			/* mark time */
              }
          }
      }
  }

  /* Reschedule a synchronous alarm for the next period. */
  if ((s=sys_setalarm(RS_DELTA_T, 0)) != 0)
      panic("RS", "couldn't set alarm", s);
}


/*===========================================================================*
 *				start_service				     *
 *===========================================================================*/
static int start_service(rp, flags, endpoint)
struct rproc *rp;
int flags;
endpoint_t *endpoint;
{
/* Try to execute the given system service. Fork a new process. The child
 * process will be inhibited from running by the NO_PRIV flag. Only let the
 * child run once its privileges have been set by the parent.
 */
  int child_proc_nr_e, child_proc_nr_n;		/* child process slot */
  pid_t child_pid;				/* child's process id */
  char *file_only;
  int s, use_copy, slot_nr;
  bitchunk_t *vm_mask;
  message m;
  char * null_env = NULL;

  use_copy= (rp->r_sys_flags & SF_USE_COPY);

  /* See if we are not using a copy but we do need one to start the service. */
  if(!use_copy && (rp->r_sys_flags & SF_NEED_COPY)) {
	printf("RS: unable to start service %s without an in-memory copy\n",
	    rp->r_label);
	return(-EPERM);
  }

  /* Now fork and branch for parent and child process (and check for error). */
  if (use_copy) {
  if(rs_verbose) printf("RS: fork_nb..\n");
	child_pid= fork_nb();
  } else {
  if(rs_verbose) printf("RS: fork regular..\n");
  child_pid = fork();
  }

  switch(child_pid) {					/* see fork(2) */
  case -1:						/* fork failed */
      report("RS", "warning, fork() failed", errno);	/* shouldn't happen */
      return(errno);					/* return error */

  case 0:						/* child process */
      /* Try to execute the binary that has an absolute path. If this fails, 
       * e.g., because the root file system cannot be read, try to strip off
       * the path, and see if the command is in RS' current working dir.
       */
      nice(rp->r_nice);		/* Nice before setuid, to allow negative
				 * nice values.
				 */
      setuid(rp->r_uid);
      cpf_reload();			/* Tell kernel about grant table  */
      if (!use_copy)
      {
	execve(rp->r_argv[0], rp->r_argv, &null_env);	/* POSIX execute */
      file_only = strrchr(rp->r_argv[0], '/') + 1;
	execve(file_only, rp->r_argv, &null_env);	/* POSIX execute */
      }
      printf("RS: exec failed for %s: %d\n", rp->r_argv[0], errno);
      slot_nr= rp-rproc;
      s= write(exec_pipe[1], &slot_nr, sizeof(slot_nr));
      if (s != sizeof(slot_nr))
	printf("RS: write to exec pipe failed: %d/%d\n", s, errno);
      exit(1);						/* terminate child */

  default:						/* parent process */
#if 0
      if(rs_verbose) printf("RS: parent forked, pid %d..\n", child_pid);
#endif
      child_proc_nr_e = getnprocnr(child_pid);		/* get child slot */ 
#if 0
      if(rs_verbose) printf("RS: forked into %d..\n", child_proc_nr_e);
#endif
      break;						/* continue below */
  }

  /* Regardless of any following failures, there is now a child process.
   * Update the system process table that is maintained by the RS server.
   */
  child_proc_nr_n = _ENDPOINT_P(child_proc_nr_e);
  rp->r_flags = RS_IN_USE | flags;		/* mark slot in use */
  rp->r_restarts += 1;				/* raise nr of restarts */
  rp->r_proc_nr_e = child_proc_nr_e;		/* set child details */
  rp->r_pid = child_pid;
  rp->r_check_tm = 0;				/* not checked yet */
  getuptime(&rp->r_alive_tm); 			/* currently alive */
  rp->r_stop_tm = 0;				/* not exiting yet */
  rp->r_backoff = 0;				/* not to be restarted */
  rproc_ptr[child_proc_nr_n] = rp;		/* mapping for fast access */

  /* If any of the calls below fail, the RS_EXITING flag is set. This implies
   * that the process will be removed from RS's process table once it has
   * terminated. The assumption is that it is not useful to try to restart the
   * process later in these failure cases.
   */

  if (use_copy)
  {
	extern char **environ;

	/* Copy the executable image into the child process. If this call
	 * fails, the child process may or may not be killed already. If it is
	 * not killed, it's blocked because of NO_PRIV. Kill it now either way.
	 */
	s = dev_execve(child_proc_nr_e, rp->r_exec, rp->r_exec_len, rp->r_argv,
		environ);
	if (s != 0) {
		report("RS", "dev_execve call failed", s);
		kill(child_pid, SIGKILL);
		rp->r_flags |= RS_EXITING;	/* don't try again */
		return(s);
	}
  }

  /* Set resources when asked to. */
  if (rp->r_set_resources)
  {
	/* Initialize privilege structure. */
	init_privs(rp, &rp->r_priv);

	/* Tell VM about allowed calls. */
	vm_mask = &rp->r_vm[0];
	if ((s = vm_set_priv(child_proc_nr_e, vm_mask)) < 0) {
	    report("RS", "vm_set_priv call failed", s);
	    kill(child_pid, SIGKILL);
	    rp->r_flags |= RS_EXITING;
	    return (s);
	}
  }

  /* If PCI properties are set, inform the PCI driver about the new service. */
  if(rp->r_nr_pci_id || rp->r_nr_pci_class) {
      init_pci(rp, child_proc_nr_e);
  }

  /* The purpose of non-blocking forks is to avoid involving VFS in the forking
   * process, because VFS may be blocked on a sendrec() to a MFS that is
   * waiting for a endpoint update for a dead driver. We have just published
   * that update, but VFS may still be blocked. As a result, VFS may not yet
   * have received PM's fork message. Hence, if we call mapdriver5()
   * immediately, VFS may not know about the process and thus refuse to add the
   * driver entry. The following temporary hack works around this by forcing
   * blocking communication from PM to VFS. Once VFS has been made non-blocking
   * towards MFS instances, this hack and the entire fork_nb() call can go.
   */
  if (use_copy)
	setuid(0);

  /* Publish the new system service. */
  s = publish_service(rp);
  if (s != 0) {
	printf("RS: warning: publish_service failed: %d\n", s);
  }
  if (rp->r_dev_nr > 0) {				/* set driver map */
      if ((s=mapdriver5(rp->r_label, strlen(rp->r_label),
	      rp->r_dev_nr, rp->r_dev_style, !!use_copy /* force */)) < 0) {
          report("RS", "couldn't map driver (continuing)", errno);
      }
  }

  /* Set the privilege structure for the child process.
   * That will also cause the child process to start running.
   * This call should succeed: we tested number in use above.
   */
  if ((s = set_privs(child_proc_nr_e, &rp->r_priv, SYS_PRIV_SET_SYS)) != 0) {
      report("RS","set_privs failed", s);
      kill(child_pid, SIGKILL);				/* kill the module */
      rp->r_flags |= RS_EXITING;			/* expect exit */
      return(s);					/* return error */
  }

  if(rs_verbose)
      printf("RS: started '%s', major %d, pid %d, endpoint %d, proc %d\n", 
          rp->r_cmd, rp->r_dev_nr, child_pid,
	  child_proc_nr_e, child_proc_nr_n);

  /* The system service now has been successfully started. The only thing
   * that can go wrong now, is that execution fails at the child. If that's
   * the case, the child will exit. 
   */
  if(endpoint) *endpoint = child_proc_nr_e;	/* send back child endpoint */

  return(0);
}

/*===========================================================================*
 *				stop_service				     *
 *===========================================================================*/
static int stop_service(rp,how)
struct rproc *rp;
int how;
{
  /* Try to stop the system service. First send a SIGTERM signal to ask the
   * system service to terminate. If the service didn't install a signal 
   * handler, it will be killed. If it did and ignores the signal, we'll
   * find out because we record the time here and send a SIGKILL.
   */
  if(rs_verbose) printf("RS tries to stop %s (pid %d)\n", rp->r_cmd, rp->r_pid);

  rp->r_flags |= how;				/* what to on exit? */
  if(rp->r_pid > 0) kill(rp->r_pid, SIGTERM);	/* first try friendly */
  else if(rs_verbose) printf("RS: no process to kill\n");
  getuptime(&rp->r_stop_tm); 			/* record current time */
}


/*===========================================================================*
 *				do_getsysinfo				     *
 *===========================================================================*/
int do_getsysinfo(m_ptr)
message *m_ptr;
{
  vir_bytes src_addr, dst_addr;
  int dst_proc;
  size_t len;
  int s;

  /* This call requires special privileges. */
  if (!caller_is_root(m_ptr->m_source)) return(-EPERM);

  switch(m_ptr->m1_i1) {
  case SI_PROC_TAB:
  	src_addr = (vir_bytes) rproc;
  	len = sizeof(struct rproc) * NR_SYS_PROCS;
  	break; 
  default:
  	return(-EINVAL);
  }

  dst_proc = m_ptr->m_source;
  dst_addr = (vir_bytes) m_ptr->m1_p1;
  if ((s=sys_datacopy(SELF, src_addr, dst_proc, dst_addr, len)) != 0)
  	return(s);
  return 0;
}

static pid_t fork_nb()
{
  message m;

  return(ksyscall(PM_PROC_NR, KCNR_FORK_NB, &m));
}

static int copy_exec(rp_dst, rp_src)
struct rproc *rp_dst, *rp_src;
{
	/* Copy binary from rp_src to rp_dst. */
	rp_dst->r_exec_len = rp_src->r_exec_len;
	rp_dst->r_exec = malloc(rp_dst->r_exec_len);
	if(rp_dst->r_exec == NULL)
	        return -ENOMEM;

	memcpy(rp_dst->r_exec, rp_src->r_exec, rp_dst->r_exec_len);
	if(rp_dst->r_exec_len != 0 && rp_dst->r_exec != NULL)
	        return 0;
	        
        rp_dst->r_exec = NULL;
        return -EIO;
}

static int read_exec(rp)
struct rproc *rp;
{
	int e, r, fd;
	char *e_name;
	struct stat sb;


	e_name= rp->r_argv[0];
	r= stat(e_name, &sb);
	if (r != 0) 
		return -errno;

	fd= open(e_name, O_RDONLY);
	if (fd == -1)
		return -errno;

	rp->r_exec_len= sb.st_size;
	rp->r_exec= malloc(rp->r_exec_len);
	if (rp->r_exec == NULL)
	{
		printf("RS: read_exec: unable to allocate %d bytes\n",
			rp->r_exec_len);
		close(fd);
		return -ENOMEM;
	}

	r= read(fd, rp->r_exec, rp->r_exec_len);
	e= errno;
	close(fd);
	if (r == rp->r_exec_len)
		return 0;

	printf("RS: read_exec: read failed %d, errno %d\n", r, e);

	free(rp->r_exec);
	rp->r_exec= NULL;

	if (r >= 0)
		return -EIO;
	else
		return -e;
}

/*===========================================================================*
 *				run_script				     *
 *===========================================================================*/
static void run_script(rp)
struct rproc *rp;
{
	int r, proc_nr_e;
	pid_t pid;
	char *reason;
	char incarnation_str[20];	/* Enough for a counter? */
	char *envp[1] = { NULL };

	if (rp->r_flags & RS_REFRESHING)
		reason= "restart";
	else if (rp->r_flags & RS_NOPINGREPLY)
		reason= "no-heartbeat";
	else if (rp->r_flags & RS_KILLED)
		reason= "killed";
	else if (rp->r_flags & RS_CRASHED)
		reason= "crashed";
	else if (rp->r_flags & RS_SIGNALED)
		reason= "signaled";
	else
	{
		printf(
		"RS: run_script: can't find reason for termination of '%s'\n",
			rp->r_label);
		return;
	}
	sprintf(incarnation_str, "%d", rp->r_restarts);

 	if(rs_verbose) {
	  printf("RS: calling script '%s'\n", rp->r_script);
	  printf("RS: sevice name: '%s'\n", rp->r_label);
	  printf("RS: reason: '%s'\n", reason);
	  printf("RS: incarnation: '%s'\n", incarnation_str);
	}

	pid= fork();
	switch(pid)
	{
	case -1:	
		printf("RS: run_script: fork failed: %s\n", strerror(errno));
		break;
	case 0:
		execle(rp->r_script, rp->r_script, rp->r_label, reason,
			incarnation_str, NULL, envp);
		printf("RS: run_script: execl '%s' failed: %s\n",
			rp->r_script, strerror(errno));
		exit(1);
	default:
		/* Set the privilege structure for the child process and let it
		 * run.
		 */
		proc_nr_e = getnprocnr(pid);
		if ((r= set_privs(proc_nr_e, NULL, SYS_PRIV_SET_USER)) != 0) {
			printf("RS: run_script: set_privs call failed: %d\n",r);
		}
		/* Do not wait for the child */
		break;
	}
}


/*===========================================================================*
 *				get_next_label				     *
 *===========================================================================*/
static char *get_next_label(ptr, label, caller_label)
char *ptr;
char *label;
char *caller_label;
{
	/* Get the next label from the list of (IPC) labels.
	 */
	char *p, *q;
	size_t len;

	for (p= ptr; p[0] != '\0'; p= q)
	{
		/* Skip leading space */
		while (p[0] != '\0' && isspace((unsigned char)p[0]))
			p++;

		/* Find start of next word */
		q= p;
		while (q[0] != '\0' && !isspace((unsigned char)q[0]))
			q++;
		if (q == p)
			continue;
		len= q-p;
		if (len > MAX_LABEL_LEN)
		{
			printf(
	"rs:get_next_label: bad ipc list entry '.*s' for %s: too long\n",
				len, p, caller_label);
			continue;
		}
		memcpy(label, p, len);
		label[len]= '\0';

		return q; /* found another */
	}

	return NULL; /* done */
}

/*===========================================================================*
 *				add_forward_ipc				     *
 *===========================================================================*/
static void add_forward_ipc(rp, privp)
struct rproc *rp;
struct priv *privp;
{
	/* Add IPC send permissions to a process based on that process's IPC
	 * list.
	 */
	char label[MAX_LABEL_LEN+1], *p;
	struct rproc *tmp_rp;
	endpoint_t proc_nr_e;
	int r;
	int slot_nr, priv_id;
	struct priv priv;

	p = rp->r_ipc_list;

	while ((p = get_next_label(p, label, rp->r_label)) != NULL) {

		if (strcmp(label, "SYSTEM") == 0)
			proc_nr_e= SYSTEM;
		else if (strcmp(label, "USER") == 0)
			proc_nr_e= INIT_PROC_NR; /* all user procs */
		else if (strcmp(label, "PM") == 0)
			proc_nr_e= PM_PROC_NR;
		else if (strcmp(label, "VFS") == 0)
			proc_nr_e= FS_PROC_NR;
		else if (strcmp(label, "RS") == 0)
			proc_nr_e= RS_PROC_NR;
		else if (strcmp(label, "LOG") == 0)
			proc_nr_e= LOG_PROC_NR;
		else if (strcmp(label, "TTY") == 0)
			proc_nr_e= TTY_PROC_NR;
		else if (strcmp(label, "DS") == 0)
			proc_nr_e= DS_PROC_NR;
		else if (strcmp(label, "VM") == 0)
			proc_nr_e= VM_PROC_NR;
		else
		{
			/* Try to find process */
			for (slot_nr = 0; slot_nr < NR_SYS_PROCS;
				slot_nr++)
			{
				tmp_rp = &rproc[slot_nr];
				if (!(tmp_rp->r_flags & RS_IN_USE))
					continue;
				if (strcmp(tmp_rp->r_label, label) == 0)
					break;
			}
			if (slot_nr >= NR_SYS_PROCS)
			{
				if (rs_verbose)
					printf(
			"add_forward_ipc: unable to find '%s'\n", label);
				continue;
			}
			proc_nr_e= tmp_rp->r_proc_nr_e;
		}

		if ((r = sys_getpriv(&priv, proc_nr_e)) < 0)
		{
			printf(
		"add_forward_ipc: unable to get priv_id for '%s': %d\n",
				label, r);
			continue;
		}
		priv_id= priv.s_id;
		set_sys_bit(privp->s_ipc_to, priv_id);
	}
}


/*===========================================================================*
 *				add_backward_ipc			     *
 *===========================================================================*/
static void add_backward_ipc(rp, privp)
struct rproc *rp;
struct priv *privp;
{
	/* Add IPC send permissions to a process based on other processes' IPC
	 * lists. This is enough to allow each such two processes to talk to
	 * each other, as the kernel guarantees send mask symmetry. We need to
	 * add these permissions now because the current process may not yet
	 * have existed at the time that the other process was initialized.
	 */
	char label[MAX_LABEL_LEN+1], *p;
	struct rproc *rrp;
	int priv_id, found;

	for (rrp=BEG_RPROC_ADDR; rrp<END_RPROC_ADDR; rrp++) {
		if (!(rrp->r_flags & RS_IN_USE))
			continue;

		/* If an IPC target list was provided for the process being
		 * checked here, make sure that the label of the new process
		 * is in that process's list.
		 */
		if (rrp->r_ipc_list[0]) {
			found = 0;

			p = rrp->r_ipc_list;

			while ((p = get_next_label(p, label, rp->r_label)) !=
									NULL) {
				if (!strcmp(rp->r_label, label)) {
					found = 1;
					break;
				}
			}

			if (!found)
				continue;
		}

		priv_id= rrp->r_priv.s_id;

		set_sys_bit(privp->s_ipc_to, priv_id);
	}
}


/*===========================================================================*
 *				init_privs				     *
 *===========================================================================*/
static void init_privs(rp, privp)
struct rproc *rp;
struct priv *privp;
{
	int i, src_bits_per_word, dst_bits_per_word, src_word, dst_word,
		src_bit, call_nr;
	unsigned long mask;

	/* Clear s_k_call_mask */
	memset(privp->s_k_call_mask, '\0', sizeof(privp->s_k_call_mask));

	src_bits_per_word= 8*sizeof(rp->r_call_mask[0]);
	dst_bits_per_word= 8*sizeof(privp->s_k_call_mask[0]);
	for (src_word= 0; src_word < RSS_NR_SYSTEM; src_word++)
	{
		for (src_bit= 0; src_bit < src_bits_per_word; src_bit++)
		{
			mask= (1UL << src_bit);
			if (!(rp->r_call_mask[src_word] & mask))
				continue;
			call_nr= src_word*src_bits_per_word+src_bit;
#if 0
			if(rs_verbose)
			  printf("RS: init_privs: system call %d\n", call_nr);
#endif
			dst_word= call_nr / dst_bits_per_word;
			mask= (1UL << (call_nr % dst_bits_per_word));
			if (dst_word >= CALL_MASK_SIZE)
			{
				printf(
				"RS: init_privs: call number %d doesn't fit\n",
					call_nr);
			}
			privp->s_k_call_mask[dst_word] |= mask;
		}
	}

	/* Clear s_ipc_to */
	memset(&privp->s_ipc_to, '\0', sizeof(privp->s_ipc_to));

	if (strlen(rp->r_ipc_list) != 0)
	{
		add_forward_ipc(rp, privp);
		add_backward_ipc(rp, privp);

	}
	else
	{
		for (i= 0; i<NR_SYS_PROCS; i++)
		{
			if (i != USER_PRIV_ID)
				set_sys_bit(privp->s_ipc_to, i);
		}
	}
}

/*===========================================================================*
 *				set_privs				     *
 *===========================================================================*/
static int set_privs(endpoint, privp, req)
endpoint_t endpoint;
struct priv *privp;
int req;
{
  int r;

  /* Set the privilege structure. */
  if ((r = sys_privctl(endpoint, req, privp)) != 0) {
      return r;
  }

  /* Synch the privilege structure with the kernel for system services. */
  if(req == SYS_PRIV_SET_SYS) {
      if ((r = sys_getpriv(privp, endpoint)) != 0) {
          return r;
      }
  }

  /* Allow the process to run. */
  if ((r = sys_privctl(endpoint, SYS_PRIV_ALLOW, NULL)) != 0) {
      return r;
  }

  return(0);
}

/*===========================================================================*
 *				init_pci				     *
 *===========================================================================*/
static void init_pci(rp, endpoint)
struct rproc *rp;
int endpoint;
{
	/* Inform the PCI driver about the new module. */
	size_t len;
	int i, r;
	struct rs_pci rs_pci;

	if (strcmp(rp->r_label, "pci") == 0)
	{
		if(rs_verbose)
			printf("RS: init_pci: not when starting 'pci'\n");
		return;
	}

	len= strlen(rp->r_label);
	if (len+1 > sizeof(rs_pci.rsp_label))
	{
		if(rs_verbose)
		  printf("RS: init_pci: label '%s' too long for rsp_label\n",
			rp->r_label);
		return;
	}
	strcpy(rs_pci.rsp_label, rp->r_label);
	rs_pci.rsp_endpoint= endpoint;

	rs_pci.rsp_nr_device= rp->r_nr_pci_id;
	if (rs_pci.rsp_nr_device > RSP_NR_DEVICE)
	{
		printf("RS: init_pci: too many PCI devices (max %d) "
		  "truncating\n",
			RSP_NR_DEVICE);
		rs_pci.rsp_nr_device= RSP_NR_DEVICE;
	}
	for (i= 0; i<rs_pci.rsp_nr_device; i++)
	{
		rs_pci.rsp_device[i].vid= rp->r_pci_id[i].vid;
		rs_pci.rsp_device[i].did= rp->r_pci_id[i].did;
	}

	rs_pci.rsp_nr_class= rp->r_nr_pci_class;
	if (rs_pci.rsp_nr_class > RSP_NR_CLASS)
	{
		printf("RS: init_pci: too many PCI classes "
		   "(max %d) truncating\n",
			RSP_NR_CLASS);
		rs_pci.rsp_nr_class= RSP_NR_CLASS;
	}
	for (i= 0; i<rs_pci.rsp_nr_class; i++)
	{
		rs_pci.rsp_class[i].class= rp->r_pci_class[i].class;
		rs_pci.rsp_class[i].mask= rp->r_pci_class[i].mask;
	}

	if(rs_verbose)
		printf("RS: init_pci: calling pci_set_acl\n");

	r= pci_set_acl(&rs_pci);

	if(rs_verbose)
		printf("RS: init_pci: after pci_set_acl\n");

	if (r != 0)
	{
		printf("RS: init_pci: pci_set_acl failed: %s\n",
			strerror(errno));
		return;
	}
}

/*===========================================================================*
 *				do_lookup				     *
 *===========================================================================*/
int do_lookup(m_ptr)
message *m_ptr;
{
	static char namebuf[100];
	int len, r;
	struct rproc *rrp;

	len = m_ptr->RS_NAME_LEN;

	if(len < 2 || len >= sizeof(namebuf)) {
		printf("RS: len too weird (%d)\n", len);
		return -EINVAL;
	}

	if((r=sys_vircopy(m_ptr->m_source, D, (vir_bytes) m_ptr->RS_NAME,
		SELF, D, (vir_bytes) namebuf, len)) != 0) {
		printf("RS: name copy failed\n");
		return r;

	}

	namebuf[len] = '\0';

	for (rrp=BEG_RPROC_ADDR; rrp<END_RPROC_ADDR; rrp++) {
		if (!(rrp->r_flags & RS_IN_USE))
			continue;
		if (!strcmp(rrp->r_label, namebuf)) {
			m_ptr->RS_ENDPOINT = rrp->r_proc_nr_e;
			return 0;
		}
	}

	return -ESRCH;
}

