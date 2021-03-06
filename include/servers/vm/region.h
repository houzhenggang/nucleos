/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __SERVERS_VM_REGION_H
#define __SERVERS_VM_REGION_H

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

struct phys_block {
#if SANITYCHECKS
	u32_t			seencount;
#endif
	vir_bytes		length;	/* no. of contiguous bytes */
	phys_bytes		phys;	/* physical memory */
	u8_t			refcount;	/* Refcount of these pages */

	/* first in list of phys_regions that reference this block */
	struct phys_region	*firstregion;	
};

typedef struct phys_region {
	struct phys_block	*ph;
	struct vir_region	*parent; /* parent vir_region. */
	vir_bytes		offset;	/* offset from start of vir region */
#if SANITYCHECKS
	int			written;	/* written to pagetable */
#endif

	/* list of phys_regions that reference the same phys_block */
	struct phys_region	*next_ph_list;	

	/* AVL fields */
	struct phys_region	*less, *greater;
	int			factor;
} phys_region_t;

#include <servers/vm/physravl.h>

struct vir_region {
	struct vir_region *next; /* next virtual region in this process */
	vir_bytes	vaddr;	/* virtual address, offset from pagetable */
	vir_bytes	length;	/* length in bytes */
	physr_avl	*phys;	/* avl tree of physical memory blocks */
	u16_t		flags;
	u32_t tag;		/* Opaque to mapping code. */
	struct vmproc *parent;	/* Process that owns this vir_region. */
};

/* Mapping flags: */
#define VR_WRITABLE	0x001	/* Process may write here. */
#define VR_NOPF		0x002	/* May not generate page faults. */
#define VR_PHYS64K	0x004	/* Physical memory must be 64k aligned. */
#define VR_LOWER16MB	0x008
#define VR_LOWER1MB	0x010

/* Mapping type: */
#define VR_ANON		0x100	/* Memory to be cleared and allocated */
#define VR_DIRECT	0x200	/* Mapped, but not managed by VM */
#define VR_SHARED	0x40

/* Tag values: */
#define VRT_NONE	0xBEEF0000
#define VRT_HEAP	0xBEEF0001
#define VRT_CODE	0xBEEF0002

/* map_page_region flags */
#define MF_PREALLOC	0x01
#define MF_CONTIG	0x02

#endif /* __SERVERS_VM_REGION_H */
