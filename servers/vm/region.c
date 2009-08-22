/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */

#define _SYSTEM 1

#include <nucleos/com.h>
#include <nucleos/callnr.h>
#include <nucleos/type.h>
#include <nucleos/const.h>
#include <nucleos/sysutil.h>
#include <nucleos/syslib.h>

#include <sys/mman.h>

#include <nucleos/limits.h>
#include <string.h>
#include <nucleos/errno.h>
#include <assert.h>
#include <nucleos/types.h>

#include <servers/vm/vm.h>
#include <servers/vm/proto.h>
#include <servers/vm/util.h>
#include <servers/vm/glo.h>
#include <servers/vm/region.h>
#include <servers/vm/sanitycheck.h>
#include <asm/servers/vm/memory.h>

static int map_new_physblock(struct vmproc *vmp, struct vir_region *region, vir_bytes offset,
			     vir_bytes length, phys_bytes what, struct phys_region *physhint);

static int map_copy_ph_block(struct vmproc *vmp, struct vir_region *region, struct phys_region *ph);
static struct vir_region *map_copy_region(struct vir_region *);

static void map_printmap(struct vmproc *vmp);

static char *map_name(struct vir_region *vr)
{
	int type = vr->flags & (VR_ANON|VR_DIRECT);
	switch(type) {
		case VR_ANON:
			return "anonymous";
		case VR_DIRECT:
			return "direct";
		default:
			vm_panic("unknown mapping type", type);
	}

	return "NOTREACHED";
}

/*===========================================================================*
 *				map_printmap				     *
 *===========================================================================*/
static void map_printmap(vmp)
struct vmproc *vmp;
{
	struct vir_region *vr;
	printf("memory regions in process %d:\n", vmp->vm_endpoint);
	for(vr = vmp->vm_regions; vr; vr = vr->next) {
		struct phys_region *ph;
		int nph = 0;
		printf("\t0x%lx - 0x%lx (len 0x%lx), %s\n",
			vr->vaddr, vr->vaddr + vr->length, vr->length,
			vr->vaddr + vr->length, map_name(vr));
		printf("\t\tphysical: ");
		for(ph = vr->first; ph; ph = ph->next) {
			printf("0x%lx-0x%lx (refs %d): phys 0x%lx ",
				vr->vaddr + ph->ph->offset,
				vr->vaddr + ph->ph->offset + ph->ph->length,
				ph->ph->refcount,
				ph->ph->phys);
			nph++;
		}
		printf(" (phregions %d)\n", nph);
	}
}


#if SANITYCHECKS


/*===========================================================================*
 *				map_sanitycheck			     *
 *===========================================================================*/
void map_sanitycheck(char *file, int line)
{
	struct vmproc *vmp;

/* Macro for looping over all physical blocks of all regions of
 * all processes.
 */
#define ALLREGIONS(regioncode, physcode)			\
	for(vmp = vmproc; vmp <= &vmproc[NR_PROCS]; vmp++) {	\
		struct vir_region *vr;				\
		if(!(vmp->vm_flags & VMF_INUSE))		\
			continue;				\
		for(vr = vmp->vm_regions; vr; vr = vr->next) {	\
			struct phys_region *pr;			\
			regioncode;				\
			for(pr = vr->first; pr; pr = pr->next) { \
				physcode;			\
			}					\
		}						\
	}

#define MYSLABSANE(s) MYASSERT(slabsane(s, sizeof(*(s))))
	/* Basic pointers check. */
	ALLREGIONS(MYSLABSANE(vr),MYSLABSANE(pr); MYSLABSANE(pr->ph);MYSLABSANE(pr->parent));
	ALLREGIONS(MYASSERT(vr->parent == vmp),MYASSERT(pr->parent == vr););

	/* Do counting for consistency check. */
	ALLREGIONS(;,pr->ph->seencount = 0;);
	ALLREGIONS(;,pr->ph->seencount++;);

	/* Do consistency check. */
	ALLREGIONS(if(vr->next) {
			MYASSERT(vr->vaddr < vr->next->vaddr);
			MYASSERT(vr->vaddr + vr->length <= vr->next->vaddr);
		}
		MYASSERT(!(vr->vaddr % VM_PAGE_SIZE));,	
		if(pr->ph->refcount != pr->ph->seencount) {
			map_printmap(vmp);
			printf("ph in vr 0x%lx: 0x%lx-0x%lx  refcount %d "
				"but seencount %lu\n", 
				vr, pr->ph->offset,
				pr->ph->offset + pr->ph->length,
				pr->ph->refcount, pr->ph->seencount);
		}
		{
			int n_others = 0;
			struct phys_region *others;
			if(pr->ph->refcount > 0) {
				MYASSERT(pr->ph->firstregion);
				if(pr->ph->refcount == 1) {
					MYASSERT(pr->ph->firstregion == pr);
				}
			} else {
				MYASSERT(!pr->ph->firstregion);
			}
			for(others = pr->ph->firstregion; others;
				others = others->next_ph_list) {
				MYSLABSANE(others);
				MYASSERT(others->ph == pr->ph);
				n_others++;
			}
			MYASSERT(pr->ph->refcount == n_others);
		}
		MYASSERT(pr->ph->refcount == pr->ph->seencount);
		MYASSERT(!(pr->ph->offset % VM_PAGE_SIZE));
		MYASSERT(!(pr->ph->length % VM_PAGE_SIZE)););
}
#endif


/*=========================================================================*
 *				map_ph_writept				*
 *=========================================================================*/
int map_ph_writept(struct vmproc *vmp, struct vir_region *vr,
	struct phys_block *pb, int *ropages, int *rwpages)
{
	int rw;

	vm_assert(!(vr->vaddr % VM_PAGE_SIZE));
	vm_assert(!(pb->length % VM_PAGE_SIZE));
	vm_assert(!(pb->offset % VM_PAGE_SIZE));
	vm_assert(pb->refcount > 0);

	if((vr->flags & VR_WRITABLE)
	&& (pb->refcount == 1 || (vr->flags & VR_DIRECT)))
		rw = PTF_WRITE;
	else
		rw = 0;

#if SANITYCHECKS
	if(rwpages && ropages && (vr->flags & VR_ANON)) {
		int pages;
		pages = pb->length / VM_PAGE_SIZE;
		if(rw)
			(*rwpages) += pages;
		else
			(*ropages) += pages;
	}
#endif

	if(pt_writemap(&vmp->vm_pt, vr->vaddr + pb->offset,
	  pb->phys, pb->length, PTF_PRESENT | PTF_USER | rw,
		WMF_OVERWRITE) != OK) {
	    printf("VM: map_writept: pt_writemap failed\n");
	    return ENOMEM;
	}

	return OK;
}

/*===========================================================================*
 *				map_page_region				     *
 *===========================================================================*/
struct vir_region *map_page_region(vmp, minv, maxv, length,
	what, flags, mapflags)
struct vmproc *vmp;
vir_bytes minv;
vir_bytes maxv;
vir_bytes length;
vir_bytes what;
u32_t flags;
int mapflags;
{
	struct vir_region *vr, *prevregion = NULL, *newregion,
		*firstregion = vmp->vm_regions;
	vir_bytes startv;
	int foundflag = 0;

	SANITYCHECK(SCL_FUNCTIONS);

	/* We must be in paged mode to be able to do this. */
	vm_assert(vm_paged);

	/* Length must be reasonable. */
	vm_assert(length > 0);

	/* Special case: allow caller to set maxv to 0 meaning 'I want
	 * it to be mapped in right here.'
	 */
        if(maxv == 0) {
                maxv = minv + length;

                /* Sanity check. */
                if(maxv <= minv) {
                        printf("map_page_region: minv 0x%lx and bytes 0x%lx\n",
                                minv, length);
			map_printmap(vmp);
                        return NULL;
                }
        }

	/* Basic input sanity checks. */
	vm_assert(!(length % VM_PAGE_SIZE));
	if(minv >= maxv) {
		printf("VM: 1 minv: 0x%lx maxv: 0x%lx length: 0x%lx\n",
			minv, maxv, length);
	}
	vm_assert(minv < maxv);
	vm_assert(minv + length <= maxv);

#define FREEVRANGE(rangestart, rangeend, foundcode) {		\
	vir_bytes frstart = (rangestart), frend = (rangeend);	\
	frstart = MAX(frstart, minv);				\
	frend   = MIN(frend, maxv);				\
	if(frend > frstart && (frend - frstart) >= length) {	\
		startv = frstart;				\
		foundflag = 1;					\
		foundcode;					\
	} }

	/* This is the free virtual address space before the first region. */
	FREEVRANGE(0, firstregion ? firstregion->vaddr : VM_DATATOP, ;);

	if(!foundflag) {
		for(vr = vmp->vm_regions; vr && !foundflag; vr = vr->next) {
			FREEVRANGE(vr->vaddr + vr->length,
			  vr->next ? vr->next->vaddr : VM_DATATOP,
				prevregion = vr;);
		}
	}

	if(!foundflag) {
		printf("VM: map_page_region: no 0x%lx bytes found for %d between 0x%lx and 0x%lx\n",
			length, vmp->vm_endpoint, minv, maxv);
		map_printmap(vmp);
		return NULL;
	}

#if SANITYCHECKS
	if(prevregion) vm_assert(prevregion->vaddr < startv);
#endif

	/* However we got it, startv must be in the requested range. */
	vm_assert(startv >= minv);
	vm_assert(startv < maxv);
	vm_assert(startv + length <= maxv);

	/* Now we want a new region. */
	if(!SLABALLOC(newregion)) {
		printf("VM: map_page_region: allocating region failed\n");
		return NULL;
	}

	/* Fill in node details. */
	newregion->vaddr = startv;
	newregion->length = length;
	newregion->first = NULL;
	newregion->flags = flags;
	newregion->tag = VRT_NONE;
	newregion->parent = vmp;

	/* If we know what we're going to map to, map it right away. */
	if(what != MAP_NONE) {
		vm_assert(!(what % VM_PAGE_SIZE));
		vm_assert(!(length % VM_PAGE_SIZE));
		vm_assert(!(startv % VM_PAGE_SIZE));
		vm_assert(!newregion->first);
		vm_assert(!(mapflags & MF_PREALLOC));
		if(map_new_physblock(vmp, newregion, 0, length, what, NULL) != OK) {
			printf("VM: map_new_physblock failed\n");
			SLABFREE(newregion);
			return NULL;
		}
		vm_assert(newregion->first);
		vm_assert(!newregion->first->next);
		if(map_ph_writept(vmp, newregion, newregion->first->ph, NULL, NULL) != OK) {
			printf("VM: map_region_writept failed\n");
			SLABFREE(newregion);
			return NULL;
		}
	}

	if((flags & VR_ANON) && (mapflags & MF_PREALLOC)) {
		if(map_handle_memory(vmp, newregion, 0, length, 1) != OK) {
			printf("VM:map_page_region: prealloc failed\n");
			SLABFREE(newregion);
			return NULL;
		}
	}

	/* Link it. */
	if(prevregion) {
		vm_assert(prevregion->vaddr < newregion->vaddr);
		newregion->next = prevregion->next;
		prevregion->next = newregion;
	} else {
		newregion->next = vmp->vm_regions;
		vmp->vm_regions = newregion;
	}

#if SANITYCHECKS
	vm_assert(startv == newregion->vaddr);
	if(newregion->next) {
		vm_assert(newregion->vaddr < newregion->next->vaddr);
	}
#endif

	SANITYCHECK(SCL_FUNCTIONS);

	return newregion;
}

/*===========================================================================*
 *				pb_unreferenced				     *
 *===========================================================================*/
void pb_unreferenced(struct vir_region *region, struct phys_region *pr)
{
	struct phys_block *pb;
	int remap = 0;

	SLABSANE(pr);
	pb = pr->ph;
	SLABSANE(pb);
	vm_assert(pb->refcount > 0);
	pb->refcount--;
	vm_assert(pb->refcount >= 0);

	SLABSANE(pb->firstregion);
	if(pb->firstregion == pr) {
		pb->firstregion = pr->next_ph_list;
		if(pb->firstregion) {
			SLABSANE(pb->firstregion);
		}
	} else {
		struct phys_region *others;

		for(others = pb->firstregion; others;
			others = others->next_ph_list) {
			SLABSANE(others);
			vm_assert(others->ph == pb);
			if(others->next_ph_list == pr) {
				others->next_ph_list = pr->next_ph_list;
				break;
			}
		}

		vm_assert(others); /* Otherwise, wasn't on the list. */
	}

	if(pb->refcount == 0) {
		vm_assert(!pb->firstregion);
		if(region->flags & VR_ANON) {
			FREE_MEM(ABS2CLICK(pb->phys),
				ABS2CLICK(pb->length));
		} else if(region->flags & VR_DIRECT) {
			; /* No action required. */
		} else {
			vm_panic("strange phys flags", NO_NUM);
		}
		SLABFREE(pb);
	} else {
		SLABSANE(pb->firstregion);
		/* If a writable piece of physical memory is now only
		 * referenced once, map it writable right away instead of
		 * waiting for a page fault.
		 */
		if(pb->refcount == 1 && (region->flags & VR_WRITABLE)) {
			vm_assert(pb);
			vm_assert(pb->firstregion);
			vm_assert(!pb->firstregion->next_ph_list);
			vm_assert(pb->firstregion->ph == pb);
			vm_assert(pb->firstregion->ph == pb);
			SLABSANE(pb);
			SLABSANE(pb->firstregion);
			SLABSANE(pb->firstregion->parent);
			if(map_ph_writept(pb->firstregion->parent->parent,
				pb->firstregion->parent, pb, NULL, NULL) != OK) {
				vm_panic("pb_unreferenced: writept", NO_NUM);
			}
		}
	}
}

/*===========================================================================*
 *				map_free				     *
 *===========================================================================*/
static int map_free(struct vir_region *region)
{
	struct phys_region *pr, *nextpr;

#if SANITYCHECKS
	for(pr = region->first; pr; pr = pr->next) {
		struct phys_region *others;
		struct phys_block *pb;

		SLABSANE(pr);
		pb = pr->ph;
		SLABSANE(pb);
		SLABSANE(pb->firstregion);

		for(others = pb->firstregion; others;
			others = others->next_ph_list) {
			SLABSANE(others);
			vm_assert(others->ph == pb);
		}
	}
#endif

	for(pr = region->first; pr; pr = nextpr) {
		SANITYCHECK(SCL_DETAIL);
		pb_unreferenced(region, pr);
		nextpr = pr->next;
		region->first = nextpr; /* For sanity checks. */
		SLABFREE(pr);
	}

	SLABFREE(region);

	return OK;
}

/*========================================================================*
 *				map_free_proc				  *
 *========================================================================*/
int map_free_proc(vmp)
struct vmproc *vmp;
{
	struct vir_region *r, *nextr;

	SANITYCHECK(SCL_FUNCTIONS);

	for(r = vmp->vm_regions; r; r = nextr) {
		nextr = r->next;
		SANITYCHECK(SCL_DETAIL);
#if SANITYCHECKS
		nocheck++;
#endif
		map_free(r);
		vmp->vm_regions = nextr;	/* For sanity checks. */
#if SANITYCHECKS
		nocheck--;
#endif
		SANITYCHECK(SCL_DETAIL);
	}

	vmp->vm_regions = NULL;

	SANITYCHECK(SCL_FUNCTIONS);

	return OK;
}

/*===========================================================================*
 *				map_lookup				     *
 *===========================================================================*/
struct vir_region *map_lookup(vmp, offset)
struct vmproc *vmp;
vir_bytes offset;
{
	struct vir_region *r;

	SANITYCHECK(SCL_FUNCTIONS);

	if(!vmp->vm_regions)
		vm_panic("process has no regions", vmp->vm_endpoint);

	for(r = vmp->vm_regions; r; r = r->next) {
		if(offset >= r->vaddr && offset < r->vaddr + r->length)
			return r;
	}

	SANITYCHECK(SCL_FUNCTIONS);

	return NULL;
}


/*===========================================================================*
 *				map_new_physblock			     *
 *===========================================================================*/
static int map_new_physblock(vmp, region, offset, length, what_mem, physhint)
struct vmproc *vmp;
struct vir_region *region;
vir_bytes offset;
vir_bytes length;
phys_bytes what_mem;
struct phys_region *physhint;
{
	struct phys_region *newphysr;
	struct phys_block *newpb;
	phys_bytes mem_clicks, clicks;
	vir_bytes mem;

	SANITYCHECK(SCL_FUNCTIONS);

	vm_assert(!(length % VM_PAGE_SIZE));
	if(!physhint) physhint = region->first;

	/* Allocate things necessary for this chunk of memory. */
	if(!SLABALLOC(newphysr))
		return ENOMEM;
	if(!SLABALLOC(newpb)) {
		SLABFREE(newphysr);
		return ENOMEM;
	}

	/* Memory for new physical block. */
	clicks = CLICKSPERPAGE * length / VM_PAGE_SIZE;
	if(what_mem == MAP_NONE) {
		u32_t af = PAF_CLEAR;
		if(region->flags & VR_PHYS64K)
			af |= PAF_ALIGN64K;
		if((mem_clicks = ALLOC_MEM(clicks, af)) == NO_MEM) {
			SLABFREE(newpb);
			SLABFREE(newphysr);
			return ENOMEM;
		}
		mem = CLICK2ABS(mem_clicks);
	} else {
		mem = what_mem;
	}
	SANITYCHECK(SCL_DETAIL);

	/* New physical block. */
	newpb->phys = mem;
	newpb->refcount = 1;
	newpb->offset = offset;
	newpb->length = length;
	newpb->firstregion = newphysr;
	SLABSANE(newpb->firstregion);

	/* New physical region. */
	newphysr->ph = newpb;
	newphysr->parent = region;
	newphysr->next_ph_list = NULL;	/* No other references to this block. */

	/* Update pagetable. */
	vm_assert(!(length % VM_PAGE_SIZE));
	vm_assert(!(newpb->length % VM_PAGE_SIZE));
	SANITYCHECK(SCL_DETAIL);
	if(map_ph_writept(vmp, region, newpb, NULL, NULL) != OK) {
		if(what_mem == MAP_NONE)
			FREE_MEM(mem_clicks, clicks);
		SLABFREE(newpb);
		SLABFREE(newphysr);
		return ENOMEM;
	}

	if(!region->first || offset < region->first->ph->offset) {
		/* Special case: offset is before start. */
		if(region->first) {
			vm_assert(offset + length <= region->first->ph->offset);
		}
		newphysr->next = region->first;
		region->first = newphysr;
	} else {
		struct phys_region *physr;
		for(physr = physhint; physr; physr = physr->next) {
			if(!physr->next || physr->next->ph->offset > offset) {
				newphysr->next = physr->next;
				physr->next = newphysr;
				break;
			}
		}

		/* Loop must have put the node somewhere. */
		vm_assert(physr->next == newphysr);
	}

	SANITYCHECK(SCL_FUNCTIONS);

	return OK;
}


/*===========================================================================*
 *				map_copy_ph_block			     *
 *===========================================================================*/
static int map_copy_ph_block(vmp, region, ph)
struct vmproc *vmp;
struct vir_region *region;
struct phys_region *ph;
{
	int r;
	phys_bytes newmem, newmem_cl, clicks;
	struct phys_block *newpb;
	u32_t af = 0;

	SANITYCHECK(SCL_FUNCTIONS);

	/* This is only to be done if there is more than one copy. */
	vm_assert(ph->ph->refcount > 1);

	/* Do actual copy on write; allocate new physblock. */
	if(!SLABALLOC(newpb)) {
		printf("VM: map_copy_ph_block: couldn't allocate newpb\n");
		SANITYCHECK(SCL_FUNCTIONS);
		return ENOMEM;
	}

	clicks = CLICKSPERPAGE * ph->ph->length / VM_PAGE_SIZE;
	vm_assert(CLICK2ABS(clicks) == ph->ph->length);
	if(region->flags & VR_PHYS64K)
		af |= PAF_ALIGN64K;
	if((newmem_cl = ALLOC_MEM(clicks, af)) == NO_MEM) {
		SLABFREE(newpb);
		return ENOMEM;
	}
	newmem = CLICK2ABS(newmem_cl);
	vm_assert(ABS2CLICK(newmem) == newmem_cl);

	pb_unreferenced(region, ph);
	SLABSANE(ph);
	SLABSANE(ph->ph);
	vm_assert(ph->ph->refcount > 0);
	newpb->length = ph->ph->length;
	newpb->offset = ph->ph->offset;
	newpb->refcount = 1;
	newpb->phys = newmem;
	newpb->firstregion = ph;
	ph->next_ph_list = NULL;

	/* Copy old memory to new memory. */
	if((r=sys_abscopy(ph->ph->phys, newpb->phys, newpb->length)) != OK) {
		printf("VM: map_copy_ph_block: sys_abscopy failed\n");
		SANITYCHECK(SCL_FUNCTIONS);
		return r;
	}

#if VMSTATS
	vmp->vm_bytecopies += newpb->length;
#endif

	/* Reference new block. */
	ph->ph = newpb;

	/* Check reference counts. */
	SANITYCHECK(SCL_DETAIL);

	/* Update pagetable with new address.
	 * This will also make it writable.
	 */
	r = map_ph_writept(vmp, region, ph->ph, NULL, NULL);
	if(r != OK)
		vm_panic("map_copy_ph_block: map_ph_writept failed", r);

	SANITYCHECK(SCL_FUNCTIONS);

	return OK;
}

/*===========================================================================*
 *				map_pf			     *
 *===========================================================================*/
int map_pf(vmp, region, offset, write)
struct vmproc *vmp;
struct vir_region *region;
vir_bytes offset;
int write;
{
	vir_bytes virpage;
	struct phys_region *ph;
	int r;

	vm_assert(offset >= 0);
	vm_assert(offset < region->length);

	vm_assert(region->flags & VR_ANON);
	vm_assert(!(region->vaddr % VM_PAGE_SIZE));

	virpage = offset - offset % VM_PAGE_SIZE;

	SANITYCHECK(SCL_FUNCTIONS);

	for(ph = region->first; ph; ph = ph->next)
		if(ph->ph->offset <= offset && offset < ph->ph->offset + ph->ph->length)
			break;

	if(ph) {
		/* Pagefault in existing block. Do copy-on-write. */
		vm_assert(write);
		vm_assert(region->flags & VR_WRITABLE);
		vm_assert(ph->ph->refcount > 0);

		if(ph->ph->refcount == 1)
			r = map_ph_writept(vmp, region, ph->ph, NULL, NULL);
		else
			r = map_copy_ph_block(vmp, region, ph);
	} else {
		/* Pagefault in non-existing block. Map in new block. */
#if 0
		if(!write) {
			printf("VM: read from uninitialized memory by %d\n",
				vmp->vm_endpoint);
		}
#endif
		r = map_new_physblock(vmp, region, virpage, VM_PAGE_SIZE,
			MAP_NONE, region->first);
	}

	if(r != OK)
		printf("VM: map_pf: failed (%d)\n", r);

	SANITYCHECK(SCL_FUNCTIONS);

	return r;
}

/*===========================================================================*
 *				map_handle_memory			     *
 *===========================================================================*/
int map_handle_memory(vmp, region, offset, length, write)
struct vmproc *vmp;
struct vir_region *region;
vir_bytes offset, length;
int write;
{
	struct phys_region *physr;
	int changes = 0;

#define FREE_RANGE_HERE(er1, er2) {					\
	struct phys_region *r1 = (er1), *r2 = (er2);			\
	vir_bytes start = offset, end = offset + length;		\
	if(r1) { start = MAX(start, r1->ph->offset + r1->ph->length); }	\
	if(r2) { end   = MIN(end, r2->ph->offset); }			\
	if(start < end) {						\
		int r;							\
		SANITYCHECK(SCL_DETAIL);				\
		if((r=map_new_physblock(vmp, region, start,		\
			end-start, MAP_NONE, r1 ? r1 : r2)) != OK) {	\
			SANITYCHECK(SCL_DETAIL);			\
			return r;					\
		}							\
		changes++;						\
	} }

	SANITYCHECK(SCL_FUNCTIONS);

	vm_assert(region->flags & VR_ANON);
	vm_assert(!(region->vaddr % VM_PAGE_SIZE));
	vm_assert(!(offset % VM_PAGE_SIZE));
	vm_assert(!(length % VM_PAGE_SIZE));
	vm_assert(!write || (region->flags & VR_WRITABLE));

	FREE_RANGE_HERE(NULL, region->first);

	for(physr = region->first; physr; physr = physr->next) {
		int r;

		SANITYCHECK(SCL_DETAIL);

		if(write) {
		  vm_assert(physr->ph->refcount > 0);
		  if(physr->ph->refcount > 1) {
			SANITYCHECK(SCL_DETAIL);
			r = map_copy_ph_block(vmp, region, physr);
			if(r != OK) {
				printf("VM: map_handle_memory: no copy\n");
				return r;
			}
			changes++;
			SANITYCHECK(SCL_DETAIL);
		  } else {
			SANITYCHECK(SCL_DETAIL);
			if((r=map_ph_writept(vmp, region, physr->ph, NULL, NULL)) != OK) {
				printf("VM: map_ph_writept failed\n");
				return r;
			}
			changes++;
			SANITYCHECK(SCL_DETAIL);
		  }
		}

		SANITYCHECK(SCL_DETAIL);
		FREE_RANGE_HERE(physr, physr->next);
		SANITYCHECK(SCL_DETAIL);
	}

	SANITYCHECK(SCL_FUNCTIONS);

#if SANITYCHECKS
	if(changes == 0)  {
		vm_panic("no changes?!", changes);
	}
#endif

	return OK;
}

#if SANITYCHECKS
static int countregions(struct vir_region *vr)
{
	int n = 0;
	struct phys_region *ph;
	for(ph = vr->first; ph; ph = ph->next)
		n++;
	return n;
}
#endif

/*===========================================================================*
 *				map_copy_region			     	*
 *===========================================================================*/
static struct vir_region *map_copy_region(struct vir_region *vr)
{
	/* map_copy_region creates a complete copy of the vir_region
	 * data structure, linking in the same phys_blocks directly,
	 * but all in limbo, i.e., the caller has to link the vir_region
	 * to a process. Therefore it doesn't increase the refcount in
	 * the phys_block; the caller has to do this once it's linked.
	 * The reason for this is to keep the sanity checks working
	 * within this function.
	 */
	struct vir_region *newvr;
	struct phys_region *ph, *prevph = NULL;
#if SANITYCHECKS
	int cr;
	cr = countregions(vr);
#endif
	if(!SLABALLOC(newvr))
		return NULL;
	*newvr = *vr;
	newvr->first = NULL;
	newvr->next = NULL;

	SANITYCHECK(SCL_FUNCTIONS);

	for(ph = vr->first; ph; ph = ph->next) {
		struct phys_region *newph;
		if(!SLABALLOC(newph)) {
			map_free(newvr);
			return NULL;
		}
		newph->next = NULL;
		newph->ph = ph->ph;
		newph->next_ph_list = NULL;
		newph->parent = newvr;
		if(prevph) prevph->next = newph;
		else newvr->first = newph;
		prevph = newph;
		SANITYCHECK(SCL_DETAIL);
		vm_assert(countregions(vr) == cr);
	}

	vm_assert(countregions(vr) == countregions(newvr));

	SANITYCHECK(SCL_FUNCTIONS);

	return newvr;
}

/*=========================================================================*
 *				map_writept				*
 *=========================================================================*/
int map_writept(struct vmproc *vmp)
{
	struct vir_region *vr;
	struct phys_region *ph;
	int ropages = 0, rwpages = 0;

	for(vr = vmp->vm_regions; vr; vr = vr->next)
		for(ph = vr->first; ph; ph = ph->next) {
			map_ph_writept(vmp, vr, ph->ph, &ropages, &rwpages);
		}

	return OK;
}

/*========================================================================*
 *				map_proc_copy			     	  *
 *========================================================================*/
int map_proc_copy(dst, src)
struct vmproc *dst;
struct vmproc *src;
{
	struct vir_region *vr, *prevvr = NULL;
	dst->vm_regions = NULL;

	SANITYCHECK(SCL_FUNCTIONS);
	for(vr = src->vm_regions; vr; vr = vr->next) {
		struct vir_region *newvr;
		struct phys_region *orig_ph, *new_ph;
	SANITYCHECK(SCL_DETAIL);
		if(!(newvr = map_copy_region(vr))) {
			map_free_proc(dst);
	SANITYCHECK(SCL_FUNCTIONS);
			return ENOMEM;
		}
		SANITYCHECK(SCL_DETAIL);
		newvr->parent = dst;
		if(prevvr) { prevvr->next = newvr; }
		else { dst->vm_regions = newvr; }
		new_ph = newvr->first;
		for(orig_ph = vr->first; orig_ph; orig_ph = orig_ph->next) {
			struct phys_block *pb;
			/* Check two physregions both are nonnull,
			 * are different, and match physblocks.
			 */
			vm_assert(orig_ph && new_ph);
			vm_assert(orig_ph != new_ph);
			pb = orig_ph->ph;
			vm_assert(pb == new_ph->ph);

			/* Link in new physregion. */
			vm_assert(!new_ph->next_ph_list);
			new_ph->next_ph_list = pb->firstregion;
			pb->firstregion = new_ph;
			SLABSANE(new_ph);
			SLABSANE(new_ph->next_ph_list);

			/* Increase phys block refcount */
			vm_assert(pb->refcount > 0);
			pb->refcount++;
			vm_assert(pb->refcount > 1);

			/* Get next new physregion */
			new_ph = new_ph->next;
		}
		vm_assert(!new_ph);
		SANITYCHECK(SCL_DETAIL);
		prevvr = newvr;
	SANITYCHECK(SCL_DETAIL);
	}
	SANITYCHECK(SCL_DETAIL);

	map_writept(src);
	map_writept(dst);

	SANITYCHECK(SCL_FUNCTIONS);
	return OK;
}

/*========================================================================*
 *				map_proc_kernel		     	  	*
 *========================================================================*/
struct vir_region *map_proc_kernel(struct vmproc *vmp)
{
	struct vir_region *vr;

	/* We assume these are the first regions to be mapped to
	 * make the function a bit simpler (free all regions on error).
	 */
	vm_assert(!vmp->vm_regions);
	vm_assert(vmproc[VMP_SYSTEM].vm_flags & VMF_INUSE);
	vm_assert(!(KERNEL_TEXT % VM_PAGE_SIZE));
	vm_assert(!(KERNEL_TEXT_LEN % VM_PAGE_SIZE));
	vm_assert(!(KERNEL_DATA % VM_PAGE_SIZE));
	vm_assert(!(KERNEL_DATA_LEN % VM_PAGE_SIZE));

	if(!(vr = map_page_region(vmp, KERNEL_TEXT, 0, KERNEL_TEXT_LEN, 
		KERNEL_TEXT, VR_DIRECT | VR_WRITABLE | VR_NOPF, 0)) ||
	   !(vr = map_page_region(vmp, KERNEL_DATA, 0, KERNEL_DATA_LEN, 
		KERNEL_DATA, VR_DIRECT | VR_WRITABLE | VR_NOPF, 0))) {
		map_free_proc(vmp);
		return NULL;
	}

	return vr; /* Return pointer not useful, just non-NULL. */
}

/*========================================================================*
 *				map_region_extend	     	  	*
 *========================================================================*/
int map_region_extend(struct vmproc *vmp, struct vir_region *vr,
	vir_bytes delta)
{
	vir_bytes end;

	vm_assert(vr);
	vm_assert(vr->flags & VR_ANON);
	vm_assert(!(delta % VM_PAGE_SIZE));

	if(!delta) return OK;
	end = vr->vaddr + vr->length;
	vm_assert(end >= vr->vaddr);

	if(end + delta <= end) {
		printf("VM: strange delta 0x%lx\n", delta);
		return ENOMEM;
	}

	if(!vr->next || end + delta <= vr->next->vaddr) {
		vr->length += delta;
		return OK;
	}

	map_printmap(vmp);

	return ENOMEM;
}

/*========================================================================*
 *				map_region_shrink	     	  	*
 *========================================================================*/
int map_region_shrink(struct vir_region *vr, vir_bytes delta)
{
	vm_assert(vr);
	vm_assert(vr->flags & VR_ANON);
	vm_assert(!(delta % VM_PAGE_SIZE));

#if 0
	printf("VM: ignoring region shrink\n");
#endif

	return OK;
}

struct vir_region *map_region_lookup_tag(vmp, tag)
struct vmproc *vmp;
u32_t tag;
{
	struct vir_region *vr;

	for(vr = vmp->vm_regions; vr; vr = vr->next)
		if(vr->tag == tag)
			return vr;

	return NULL;
}

void map_region_set_tag(struct vir_region *vr, u32_t tag)
{
	vr->tag = tag;
}

u32_t map_region_get_tag(struct vir_region *vr)
{
	return vr->tag;
}

/*========================================================================*
 *				map_unmap_region	     	  	*
 *========================================================================*/
int map_unmap_region(struct vmproc *vmp, struct vir_region *region)
{
	struct vir_region *r, *nextr, *prev = NULL;

	SANITYCHECK(SCL_FUNCTIONS);

	for(r = vmp->vm_regions; r; r = r->next) {
		if(r == region)
			break;

		prev = r;
	}

	SANITYCHECK(SCL_DETAIL);

	if(r == NULL)
		vm_panic("map_unmap_region: region not found\n", NO_NUM);

	if(!prev)
		vmp->vm_regions = r->next;
	else
		prev->next = r->next;
	map_free(r);

	SANITYCHECK(SCL_DETAIL);

	if(pt_writemap(&vmp->vm_pt, r->vaddr,
	  MAP_NONE, r->length, 0, WMF_OVERWRITE) != OK) {
	    printf("VM: map_unmap_region: pt_writemap failed\n");
	    return ENOMEM;
	}

	SANITYCHECK(SCL_FUNCTIONS);

	return OK;
}
