/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#include <nucleos/callnr.h>
#include <nucleos/com.h>
#include <nucleos/const.h>
#include <servers/ds/ds.h>
#include <nucleos/endpoint.h>
#include <nucleos/keymap.h>
#include <nucleos/minlib.h>
#include <nucleos/type.h>
#include <nucleos/ipc.h>
#include <nucleos/sysutil.h>
#include <nucleos/syslib.h>

#include <nucleos/errno.h>
#include <assert.h>
#include <env.h>

#include <servers/vm/glo.h>
#include <servers/vm/proto.h>
#include <servers/vm/util.h>
#include <servers/vm/vm.h>
#include <servers/vm/region.h>
#include <servers/vm/sanitycheck.h>
#include <asm/pagetable.h>
#include <asm/servers/vm/memory.h>


static int new_mem(struct vmproc *vmp, struct vmproc *sh_vmp, vir_bytes text_bytes,
		   vir_bytes data_bytes, vir_bytes bss_bytes, vir_bytes stk_bytes,
		   phys_bytes tot_bytes);

/*===========================================================================*
 *                              find_share                                   *
 *===========================================================================*/
struct vmproc *find_share(vmp_ign, ino, dev, ctime)
struct vmproc *vmp_ign;         /* process that should not be looked at */
ino_t ino;                      /* parameters that uniquely identify a file */
dev_t dev;
time_t ctime;
{
/* Look for a process that is the file <ino, dev, ctime> in execution.  Don't
 * accidentally "find" vmp_ign, because it is the process on whose behalf this
 * call is made.
 */
  struct vmproc *vmp;
  for (vmp = &vmproc[0]; vmp < &vmproc[NR_PROCS]; vmp++) {
        if (!(vmp->vm_flags & VMF_INUSE)) continue;
        if (!(vmp->vm_flags & VMF_SEPARATE)) continue;
        if (vmp->vm_flags & VMF_HASPT) continue;
        if (vmp == vmp_ign) continue;
        if (vmp->vm_ino != ino) continue;
        if (vmp->vm_dev != dev) continue;
        if (vmp->vm_ctime != ctime) continue;
        return vmp;
  }
  return(NULL);
}


/*===========================================================================*
 *				exec_newmem				     *
 *===========================================================================*/
int do_exec_newmem(message *msg)
{
	int r, proc_e, proc_n;
	vir_bytes stack_top;
	vir_clicks tc, dc, sc, totc, dvir, s_vir;
	struct vmproc *vmp, *sh_mp;
	char *ptr;
	struct exec_newmem args;

	SANITYCHECK(SCL_FUNCTIONS);

	proc_e= msg->VMEN_ENDPOINT;

	if (vm_isokendpt(proc_e, &proc_n) != 0)
	{
		printf("VM:exec_newmem: bad endpoint %d from %d\n",
			proc_e, msg->m_source);
		return -ESRCH;
	}
	vmp= &vmproc[proc_n];
	ptr= msg->VMEN_ARGSPTR;

	if (msg->VMEN_ARGSSIZE != sizeof(args)) {
		printf("VM:exec_newmem: args size %d != %ld\n",
			msg->VMEN_ARGSSIZE, sizeof(args));
		return -EINVAL;
	}

	SANITYCHECK(SCL_DETAIL);

	r= sys_datacopy(msg->m_source, (vir_bytes)ptr,
		SELF, (vir_bytes)&args, sizeof(args));
	if (r != 0)
		vm_panic("exec_newmem: sys_datacopy failed", r);

	/* Check to see if segment sizes are feasible. */
	tc = ((unsigned long) args.text_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
	dc = (args.data_bytes+args.bss_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
	totc = (args.tot_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
	sc = (args.args_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;

	if (dc >= totc) return(-ENOEXEC); /* stack must be at least 1 click */

	dvir = (args.sep_id ? 0 : tc);
	s_vir = dvir + (totc - sc);

	r = (dvir + dc > s_vir) ? -ENOMEM : 0;

	if (r != 0)
		return r;

	/* Can the process' text be shared with that of one already running? */
	if(!vm_paged) {
		sh_mp = find_share(vmp, args.st_ino, args.st_dev, args.st_ctime);
	} else {
		sh_mp = NULL;
	}

	/* Allocate new memory and release old memory.  Fix map and tell
	 * kernel.
	 */
	r = new_mem(vmp, sh_mp, args.text_bytes, args.data_bytes,
		args.bss_bytes, args.args_bytes, args.tot_bytes);
	if (r != 0) return(r);

	/* Save file identification to allow it to be shared. */
	vmp->vm_ino = args.st_ino;
	vmp->vm_dev = args.st_dev;
	vmp->vm_ctime = args.st_ctime;

	stack_top= ((vir_bytes)vmp->vm_arch.vm_seg[S].mem_vir << CLICK_SHIFT) +
		((vir_bytes)vmp->vm_arch.vm_seg[S].mem_len << CLICK_SHIFT);

	/* set/clear separate I&D flag */
	if (args.sep_id)
		vmp->vm_flags |= VMF_SEPARATE;	
	else
		vmp->vm_flags &= ~VMF_SEPARATE;

	msg->VMEN_STACK_TOP = (void *) stack_top;
	msg->VMEN_FLAGS = 0;

	if (!sh_mp)			 /* Load text if sh_mp = NULL */
		msg->VMEN_FLAGS |= EXC_NM_RF_LOAD_TEXT;

	return 0;
}

/*===========================================================================*
 *				new_mem					     *
 *===========================================================================*/
static int new_mem(rmp, sh_mp, text_bytes, data_bytes,
	bss_bytes,stk_bytes,tot_bytes)
struct vmproc *rmp;		/* process to get a new memory map */
struct vmproc *sh_mp;		/* text can be shared with this process */
vir_bytes text_bytes;		/* text segment size in bytes */
vir_bytes data_bytes;		/* size of initialized data in bytes */
vir_bytes bss_bytes;		/* size of bss in bytes */
vir_bytes stk_bytes;		/* size of initial stack segment in bytes */
phys_bytes tot_bytes;		/* total memory to allocate, including gap */
{
/* Allocate new memory and release the old memory.  Change the map and report
 * the new map to the kernel.  Zero the new core image's bss, gap and stack.
 */

  vir_clicks text_clicks, data_clicks, gap_clicks, stack_clicks, tot_clicks;
  phys_bytes bytes, base, bss_offset;
  int s, r2;

  SANITYCHECK(SCL_FUNCTIONS);

  /* No need to allocate text if it can be shared. */
  if (sh_mp != NULL) {
	text_bytes = 0;
	vm_assert(!vm_paged);
  }

  /* Acquire the new memory.  Each of the 4 parts: text, (data+bss), gap,
   * and stack occupies an integral number of clicks, starting at click
   * boundary.  The data and bss parts are run together with no space.
   */
  text_clicks = ((unsigned long) text_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
  data_clicks = (data_bytes + bss_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
  stack_clicks = (stk_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
  tot_clicks = (tot_bytes + CLICK_SIZE - 1) >> CLICK_SHIFT;
  gap_clicks = tot_clicks - data_clicks - stack_clicks;
  if ( (int) gap_clicks < 0) return(-ENOMEM);

SANITYCHECK(SCL_DETAIL);


  /* We've got memory for the new core image.  Release the old one. */

  if(rmp->vm_flags & VMF_HASPT) {
  	/* Free page table and memory allocated by pagetable functions. */
	rmp->vm_flags &= ~VMF_HASPT;
	free_proc(rmp);
  } else {

  	if (find_share(rmp, rmp->vm_ino, rmp->vm_dev, rmp->vm_ctime) == NULL) {
		/* No other process shares the text segment, so free it. */
		FREE_MEM(rmp->vm_arch.vm_seg[T].mem_phys, rmp->vm_arch.vm_seg[T].mem_len);
  	}

  	/* Free the data and stack segments. */
  	FREE_MEM(rmp->vm_arch.vm_seg[D].mem_phys,
		rmp->vm_arch.vm_seg[S].mem_vir
		+ rmp->vm_arch.vm_seg[S].mem_len
		- rmp->vm_arch.vm_seg[D].mem_vir);
  }

  /* We have now passed the point of no return.  The old core image has been
   * forever lost, memory for a new core image has been allocated.  Set up
   * and report new map.
   */

  if(vm_paged) {
	if(pt_new(&rmp->vm_pt) != 0)
		vm_panic("exec_newmem: no new pagetable", NO_NUM);

	SANITYCHECK(SCL_DETAIL);
	proc_new(rmp,
	 VM_PROCSTART,	/* where to start the process in the page table */
	 CLICK2ABS(text_clicks),/* how big is the text in bytes, page-aligned */
	 CLICK2ABS(data_clicks),/* how big is data+bss, page-aligned */
	 CLICK2ABS(stack_clicks),/* how big is stack, page-aligned */
	 CLICK2ABS(gap_clicks),	/* how big is gap, page-aligned */
	 0,0,			/* not preallocated */
	 VM_STACKTOP		/* regular stack top */
	 );
	SANITYCHECK(SCL_DETAIL);
  } else {
  	phys_clicks new_base;

	new_base = ALLOC_MEM(text_clicks + tot_clicks, 0);
	if (new_base == NO_MEM) return(-ENOMEM);

	if (sh_mp != NULL) {
		/* Share the text segment. */
		rmp->vm_arch.vm_seg[T] = sh_mp->vm_arch.vm_seg[T];
	} else {
		rmp->vm_arch.vm_seg[T].mem_phys = new_base;
		rmp->vm_arch.vm_seg[T].mem_vir = 0;
		rmp->vm_arch.vm_seg[T].mem_len = text_clicks;
	
		if (text_clicks > 0)
		{
			/* Zero the last click of the text segment. Otherwise the
			 * part of that click may remain unchanged.
			 */
			base = (phys_bytes)(new_base+text_clicks-1) << CLICK_SHIFT;
			if ((s= sys_memset(0, base, CLICK_SIZE)) != 0)
				vm_panic("new_mem: sys_memset failed", s);
		}
  	}

	/* No paging stuff. */
	rmp->vm_flags &= ~VMF_HASPT;
	rmp->vm_regions = NULL;

	  rmp->vm_arch.vm_seg[D].mem_phys = new_base + text_clicks;
	  rmp->vm_arch.vm_seg[D].mem_vir = 0;
	  rmp->vm_arch.vm_seg[D].mem_len = data_clicks;
	  rmp->vm_arch.vm_seg[S].mem_phys = rmp->vm_arch.vm_seg[D].mem_phys +
		data_clicks + gap_clicks;
	  rmp->vm_arch.vm_seg[S].mem_vir = rmp->vm_arch.vm_seg[D].mem_vir +
		data_clicks + gap_clicks;
	  rmp->vm_arch.vm_seg[S].mem_len = stack_clicks;
	rmp->vm_stacktop =
		CLICK2ABS(rmp->vm_arch.vm_seg[S].mem_vir +
			rmp->vm_arch.vm_seg[S].mem_len);

	rmp->vm_arch.vm_data_top = 
		(rmp->vm_arch.vm_seg[S].mem_vir + 
		rmp->vm_arch.vm_seg[S].mem_len) << CLICK_SHIFT;

	  if((r2=sys_newmap(rmp->vm_endpoint, rmp->vm_arch.vm_seg)) != 0) {
		/* report new map to the kernel */
		vm_panic("sys_newmap failed", r2);
	  }

	  /* Zero the bss, gap, and stack segment. */
	  bytes = (phys_bytes)(data_clicks + gap_clicks + stack_clicks) << CLICK_SHIFT;
	  base = (phys_bytes) rmp->vm_arch.vm_seg[D].mem_phys << CLICK_SHIFT;
	  bss_offset = (data_bytes >> CLICK_SHIFT) << CLICK_SHIFT;
	  base += bss_offset;
	  bytes -= bss_offset;

	  if ((s=sys_memset(0, base, bytes)) != 0) {
		vm_panic("new_mem can't zero", s);
	  }

	  /* Tell kernel this thing has no page table. */
	  if((s=pt_bind(NULL, rmp)) != 0)
		vm_panic("exec_newmem: pt_bind failed", s);
  }

SANITYCHECK(SCL_FUNCTIONS);

  return 0;
}

/*===========================================================================*
 *				find_kernel_top				     *
 *===========================================================================*/
phys_bytes find_kernel_top(void)
{
/* Find out where the kernel is, so we know where to start mapping
 * user processes.
 */
	u32_t kernel_top = 0;
#define MEMTOP(v, i) \
  (vmproc[v].vm_arch.vm_seg[i].mem_phys + vmproc[v].vm_arch.vm_seg[i].mem_len)
	vm_assert(vmproc[VMP_SYSTEM].vm_flags & VMF_INUSE);
	kernel_top = MEMTOP(VMP_SYSTEM, T);
	kernel_top = MAX(kernel_top, MEMTOP(VMP_SYSTEM, D));
	kernel_top = MAX(kernel_top, MEMTOP(VMP_SYSTEM, S));
	vm_assert(kernel_top);

	return CLICK2ABS(kernel_top);
}

/*===========================================================================*
 *				proc_new				     *
 *===========================================================================*/
int proc_new(struct vmproc *vmp,
  phys_bytes vstart,	  /* where to start the process in page table */
  phys_bytes text_bytes,  /* how much code, in bytes but page aligned */
  phys_bytes data_bytes,  /* how much data + bss, in bytes but page aligned */
  phys_bytes stack_bytes, /* stack space to reserve, in bytes, page aligned */
  phys_bytes gap_bytes,   /* gap bytes, page aligned */
  phys_bytes text_start,  /* text starts here, if preallocated, otherwise 0 */
  phys_bytes data_start,  /* data starts here, if preallocated, otherwise 0 */
  phys_bytes stacktop
)
{
	int s;
	vir_bytes hole_bytes;
	int prealloc;

	vm_assert(!(vstart % VM_PAGE_SIZE));
	vm_assert(!(text_bytes % VM_PAGE_SIZE));
	vm_assert(!(data_bytes % VM_PAGE_SIZE));
	vm_assert(!(stack_bytes % VM_PAGE_SIZE));
	vm_assert(!(gap_bytes % VM_PAGE_SIZE));
	vm_assert(!(text_start % VM_PAGE_SIZE));
	vm_assert(!(data_start % VM_PAGE_SIZE));
	vm_assert((!text_start && !data_start) || (text_start && data_start));

#if 0
	if(!map_proc_kernel(vmp)) {
		printf("VM: exec: map_proc_kernel failed\n");
		return -ENOMEM;
	}
#endif

	/* Place text at start of process. */
	vmp->vm_arch.vm_seg[T].mem_phys = ABS2CLICK(vstart);
	vmp->vm_arch.vm_seg[T].mem_vir = 0;
	vmp->vm_arch.vm_seg[T].mem_len = ABS2CLICK(text_bytes);

	vmp->vm_offset = vstart;

	/* page mapping flags for code */
#define TEXTFLAGS (PTF_PRESENT | PTF_USER)
	SANITYCHECK(SCL_DETAIL);
	if(text_bytes > 0) {
		if(!map_page_region(vmp, vstart, 0, text_bytes,
		  text_start ? text_start : MAP_NONE,
		  VR_ANON | VR_WRITABLE, text_start ? 0 : MF_PREALLOC)) {
			SANITYCHECK(SCL_DETAIL);
			printf("VM: proc_new: map_page_region failed (text)\n");
			return(-ENOMEM);
		}
		SANITYCHECK(SCL_DETAIL);
	}
	SANITYCHECK(SCL_DETAIL);

	/* Allocate memory for data (including bss, but not including gap
	 * or stack), make sure it's cleared, and map it in after text
	 * (if any).
	 */
	if(!(vmp->vm_heap = map_page_region(vmp, vstart + text_bytes, 0,
	  data_bytes, data_start ? data_start : MAP_NONE, VR_ANON | VR_WRITABLE,
		data_start ? 0 : MF_PREALLOC))) {
		printf("VM: exec: map_page_region for data failed\n");
		return -ENOMEM;
	}

	/* Tag the heap so brk() call knows which region to extend. */
	map_region_set_tag(vmp->vm_heap, VRT_HEAP);

	/* How many address space clicks between end of data
	 * and start of stack?
	 * stacktop is the first address after the stack, as addressed
	 * from within the user process.
	 */
	hole_bytes = stacktop - data_bytes - stack_bytes - gap_bytes;

	if(!map_page_region(vmp, vstart + text_bytes + data_bytes + hole_bytes,
	  0, stack_bytes + gap_bytes, MAP_NONE,
	  VR_ANON | VR_WRITABLE, 0) != 0) {
	  	vm_panic("map_page_region failed for stack", NO_NUM);
	}

	vmp->vm_arch.vm_seg[D].mem_phys = ABS2CLICK(vstart + text_bytes);
	vmp->vm_arch.vm_seg[D].mem_vir = 0;
	vmp->vm_arch.vm_seg[D].mem_len = ABS2CLICK(data_bytes);

	vmp->vm_arch.vm_seg[S].mem_phys = ABS2CLICK(vstart +
		text_bytes + data_bytes + gap_bytes + hole_bytes);
	vmp->vm_arch.vm_seg[S].mem_vir = ABS2CLICK(data_bytes + gap_bytes + hole_bytes);

	/* Pretend the stack is the full size of the data segment, so 
	 * we get a full-sized data segment, up to VM_DATATOP.
	 * After sys_newmap(), change the stack to what we know the
	 * stack to be (up to stacktop).
	 */
	vmp->vm_arch.vm_seg[S].mem_len = (VM_DATATOP >> CLICK_SHIFT) -
		vmp->vm_arch.vm_seg[S].mem_vir - ABS2CLICK(vstart) - ABS2CLICK(text_bytes);

	/* Where are we allowed to start using the rest of the virtual
	 * address space?
	 */
	vmp->vm_stacktop = stacktop;

	/* What is the final size of the data segment in bytes? */
	vmp->vm_arch.vm_data_top = 
		(vmp->vm_arch.vm_seg[S].mem_vir + 
		vmp->vm_arch.vm_seg[S].mem_len) << CLICK_SHIFT;

	vmp->vm_flags |= VMF_HASPT;

	if((s=sys_newmap(vmp->vm_endpoint, vmp->vm_arch.vm_seg)) != 0) {
		vm_panic("sys_newmap (vm) failed", s);
	}


	/* This is the real stack clicks. */
	vmp->vm_arch.vm_seg[S].mem_len = ABS2CLICK(stack_bytes);

	if((s=pt_bind(&vmp->vm_pt, vmp)) != 0)
		vm_panic("exec_newmem: pt_bind failed", s);

	return 0;
}
