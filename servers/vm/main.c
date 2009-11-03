/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */

#define VERBOSE 0
#include <nucleos/kernel.h>
#include <nucleos/unistd.h>
#include <nucleos/com.h>
#include <nucleos/const.h>
#include <servers/ds/ds.h>
#include <nucleos/endpoint.h>
#include <nucleos/keymap.h>
#include <nucleos/minlib.h>
#include <nucleos/type.h>
#include <nucleos/kipc.h>
#include <nucleos/sysutil.h>
#include <nucleos/syslib.h>
#include <nucleos/bitmap.h>
#include <nucleos/mman.h>

#include <nucleos/errno.h>
#include <nucleos/string.h>
#include <env.h>
#include <stdio.h>

#include <servers/vm/glo.h>
#include <servers/vm/proto.h>
#include <servers/vm/util.h>
#include <servers/vm/vm.h>
#include <servers/vm/sanitycheck.h>
#include <kernel/const.h>
#include <kernel/proc.h>
#include <asm/servers/vm/memory.h>
#include <asm/kernel/types.h>

extern int missing_spares;

struct vmproc vmproc[VMP_NR];

#if SANITYCHECKS
int nocheck;
int incheck;
long vm_sanitychecklevel;
#endif

/* vm operation mode state and values */
long vm_paged;

int meminit_done;

typedef u32_t mask_t;

#define MINEPM 0
#define MAXMASK (sizeof(mask_t)*8)
#define ANYEPM (MINEPM+MAXMASK-1)
#define NEEDACL (MINEPM+MAXMASK-2)
#define MAXEPM (NEEDACL-1)
#define EPM(e) ((1L) << ((e)-MINEPM))
#define EPMOK(mask, ep) (((mask) & EPM(ANYEPM)) || ((ep) >= MINEPM && (ep) <= MAXEPM && (EPM(ep) & (mask))))

/* Table of calls and a macro to test for being in range. */
struct {
	mask_t vmc_callers;		/* bitmap of endpoint numbers */
	int (*vmc_func)(message *);	/* Call handles message. */
	char *vmc_name;			/* Human-readable string. */
} vm_calls[VM_NCALLS];

/* Macro to verify call range and map 'high' range to 'base' range
 * (starting at 0) in one. Evaluates to zero-based call number if call
 * number is valid, returns -1 otherwise.
 */
#define CALLNUMBER(c) (((c) >= VM_RQ_BASE && 				\
			(c) < VM_RQ_BASE + ELEMENTS(vm_calls)) ?	\
			((c) - VM_RQ_BASE) : -1)

static void vm_init(void);
static int vm_acl_ok(endpoint_t caller, int call);


/*===========================================================================*
 *				main					     *
 *===========================================================================*/
int main(void)
{
  message msg;
  int result, who_e;

#if SANITYCHECKS
  incheck = nocheck = 0;
  FIXME("VM SANITYCHECKS are on");
#endif

  vm_paged = 1;
  env_parse("vm_paged", "d", 0, &vm_paged, 0, 1);
#if SANITYCHECKS
  env_parse("vm_sanitychecklevel", "d", 0, &vm_sanitychecklevel, 0, SCL_MAX);
#endif

  vm_init();

  /* This is VM's main loop. */
  while (TRUE) {
	int r, c;

	SANITYCHECK(SCL_TOP);
	if(missing_spares > 0) {
		pt_cycle();	/* pagetable code wants to be called */
	}
	SANITYCHECK(SCL_DETAIL);

  	if ((r=kipc_receive(ANY, &msg)) != 0)
		vm_panic("receive() error", r);

	SANITYCHECK(SCL_DETAIL);

	if(msg.m_type & NOTIFY_MESSAGE) {
		switch(msg.m_source) {
			case SYSTEM:
				/* Kernel wants to have memory ranges
				 * verified, and/or pagefaults handled.
				 */
				do_memory();
				break;
			case HARDWARE:
				do_pagefaults();
				break;
			case PM_PROC_NR:
				/* PM sends a notify() on shutdown, which
				 * is OK and we ignore.
				 */
				break;
			default:
				/* No-one else should send us notifies. */
				printf("VM: ignoring notify() from %d\n",
					msg.m_source);
				break;
		}
		continue;
	}
	who_e = msg.m_source;
	c = CALLNUMBER(msg.m_type);
	result = -ENOSYS; /* Out of range or restricted calls return this. */
	if(c < 0 || !vm_calls[c].vmc_func) {
		printf("VM: out of range or missing callnr %d from %d\n",
			msg.m_type, who_e);
	} else if (vm_acl_ok(who_e, c) != 0) {
		printf("VM: unauthorized %s by %d\n",
			vm_calls[c].vmc_name, who_e);
	} else {
	SANITYCHECK(SCL_FUNCTIONS);
		result = vm_calls[c].vmc_func(&msg);
	SANITYCHECK(SCL_FUNCTIONS);
	}

	/* Send reply message, unless the return code is SUSPEND,
	 * which is a pseudo-result suppressing the reply message.
	 */
	if(result != SUSPEND) {
	SANITYCHECK(SCL_DETAIL);
		msg.m_type = result;
		if((r=kipc_send(who_e, &msg)) != 0) {
			printf("VM: couldn't send %d to %d (err %d)\n",
				msg.m_type, who_e, r);
			vm_panic("send() error", NO_NUM);
		}
	SANITYCHECK(SCL_DETAIL);
	}
	SANITYCHECK(SCL_DETAIL);
  }
  return 0;
}

extern int unmap_ok;

/*===========================================================================*
 *				vm_init					     *
 *===========================================================================*/
static void vm_init(void)
{
	int s, i;
	int click, clicksforgotten = 0;
	struct memory mem_chunks[NR_MEMS];
	struct boot_image image[NR_BOOT_PROCS];
	struct boot_image *ip;

	/* Get chunks of available memory. */
	get_mem_chunks(mem_chunks);

	/* Initialize VM's process table. Request a copy of the system
	 * image table that is defined at the kernel level to see which
	 * slots to fill in.
	 */
	if ((s=sys_getimage(image)) != 0)
		vm_panic("couldn't get image table: %d\n", s);

	/* Set table to 0. This invalidates all slots (clear VMF_INUSE). */
	memset(vmproc, 0, sizeof(vmproc));

	for(i = 0; i < ELEMENTS(vmproc); i++) {
		vmproc[i].vm_slot = i;
	}

	/* Walk through boot-time system processes that are alive
	 * now and make valid slot entries for them.
	 */
	for (ip = &image[0]; ip < &image[NR_BOOT_PROCS]; ip++) {
		struct vmproc *vmp;

		if(ip->proc_nr >= NR_PROCS) { vm_panic("proc", ip->proc_nr); }
		if(ip->proc_nr < 0 && ip->proc_nr != SYSTEM) continue;

#define GETVMP(v, nr)						\
		if(nr >= 0) {					\
			vmp = &vmproc[ip->proc_nr];		\
		} else if(nr == SYSTEM) {			\
			vmp = &vmproc[VMP_SYSTEM];		\
		} else {					\
			vm_panic("init: crazy proc_nr", nr);	\
		}

		/* Initialize normal process table slot or special SYSTEM
		 * table slot. Kernel memory is already reserved.
		 */
		GETVMP(vmp, ip->proc_nr);

		/* reset fields as if exited */
		clear_proc(vmp);

		/* Get memory map for this process from the kernel. */
		if ((s=get_mem_map(ip->proc_nr, vmp->vm_arch.vm_seg)) != 0)
			vm_panic("couldn't get process mem_map",s);

		/* Remove this memory from the free list. */
		reserve_proc_mem(mem_chunks, vmp->vm_arch.vm_seg);

		vmp->vm_flags = VMF_INUSE;
		vmp->vm_endpoint = ip->endpoint;
		vmp->vm_stacktop =
			CLICK2ABS(vmp->vm_arch.vm_seg[S].mem_vir +
				vmp->vm_arch.vm_seg[S].mem_len);

		if (vmp->vm_arch.vm_seg[T].mem_len != 0)
			vmp->vm_flags |= VMF_SEPARATE;
	}

#ifndef CONFIG_BUILTIN_INITRD
	/* Remove initrd memory from the free list. We must do it right after we
	   have reserved memory for boot image otherwise it may happen that initrd
	   will be overwritten by other process (in arch_init_vm).
	 */
	if ((s = reserve_initrd_mem(mem_chunks)) < 0) {
		panic("VM", "Couldn't reserve memory for initial ramdisk!", s);
	}
#endif
	/* Architecture-dependent initialization. */
	pt_init();

	/* Initialize tables to all physical memory. */
	mem_init(mem_chunks);
	meminit_done = 1;

	/* Give these processes their own page table. */
	for (ip = &image[0]; ip < &image[NR_BOOT_PROCS]; ip++) {
		int s;
		struct vmproc *vmp;
		vir_bytes old_stacktop, old_stack;

		if(ip->proc_nr < 0) continue;

		GETVMP(vmp, ip->proc_nr);

		if(!(ip->flags & PROC_FULLVM)) {
			/* See if this process fits in kernel
			 * mapping. VM has its own pagetable,
			 * don't check it.
			 */
			if(!(vmp->vm_flags & VMF_HASPT)) {
				pt_check(vmp);
			}
			continue;
		}

		old_stack = 
			vmp->vm_arch.vm_seg[S].mem_vir +
			vmp->vm_arch.vm_seg[S].mem_len - 
			vmp->vm_arch.vm_seg[D].mem_len;

        	if(pt_new(&vmp->vm_pt) != 0)
			vm_panic("vm_init: no new pagetable", NO_NUM);
#define BASICSTACK VM_PAGE_SIZE
		old_stacktop = CLICK2ABS(vmp->vm_arch.vm_seg[S].mem_vir +
				vmp->vm_arch.vm_seg[S].mem_len);
		if(sys_vmctl(vmp->vm_endpoint, VMCTL_INCSP,
			VM_STACKTOP - old_stacktop) != 0) {
			vm_panic("VM: vmctl for new stack failed", NO_NUM);
		}

		FREE_MEM(vmp->vm_arch.vm_seg[D].mem_phys +
			vmp->vm_arch.vm_seg[D].mem_len,
			old_stack);

		if(proc_new(vmp,
			VM_PROCSTART,
			CLICK2ABS(vmp->vm_arch.vm_seg[T].mem_len),
			CLICK2ABS(vmp->vm_arch.vm_seg[D].mem_len),
			BASICSTACK,
			CLICK2ABS(vmp->vm_arch.vm_seg[S].mem_vir +
				vmp->vm_arch.vm_seg[S].mem_len -
				vmp->vm_arch.vm_seg[D].mem_len) - BASICSTACK,
			CLICK2ABS(vmp->vm_arch.vm_seg[T].mem_phys),
			CLICK2ABS(vmp->vm_arch.vm_seg[D].mem_phys),
				VM_STACKTOP) != 0) {
			vm_panic("failed proc_new for boot process", NO_NUM);
		}
	}

	/* Set up table of calls. */
#define CALLMAP(code, func, thecaller) { int i;			      \
	if((i=CALLNUMBER(code)) < 0) { vm_panic(#code " invalid", (code)); } \
	if(i >= VM_NCALLS) { vm_panic(#code " invalid", (code)); } \
	vm_calls[i].vmc_func = (func); 				      \
	vm_calls[i].vmc_name = #code; 				      \
	if(((thecaller) < MINEPM || (thecaller) > MAXEPM) 		\
		&& (thecaller) != ANYEPM				\
		&& (thecaller) != NEEDACL ) {				\
		vm_panic(#thecaller " invalid", (code));  		\
	}								\
	vm_calls[i].vmc_callers |= EPM(thecaller);		      \
}

	/* Set call table to 0. This invalidates all calls (clear
	 * vmc_func).
	 */
	memset(vm_calls, 0, sizeof(vm_calls));

	/* Requests from PM (restricted to be from PM only). */
	CALLMAP(VM_EXIT, do_exit, PM_PROC_NR);
	CALLMAP(VM_FORK, do_fork, PM_PROC_NR);
	CALLMAP(VM_BRK, do_brk, PM_PROC_NR);
	CALLMAP(VM_EXEC_NEWMEM, do_exec_newmem, PM_PROC_NR);
	CALLMAP(VM_PUSH_SIG, do_push_sig, PM_PROC_NR);
	CALLMAP(VM_WILLEXIT, do_willexit, PM_PROC_NR);
	CALLMAP(VM_ADDDMA, do_adddma, PM_PROC_NR);
	CALLMAP(VM_DELDMA, do_deldma, PM_PROC_NR);
	CALLMAP(VM_GETDMA, do_getdma, PM_PROC_NR);
	CALLMAP(VM_ALLOCMEM, do_allocmem, PM_PROC_NR);
	CALLMAP(VM_NOTIFY_SIG, do_notify_sig, PM_PROC_NR);

	/* Requests from RS */
	CALLMAP(VM_RS_SET_PRIV, do_rs_set_priv, RS_PROC_NR);

	/* Requests from userland (source unrestricted). */
	CALLMAP(VM_MMAP, do_mmap, ANYEPM);
	CALLMAP(VM_MUNMAP, do_munmap, ANYEPM);
	CALLMAP(VM_MUNMAP_TEXT, do_munmap, ANYEPM);
	CALLMAP(VM_MAP_PHYS, do_map_phys, ANYEPM); /* Does its own checking. */
	CALLMAP(VM_UNMAP_PHYS, do_unmap_phys, ANYEPM);

	/* Requests from userland (anyone can call but need an ACL bit). */
	CALLMAP(VM_REMAP, do_remap, NEEDACL);
	CALLMAP(VM_GETPHYS, do_get_phys, NEEDACL);
	CALLMAP(VM_SHM_UNMAP, do_shared_unmap, NEEDACL);
	CALLMAP(VM_GETREF, do_get_refcount, NEEDACL);
	CALLMAP(VM_CTL, do_ctl, NEEDACL);
	CALLMAP(VM_QUERY_EXIT, do_query_exit, NEEDACL);

	/* Sanity checks */
	if(find_kernel_top() >= VM_PROCSTART)
		vm_panic("kernel loaded too high", NO_NUM);

	/* Initialize the structures for queryexit */
	init_query_exit();

	/* Unmap our own low pages. */
	unmap_ok = 1;
	unmap_page_zero();
}

/*===========================================================================*
 *				vm_acl_ok				     *
 *===========================================================================*/
static int vm_acl_ok(endpoint_t caller, int call)
{
	int n, r;

	/* Some calls are always allowed by some, or all, processes. */
	if(EPMOK(vm_calls[call].vmc_callers, caller)) {
		return 0;
	}

	if ((r = vm_isokendpt(caller, &n)) != 0)
		vm_panic("VM: from strange source.", caller);

	/* Other calls need an ACL bit. */
	if (!(vm_calls[call].vmc_callers & EPM(NEEDACL))) {
		return -EPERM;
	}
	if (!GET_BIT(vmproc[n].vm_call_priv_mask, call)) {
		printf("VM: no ACL for %s for %d\n",
			vm_calls[call].vmc_name, caller);
		return -EPERM;
	}

	return 0;
}
