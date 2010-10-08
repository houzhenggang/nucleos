/*
 *  Copyright (C) 2010  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#define USER_SPACE 1
/*
pci.c

Configure devices on the PCI bus

Created:	Jan 2000 by Philip Homburg <philip@cs.vu.nl>
*/

#include <nucleos/drivers.h>
#include <nucleos/pci.h>
#include <assert.h>
#include <ibm/pci.h>
#include <asm/servers/vm/vm.h>
#include <nucleos/com.h>
#include <servers/rs/rs.h>
#include <nucleos/syslib.h>

#include "pci_amd.h"
#include "pci_intel.h"
#include "pci_sis.h"
#include "pci_via.h"

#define irq_mode_pci(irq) ((void)0)

#include <stdlib.h>
#include <stdio.h>
#include <nucleos/string.h>
#include <nucleos/sysutil.h>

#define PBT_INTEL_HOST	 1
#define PBT_PCIBRIDGE	 2
#define PBT_CARDBUS	 3

#define BAM_NR		6	/* Number of base-address registers */

int debug= 0;

static struct pcibus
{
	int pb_type;
	int pb_needinit;
	int pb_isabridge_dev;
	int pb_isabridge_type;

	int pb_devind;
	int pb_busnr;
	u8_t (*pb_rreg8)(int busind, int devind, int port);
	u16_t (*pb_rreg16)(int busind, int devind, int port);
	u32_t (*pb_rreg32)(int busind, int devind, int port);
	void (*pb_wreg8)(int busind, int devind, int port, u8 value);
	void (*pb_wreg16)(int busind, int devind, int port, u16 value);
	void (*pb_wreg32)(int busind, int devind, int port, u32_t value);
	u16_t (*pb_rsts)(int busind);
	void (*pb_wsts)(int busind, u16 value);
} pcibus[NR_PCIBUS];
static int nr_pcibus= 0;

static struct pcidev
{
	u8_t pd_busnr;
	u8_t pd_dev;
	u8_t pd_func;
	u8_t pd_baseclass;
	u8_t pd_subclass;
	u8_t pd_infclass;
	u16_t pd_vid;
	u16_t pd_did;
	u8_t pd_ilr;

	u8_t pd_inuse;
	endpoint_t pd_proc;

	struct bar
	{
		int pb_flags;
		int pb_nr;
		u32_t pb_base;
		u32_t pb_size;
	} pd_bar[BAM_NR];
	int pd_bar_nr;
} pcidev[NR_PCIDEV];

/* pb_flags */
#define PBF_IO		1	/* I/O else memory */
#define PBF_INCOMPLETE	2	/* not allocated */

static int nr_pcidev= 0;

static void pci_intel_init(void);
static void probe_bus(int busind);
static int is_duplicate(u8 busnr, u8 dev, u8 func);
static void record_irq(int devind);
static void record_bars(int devind);
static void record_bars_bridge(int devind);
static void record_bars_cardbus(int devind);
static void record_bar(int devind, int bar_nr);
static void complete_bridges(void);
static void complete_bars(void);
static void update_bridge4dev_io(int devind, u32_t io_base, u32_t io_size);
static int get_freebus(void);
static int do_isabridge(int busind);
static void do_pcibridge(int busind);
static int get_busind(int busnr);
static int do_piix(int devind);
static int do_amd_isabr(int devind);
static int do_sis_isabr(int devind);
static int do_via_isabr(int devind);
#if 0
static void report_vga(int devind);
#endif
static char *pci_vid_name(u16 vid);
static char *pci_baseclass_name(u8 baseclass);
static char *pci_subclass_name(u8 baseclass, u8 subclass, u8 infclass);
static void ntostr(unsigned n, char **str, char *end);

static u8_t pci_attr_r8_u(int devind, int port);
static u32_t pci_attr_r32_u(int devind, int port);

static u16_t pci_attr_rsts(int devind);
static void pci_attr_wsts(int devind, u16 value);
static u16_t pcibr_std_rsts(int busind);
static void pcibr_std_wsts(int busind, u16 value);
static u16_t pcibr_cb_rsts(int busind);
static void pcibr_cb_wsts(int busind, u16 value);
static u16_t pcibr_via_rsts(int busind);
static void pcibr_via_wsts(int busind, u16 value);
static u8_t pcii_rreg8(int busind, int devind, int port);
static u16_t pcii_rreg16(int busind, int devind, int port);
static u32_t pcii_rreg32(int busind, int devind, int port);
static void pcii_wreg8(int busind, int devind, int port, u8 value);
static void pcii_wreg16(int busind, int devind, int port, u16 value);
static void pcii_wreg32(int busind, int devind, int port, u32_t value);
static u16_t pcii_rsts(int busind);
static void pcii_wsts(int busind, u16 value);
static void print_capabilities(int devind);
static int visible(struct rs_pci *aclp, int devind);
static void print_hyper_cap(int devind, u8 capptr);

/*===========================================================================*
 *			helper functions for I/O			     *
 *===========================================================================*/
unsigned pci_inb(u16 port) {
	u32_t value;
	int s;
	if ((s=sys_inb(port, (unsigned long*)&value)) != 0)
		printk("PCI: warning, sys_inb failed: %d\n", s);
	return value;
}
unsigned pci_inw(u16 port) {
	u32_t value;
	int s;
	if ((s=sys_inw(port, (unsigned long*)&value)) != 0)
		printk("PCI: warning, sys_inw failed: %d\n", s);
	return value;
}
unsigned pci_inl(u16 port) {
	u32 value;
	int s;
	if ((s=sys_inl(port, (unsigned long*)&value)) != 0)
		printk("PCI: warning, sys_inl failed: %d\n", s);
	return value;
}
void pci_outb(u16 port, u8 value) {
	int s;
	if ((s=sys_outb(port, value)) != 0)
		printk("PCI: warning, sys_outb failed: %d\n", s);
}
void pci_outw(u16 port, u16 value) {
	int s;
	if ((s=sys_outw(port, value)) != 0)
		printk("PCI: warning, sys_outw failed: %d\n", s);
}
void pci_outl(u16 port, u32 value) {
	int s;
	if ((s=sys_outl(port, value)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
}

/*===========================================================================*
 *				pci_init				     *
 *===========================================================================*/
void pci_init()
{
	static int first_time= 1;

	long v;

	if (!first_time)
		return;

	v= 0;
	env_parse("pci_debug", "d", 0, &v, 0, 1);
	debug= v;

	/* We don't expect to interrupted */
	assert(first_time == 1);
	first_time= -1;

	/* Only Intel (compatible) PCI controllers are supported at the
	 * moment.
	 */
	pci_intel_init();

	first_time= 0;
}

/*===========================================================================*
 *				pci_find_dev				     *
 *===========================================================================*/
int pci_find_dev(bus, dev, func, devindp)
u8_t bus;
u8_t dev;
u8_t func;
int *devindp;
{
	int devind;

	for (devind= 0; devind < nr_pcidev; devind++)
	{
		if (pcidev[devind].pd_busnr == bus &&
			pcidev[devind].pd_dev == dev &&
			pcidev[devind].pd_func == func)
		{
			break;
		}
	}
	if (devind >= nr_pcidev)
		return 0;
#if 0
	if (pcidev[devind].pd_inuse)
		return 0;
#endif
	*devindp= devind;
	return 1;
}

/*===========================================================================*
 *				pci_first_dev_a				     *
 *===========================================================================*/
int pci_first_dev_a(aclp, devindp, vidp, didp)
struct rs_pci *aclp;
int *devindp;
u16_t *vidp;
u16_t *didp;
{
	int i, devind;

	for (devind= 0; devind < nr_pcidev; devind++)
	{
#if 0
		if (pcidev[devind].pd_inuse)
			continue;
#endif
		if (!visible(aclp, devind))
			continue;
			break;
	}
	if (devind >= nr_pcidev)
		return 0;
	*devindp= devind;
	*vidp= pcidev[devind].pd_vid;
	*didp= pcidev[devind].pd_did;
	return 1;
}

/*===========================================================================*
 *				pci_next_dev				     *
 *===========================================================================*/
int pci_next_dev_a(aclp, devindp, vidp, didp)
struct rs_pci *aclp;
int *devindp;
u16_t *vidp;
u16_t *didp;
{
	int devind;

	for (devind= *devindp+1; devind < nr_pcidev; devind++)
	{
#if 0
		if (pcidev[devind].pd_inuse)
			continue;
#endif
		if (!visible(aclp, devind))
			continue;
			break;
	}
	if (devind >= nr_pcidev)
		return 0;
	*devindp= devind;
	*vidp= pcidev[devind].pd_vid;
	*didp= pcidev[devind].pd_did;
	return 1;
}

/*===========================================================================*
 *				pci_reserve2				     *
 *===========================================================================*/
int pci_reserve2(devind, proc)
int devind;
endpoint_t proc;
{
	int i, r;
	int ilr;
	struct io_range ior;
	struct mem_range mr;

	if (devind < 0 || devind >= nr_pcidev)
	{
		printk("pci:pci_reserve2: bad devind: %d\n", devind);
		return -EINVAL;
	}
	if(pcidev[devind].pd_inuse)
		return -EBUSY;
	pcidev[devind].pd_inuse= 1;
	pcidev[devind].pd_proc= proc;

	for (i= 0; i<pcidev[devind].pd_bar_nr; i++)
	{
		if (pcidev[devind].pd_bar[i].pb_flags & PBF_INCOMPLETE)
		{
			printk("pci_reserve3: BAR %d is incomplete\n", i);
			continue;
		}
		if (pcidev[devind].pd_bar[i].pb_flags & PBF_IO)
		{
			ior.ior_base= pcidev[devind].pd_bar[i].pb_base;
			ior.ior_limit= ior.ior_base +
				pcidev[devind].pd_bar[i].pb_size-1;

			if(debug) {
			   printk(
		"pci_reserve3: for proc %d, adding I/O range [0x%x..0x%x]\n",
				proc, ior.ior_base, ior.ior_limit);
			}
			r= sys_privctl(proc, SYS_PRIV_ADD_IO, &ior);
			if (r != 0)
			{
				printk("sys_privctl failed for proc %d: %d\n",
					proc, r);
			}
		}
		else
		{
			mr.mr_base= pcidev[devind].pd_bar[i].pb_base;
			mr.mr_limit= mr.mr_base +
				pcidev[devind].pd_bar[i].pb_size-1;

			r= sys_privctl(proc, SYS_PRIV_ADD_MEM, &mr);
			if (r != 0)
			{
				printk("sys_privctl failed for proc %d: %d\n",
					proc, r);
			}
		}
	}
	ilr= pcidev[devind].pd_ilr;
	if (ilr != PCI_ILR_UNKNOWN)
	{
		if(debug) printk("pci_reserve3: adding IRQ %d\n", ilr);
		r= sys_privctl(proc, SYS_PRIV_ADD_IRQ, &ilr);
		if (r != 0)
		{
			printk("sys_privctl failed for proc %d: %d\n",
				proc, r);
		}
	}

	return 0;
}

/*===========================================================================*
 *				pci_release				     *
 *===========================================================================*/
void pci_release(proc)
endpoint_t proc;
{
	int i;

	for (i= 0; i<nr_pcidev; i++)
	{
		if (!pcidev[i].pd_inuse)
			continue;
		if (pcidev[i].pd_proc != proc)
			continue;
		pcidev[i].pd_inuse= 0;
	}
}

/*===========================================================================*
 *				pci_ids_s				     *
 *===========================================================================*/
int pci_ids_s(devind, vidp, didp)
int devind;
u16_t *vidp;
u16_t *didp;
{
	if (devind < 0 || devind >= nr_pcidev)
		return -EINVAL;

	*vidp= pcidev[devind].pd_vid;
	*didp= pcidev[devind].pd_did;
	return 0;
}

/*===========================================================================*
 *				pci_rescan_bus				     *
 *===========================================================================*/
void pci_rescan_bus(busnr)
u8_t busnr;
{
	int busind;

	busind= get_busind(busnr);
	probe_bus(busind);

	/* Allocate bus numbers for uninitialized bridges */
	complete_bridges();

	/* Allocate I/O and memory resources for uninitialized devices */
	complete_bars();
}

/*===========================================================================*
 *				pci_slot_name_s				     *
 *===========================================================================*/
int pci_slot_name_s(devind, cpp)
int devind;
char **cpp;
{
	static char label[]= "ddd.ddd.ddd";
	char *end;
	char *p;

	if (devind < 0 || devind >= nr_pcidev)
		return -EINVAL;

	p= label;
	end= label+sizeof(label);

	ntostr(pcidev[devind].pd_busnr, &p, end);
	*p++= '.';

	ntostr(pcidev[devind].pd_dev, &p, end);
	*p++= '.';

	ntostr(pcidev[devind].pd_func, &p, end);

	*cpp= label;
	return 0;
}

/*===========================================================================*
 *				pci_dev_name				     *
 *===========================================================================*/
char *pci_dev_name(vid, did)
u16_t vid;
u16_t did;
{
	int i;

	for (i= 0; pci_device_table[i].name; i++)
	{
		if (pci_device_table[i].vid == vid &&
			pci_device_table[i].did == did)
		{
			return pci_device_table[i].name;
		}
	}
	return NULL;
}

/*===========================================================================*
 *				pci_attr_r8_s				     *
 *===========================================================================*/
int pci_attr_r8_s(devind, port, vp)
int devind;
int port;
u8_t *vp;
{
	if (devind < 0 || devind >= nr_pcidev)
		return -EINVAL;
	if (port < 0 || port > 255)
		return -EINVAL;

	*vp= pci_attr_r8_u(devind, port);
	return 0;
}

/*===========================================================================*
 *				pci_attr_r8_u				     *
 *===========================================================================*/
static u8_t pci_attr_r8_u(devind, port)
int devind;
int port;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	return pcibus[busind].pb_rreg8(busind, devind, port);
}

/*===========================================================================*
 *				pci_attr_r16				     *
 *===========================================================================*/
u16_t pci_attr_r16(devind, port)
int devind;
int port;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	return pcibus[busind].pb_rreg16(busind, devind, port);
}

/*===========================================================================*
 *				pci_attr_r32_s				     *
 *===========================================================================*/
int pci_attr_r32_s(devind, port, vp)
int devind;
int port;
u32_t *vp;
{
	if (devind < 0 || devind >= nr_pcidev)
		return -EINVAL;
	if (port < 0 || port > 256-4)
		return -EINVAL;

	*vp= pci_attr_r32_u(devind, port);
	return 0;
}

/*===========================================================================*
 *				pci_attr_r32_u				     *
 *===========================================================================*/
static u32_t pci_attr_r32_u(devind, port)
int devind;
int port;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	return pcibus[busind].pb_rreg32(busind, devind, port);
}

/*===========================================================================*
 *				pci_attr_w8				     *
 *===========================================================================*/
void pci_attr_w8(devind, port, value)
int devind;
int port;
u8 value;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	pcibus[busind].pb_wreg8(busind, devind, port, value);
}

/*===========================================================================*
 *				pci_attr_w16				     *
 *===========================================================================*/
void pci_attr_w16(devind, port, value)
int devind;
int port;
u16_t value;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	pcibus[busind].pb_wreg16(busind, devind, port, value);
}

/*===========================================================================*
 *				pci_attr_w32				     *
 *===========================================================================*/
void pci_attr_w32(devind, port, value)
int devind;
int port;
u32_t value;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	pcibus[busind].pb_wreg32(busind, devind, port, value);
}

/*===========================================================================*
 *				pci_intel_init				     *
 *===========================================================================*/
static void pci_intel_init()
{
	/* Try to detect a know PCI controller. Read the Vendor ID and
	 * the Device ID for function 0 of device 0.
	 * Two times the value 0xffff suggests a system without a (compatible)
	 * PCI controller. 
	 */
	u32_t bus, dev, func;
	u16_t vid, did;
	int s, i, r, busind, busnr;
	char *dstr;

	bus= 0;
	dev= 0;
	func= 0;

	vid= PCII_RREG16_(bus, dev, func, PCI_VID);
	did= PCII_RREG16_(bus, dev, func, PCI_DID);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif

	if (vid == 0xffff && did == 0xffff)
		return;	/* Nothing here */

#if 0
	for (i= 0; pci_intel_ctrl[i].vid; i++)
	{
		if (pci_intel_ctrl[i].vid == vid &&
			pci_intel_ctrl[i].did == did)
		{
			break;
		}
	}

	if (!pci_intel_ctrl[i].vid)
	{
		printk("pci_intel_init (warning): unknown PCI-controller:\n"
			"\tvendor %04X (%s), device %04X\n",
			vid, pci_vid_name(vid), did);
	}
#endif

	if (nr_pcibus >= NR_PCIBUS)
		panic("PCI","too many PCI busses", nr_pcibus);
	busind= nr_pcibus;
	nr_pcibus++;
	pcibus[busind].pb_type= PBT_INTEL_HOST;
	pcibus[busind].pb_needinit= 0;
	pcibus[busind].pb_isabridge_dev= -1;
	pcibus[busind].pb_isabridge_type= 0;
	pcibus[busind].pb_devind= -1;
	pcibus[busind].pb_busnr= 0;
	pcibus[busind].pb_rreg8= pcii_rreg8;
	pcibus[busind].pb_rreg16= pcii_rreg16;
	pcibus[busind].pb_rreg32= pcii_rreg32;
	pcibus[busind].pb_wreg8= pcii_wreg8;
	pcibus[busind].pb_wreg16= pcii_wreg16;
	pcibus[busind].pb_wreg32= pcii_wreg32;
	pcibus[busind].pb_rsts= pcii_rsts;
	pcibus[busind].pb_wsts= pcii_wsts;

	dstr= pci_dev_name(vid, did);
	if (!dstr)
		dstr= "unknown device";
	if (debug)
	{
		printk("pci_intel_init: %s (%04X/%04X)\n",
			dstr, vid, did);
	}

	probe_bus(busind);

	r= do_isabridge(busind);
	if (r != 0)
	{
		busnr= pcibus[busind].pb_busnr;

		/* Disable all devices for this bus */
		for (i= 0; i<nr_pcidev; i++)
		{
			if (pcidev[i].pd_busnr != busnr)
				continue;
			pcidev[i].pd_inuse= 1;
		}
		return;
	}

	/* Look for PCI bridges */
	do_pcibridge(busind);

	/* Allocate bus numbers for uninitialized bridges */
	complete_bridges();

	/* Allocate I/O and memory resources for uninitialized devices */
	complete_bars();
}

/*===========================================================================*
 *				probe_bus				     *
 *===========================================================================*/
static void probe_bus(busind)
int busind;
{
	u32_t dev, func, t3;
	u16_t vid, did, sts;
	u8_t headt;
	u8_t baseclass, subclass, infclass;
	int devind, busnr;
	char *s, *dstr;

#if DEBUG
printk("probe_bus(%d)\n", busind);
#endif
	if (nr_pcidev >= NR_PCIDEV)
		panic("PCI","too many PCI devices", nr_pcidev);
	devind= nr_pcidev;

	busnr= pcibus[busind].pb_busnr;
	for (dev= 0; dev<32; dev++)
	{

		for (func= 0; func < 8; func++)
		{
			pcidev[devind].pd_busnr= busnr;
			pcidev[devind].pd_dev= dev;
			pcidev[devind].pd_func= func;

			pci_attr_wsts(devind, 
				PSR_SSE|PSR_RMAS|PSR_RTAS);
			vid= pci_attr_r16(devind, PCI_VID);
			did= pci_attr_r16(devind, PCI_DID);
			headt= pci_attr_r8_u(devind, PCI_HEADT);
			sts= pci_attr_rsts(devind);

#if 0
			printk("vid 0x%x, did 0x%x, headt 0x%x, sts 0x%x\n",
				vid, did, headt, sts);
#endif

			if (vid == NO_VID && did == NO_VID)
			{
				if (func == 0)
					break;	/* Nothing here */

				/* Scan all functions of a multifunction
				 * device.
				 */
				continue;
			}

			if (sts & (PSR_SSE|PSR_RMAS|PSR_RTAS))
			{
				printk(
					"PCI: ignoring bad value 0x%x in sts for QEMU\n",
					sts & (PSR_SSE|PSR_RMAS|PSR_RTAS));
			}

			dstr= pci_dev_name(vid, did);
			if (debug)
			{
				if (dstr)
				{
					printk("%d.%lu.%lu: %s (%04X/%04X)\n",
						busind, (unsigned long)dev,
						(unsigned long)func, dstr,
						vid, did);
				}
				else
				{
					printk(
		"%d.%lu.%lu: Unknown device, vendor %04X (%s), device %04X\n",
						busind, (unsigned long)dev,
						(unsigned long)func, vid,
						pci_vid_name(vid), did);
				}
				printk("Device index: %d\n", devind);
				printk("Subsystem: Vid 0x%x, did 0x%x\n",
					pci_attr_r16(devind, PCI_SUBVID),
					pci_attr_r16(devind, PCI_SUBDID));
			}

			baseclass= pci_attr_r8_u(devind, PCI_BCR);
			subclass= pci_attr_r8_u(devind, PCI_SCR);
			infclass= pci_attr_r8_u(devind, PCI_PIFR);
			s= pci_subclass_name(baseclass, subclass, infclass);
			if (!s)
				s= pci_baseclass_name(baseclass);
			{
				if (!s)
					s= "(unknown class)";
			}
			if (debug)
			{
				printk("\tclass %s (%X/%X/%X)\n", s,
					baseclass, subclass, infclass);
			}

			if (is_duplicate(busnr, dev, func))
			{
				printk("\tduplicate!\n");
				if (func == 0 && !(headt & PHT_MULTIFUNC))
					break;
				continue;
			}

			devind= nr_pcidev;
			nr_pcidev++;
			pcidev[devind].pd_baseclass= baseclass;
			pcidev[devind].pd_subclass= subclass;
			pcidev[devind].pd_infclass= infclass;
			pcidev[devind].pd_vid= vid;
			pcidev[devind].pd_did= did;
			pcidev[devind].pd_inuse= 0;
			pcidev[devind].pd_bar_nr= 0;
			record_irq(devind);
			switch(headt & PHT_MASK)
			{
			case PHT_NORMAL:
				record_bars(devind);
				break;
			case PHT_BRIDGE:
				record_bars_bridge(devind);
				break;
			case PHT_CARDBUS:
				record_bars_cardbus(devind);
				break;
			default:
				printk("\t%d.%d.%d: unknown header type %d\n",
					busind, dev, func,
					headt & PHT_MASK);
				break;
			}
			if (debug)
				print_capabilities(devind);

			t3= ((baseclass << 16) | (subclass << 8) | infclass);
#if 0
			if (t3 == PCI_T3_VGA || t3 == PCI_T3_VGA_OLD)
				report_vga(devind);
#endif

			if (nr_pcidev >= NR_PCIDEV)
			  panic("PCI","too many PCI devices", nr_pcidev);
			devind= nr_pcidev;

			if (func == 0 && !(headt & PHT_MULTIFUNC))
				break;
		}
	}
}

/*===========================================================================*
 *				is_duplicate				     *
 *===========================================================================*/
static int is_duplicate(busnr, dev, func)
u8_t busnr;
u8_t dev;
u8_t func;
{
	int i;

	for (i= 0; i<nr_pcidev; i++)
	{
		if (pcidev[i].pd_busnr == busnr &&
			pcidev[i].pd_dev == dev &&
			pcidev[i].pd_func == func)
		{
			return 1;
		}
	}
	return 0;
}

/*===========================================================================*
 *				record_irq				     *
 *===========================================================================*/
static void record_irq(devind)
int devind;
{
	int ilr, ipr, busnr, busind, cb_devind;

	ilr= pci_attr_r8_u(devind, PCI_ILR);
	ipr= pci_attr_r8_u(devind, PCI_IPR);
	if (ilr == 0)
	{
		static int first= 1;
		if (ipr && first && debug)
		{
			first= 0;
			printk("PCI: strange, BIOS assigned IRQ0\n");
		}
		ilr= PCI_ILR_UNKNOWN;
	}
	pcidev[devind].pd_ilr= ilr;
	if (ilr == PCI_ILR_UNKNOWN && !ipr)
	{
	}
	else if (ilr != PCI_ILR_UNKNOWN && ipr)
	{
		if (debug)
			printk("\tIRQ %d for INT%c\n", ilr, 'A' + ipr-1);
	}
	else if (ilr != PCI_ILR_UNKNOWN)
	{
		printk(
	"PCI: IRQ %d is assigned, but device %d.%d.%d does not need it\n",
			ilr, pcidev[devind].pd_busnr, pcidev[devind].pd_dev,
			pcidev[devind].pd_func);
	}
	else
	{
		/* Check for cardbus devices */
		busnr= pcidev[devind].pd_busnr;
		busind= get_busind(busnr);
		if (pcibus[busind].pb_type == PBT_CARDBUS)
		{
			cb_devind= pcibus[busind].pb_devind;
			ilr= pcidev[cb_devind].pd_ilr;
			if (ilr != PCI_ILR_UNKNOWN)
			{
				if (debug)
				{
					printk(
					"assigning IRQ %d to Cardbus device\n",
						ilr);
				}
				pci_attr_w8(devind, PCI_ILR, ilr);
				pcidev[devind].pd_ilr= ilr;
				return;
			}
		}
		if(debug) {
			printk(
		"PCI: device %d.%d.%d uses INT%c but is not assigned any IRQ\n",
			pcidev[devind].pd_busnr, pcidev[devind].pd_dev,
			pcidev[devind].pd_func, 'A' + ipr-1);
		}
	}
}

/*===========================================================================*
 *				record_bars				     *
 *===========================================================================*/
static void record_bars(devind)
int devind;
{
	int i, j, reg, prefetch, type, clear_01, clear_23, pb_nr;
	u32_t bar, bar2;

	for (i= 0, reg= PCI_BAR; reg <= PCI_BAR_6; i++, reg += 4)
	{
		record_bar(devind, i);
	}

	/* Special case code for IDE controllers in compatibility mode */
	if (pcidev[devind].pd_baseclass == PCI_BCR_MASS_STORAGE &&
		pcidev[devind].pd_subclass == PCI_MS_IDE)
	{
		/* IDE device */
		clear_01= 0;
		clear_23= 0;
		if (!(pcidev[devind].pd_infclass & PCI_IDE_PRI_NATIVE))
		{
			if (debug)
			{
				printk(
	"primary channel is not in native mode, clearing BARs 0 and 1\n");
			}
			clear_01= 1;
		}
		if (!(pcidev[devind].pd_infclass & PCI_IDE_SEC_NATIVE))
		{
			if (debug)
			{
				printk(
	"secondary channel is not in native mode, clearing BARs 2 and 3\n");
			}
			clear_23= 1;
		}

		j= 0;
		for (i= 0; i<pcidev[devind].pd_bar_nr; i++)
		{
			pb_nr= pcidev[devind].pd_bar[i].pb_nr;
			if ((pb_nr == 0 || pb_nr == 1) && clear_01)
			{
				if (debug) printk("skipping bar %d\n", pb_nr);
				continue;	/* Skip */
			}
			if ((pb_nr == 2 || pb_nr == 3) && clear_23)
			{
				if (debug) printk("skipping bar %d\n", pb_nr);
				continue;	/* Skip */
			}
			if (i == j)
			{
				j++;
				continue;	/* No need to copy */
			}
			pcidev[devind].pd_bar[j]=
				pcidev[devind].pd_bar[i];
			j++;
		}
		pcidev[devind].pd_bar_nr= j;
	}
}

/*===========================================================================*
 *				record_bars_bridge			     *
 *===========================================================================*/
static void record_bars_bridge(devind)
int devind;
{
	u32_t base, limit, size;

	record_bar(devind, 0);
	record_bar(devind, 1);

	base= ((pci_attr_r8_u(devind, PPB_IOBASE) & PPB_IOB_MASK) << 8) |
		(pci_attr_r16(devind, PPB_IOBASEU16) << 16);
	limit= 0xff |
		((pci_attr_r8_u(devind, PPB_IOLIMIT) & PPB_IOL_MASK) << 8) |
		((~PPB_IOL_MASK & 0xff) << 8) |
		(pci_attr_r16(devind, PPB_IOLIMITU16) << 16);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tI/O window: base 0x%x, limit 0x%x, size %d\n",
			base, limit, size);
	}

	base= ((pci_attr_r16(devind, PPB_MEMBASE) & PPB_MEMB_MASK) << 16);
	limit= 0xffff |
		((pci_attr_r16(devind, PPB_MEMLIMIT) & PPB_MEML_MASK) << 16) |
		((~PPB_MEML_MASK & 0xffff) << 16);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tMemory window: base 0x%x, limit 0x%x, size 0x%x\n",
			base, limit, size);
	}

	/* Ignore the upper 32 bits */
	base= ((pci_attr_r16(devind, PPB_PFMEMBASE) & PPB_PFMEMB_MASK) << 16);
	limit= 0xffff |
		((pci_attr_r16(devind, PPB_PFMEMLIMIT) &
			PPB_PFMEML_MASK) << 16) |
		((~PPB_PFMEML_MASK & 0xffff) << 16);
	size= limit-base + 1;
	if (debug)
	{
		printk(
	"\tPrefetchable memory window: base 0x%x, limit 0x%x, size 0x%x\n",
			base, limit, size);
	}
}

/*===========================================================================*
 *				record_bars_cardbus			     *
 *===========================================================================*/
static void record_bars_cardbus(devind)
int devind;
{
	u32_t base, limit, size;

	record_bar(devind, 0);

	base= pci_attr_r32_u(devind, CBB_MEMBASE_0);
	limit= pci_attr_r32_u(devind, CBB_MEMLIMIT_0) |
		(~CBB_MEML_MASK & 0xffffffff);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tMemory window 0: base 0x%x, limit 0x%x, size %d\n",
			base, limit, size);
	}

	base= pci_attr_r32_u(devind, CBB_MEMBASE_1);
	limit= pci_attr_r32_u(devind, CBB_MEMLIMIT_1) |
		(~CBB_MEML_MASK & 0xffffffff);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tMemory window 1: base 0x%x, limit 0x%x, size %d\n",
			base, limit, size);
	}

	base= pci_attr_r32_u(devind, CBB_IOBASE_0);
	limit= pci_attr_r32_u(devind, CBB_IOLIMIT_0) |
		(~CBB_IOL_MASK & 0xffffffff);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tI/O window 0: base 0x%x, limit 0x%x, size %d\n",
			base, limit, size);
	}

	base= pci_attr_r32_u(devind, CBB_IOBASE_1);
	limit= pci_attr_r32_u(devind, CBB_IOLIMIT_1) |
		(~CBB_IOL_MASK & 0xffffffff);
	size= limit-base + 1;
	if (debug)
	{
		printk("\tI/O window 1: base 0x%x, limit 0x%x, size %d\n",
			base, limit, size);
	}
}

/*===========================================================================*
 *				record_bar				     *
 *===========================================================================*/
static void record_bar(devind, bar_nr)
int devind;
int bar_nr;
{
	int reg, prefetch, type, dev_bar_nr;
	u32_t bar, bar2;
	u16_t cmd;

	reg= PCI_BAR+4*bar_nr;

	bar= pci_attr_r32_u(devind, reg);
	if (bar & PCI_BAR_IO)
	{
		/* Disable I/O access before probing for BAR's size */
		cmd = pci_attr_r16(devind, PCI_CR);
		pci_attr_w16(devind, PCI_CR, cmd & ~PCI_CR_IO_EN);

		/* Probe BAR's size */
		pci_attr_w32(devind, reg, 0xffffffff);
		bar2= pci_attr_r32_u(devind, reg);

		/* Restore original state */
		pci_attr_w32(devind, reg, bar);
		pci_attr_w16(devind, PCI_CR, cmd);

		bar &= ~(u32_t)3;	/* Clear non-address bits */
		bar2 &= ~(u32_t)3;
		bar2= (~bar2 & 0xffff)+1;
		if (debug)
		{
			printk("\tbar_%d: %d bytes at 0x%x I/O\n",
				bar_nr, bar2, bar);
		}

		dev_bar_nr= pcidev[devind].pd_bar_nr++;
		pcidev[devind].pd_bar[dev_bar_nr].pb_flags= PBF_IO;
		pcidev[devind].pd_bar[dev_bar_nr].pb_base= bar;
		pcidev[devind].pd_bar[dev_bar_nr].pb_size= bar2;
		pcidev[devind].pd_bar[dev_bar_nr].pb_nr= bar_nr;
		if (bar == 0)
		{
			pcidev[devind].pd_bar[dev_bar_nr].pb_flags |= 
				PBF_INCOMPLETE;
		}
	}
	else
	{
		/* Disable mem access before probing for BAR's size */
		cmd = pci_attr_r16(devind, PCI_CR);
		pci_attr_w16(devind, PCI_CR, cmd & ~PCI_CR_MEM_EN);

		/* Probe BAR's size */
		pci_attr_w32(devind, reg, 0xffffffff);
		bar2= pci_attr_r32_u(devind, reg);

		/* Restore original values */
		pci_attr_w32(devind, reg, bar);
		pci_attr_w16(devind, PCI_CR, cmd);

		if (bar2 == 0)
			return;	/* Reg. is not implemented */

		prefetch= !!(bar & PCI_BAR_PREFETCH);
		type= (bar & PCI_BAR_TYPE);
		bar &= ~(u32_t)0xf;	/* Clear non-address bits */
		bar2 &= ~(u32_t)0xf;
		bar2= (~bar2)+1;
		if (debug)
		{
			printk("\tbar_%d: 0x%x bytes at 0x%x%s memory\n",
				bar_nr, bar2, bar,
				prefetch ? " prefetchable" : "");
			if (type != 0)
				printk("type = 0x%x\n", type);
		}

		dev_bar_nr= pcidev[devind].pd_bar_nr++;
		pcidev[devind].pd_bar[dev_bar_nr].pb_flags= 0;
		pcidev[devind].pd_bar[dev_bar_nr].pb_base= bar;
		pcidev[devind].pd_bar[dev_bar_nr].pb_size= bar2;
		pcidev[devind].pd_bar[dev_bar_nr].pb_nr= bar_nr;
		if (bar == 0)
		{
			pcidev[devind].pd_bar[dev_bar_nr].pb_flags |= 
				PBF_INCOMPLETE;
		}
	}
}

/*===========================================================================*
 *				complete_bridges			     *
 *===========================================================================*/
static void complete_bridges()
{
	int i, freebus, devind, prim_busnr;

	for (i= 0; i<nr_pcibus; i++)
	{
		if (!pcibus[i].pb_needinit)
			continue;
		printk("should allocate bus number for bus %d\n", i);
		freebus= get_freebus();
		printk("got bus number %d\n", freebus);

		devind= pcibus[i].pb_devind;

		prim_busnr= pcidev[devind].pd_busnr;
		if (prim_busnr != 0)
		{
			printk(
	"complete_bridge: updating subordinate bus number not implemented\n");
		}

		pcibus[i].pb_needinit= 0;
		pcibus[i].pb_busnr= freebus;

		printk("devind = %d\n", devind);
		printk("prim_busnr= %d\n", prim_busnr);

		pci_attr_w8(devind, PPB_PRIMBN, prim_busnr);
		pci_attr_w8(devind, PPB_SECBN, freebus);
		pci_attr_w8(devind, PPB_SUBORDBN, freebus);

		printk("CR = 0x%x\n", pci_attr_r16(devind, PCI_CR));
		printk("SECBLT = 0x%x\n", pci_attr_r8_u(devind, PPB_SECBLT));
		printk("BRIDGECTRL = 0x%x\n",
			pci_attr_r16(devind, PPB_BRIDGECTRL));
	}
}

/*===========================================================================*
 *				complete_bars				     *
 *===========================================================================*/
static void complete_bars(void)
{
	int i, j, r, bar_nr, reg;
	u32_t memgap_low, memgap_high, iogap_low, iogap_high, io_high,
		base, size, v32, diff1, diff2;
	char *cp, *next;
	char memstr[256];

	r= env_get_param("memory", memstr, sizeof(memstr));
	if (r != 0)
		panic("pci", "env_get_param failed", r);
	
	/* Set memgap_low to just above physical memory */
	memgap_low= 0;
	cp= memstr;
	while (*cp != '\0')
	{
		base= strtoul(cp, &next, 16);
		if (!(*next) || next == cp || *next != ':')
			goto bad_mem_string;
		cp= next+1;
		size= strtoul(cp, &next, 16);
		if (next == cp || (*next != ',' && *next != '\0'))
		if (!*next)
			goto bad_mem_string;
		if (base+size > memgap_low)
			memgap_low= base+size;

		if (*next)
			cp= next+1;
		else
			break;
	}

	memgap_high= 0xfe000000;	/* Leave space for the CPU (APIC) */

	if (debug)
	{
		printk("complete_bars: initial gap: [0x%x .. 0x%x>\n",
			memgap_low, memgap_high);
	}

	/* Find the lowest memory base */
	for (i= 0; i<nr_pcidev; i++)
	{
		for (j= 0; j<pcidev[i].pd_bar_nr; j++)
		{
			if (pcidev[i].pd_bar[j].pb_flags & PBF_IO)
				continue;
			if (pcidev[i].pd_bar[j].pb_flags & PBF_INCOMPLETE)
				continue;
			base= pcidev[i].pd_bar[j].pb_base;
			size= pcidev[i].pd_bar[j].pb_size;

			if (base >= memgap_high)
				continue;	/* Not in the gap */
			if (base+size <= memgap_low)
				continue;	/* Not in the gap */

			/* Reduce the gap by the smallest amount */
			diff1= base+size-memgap_low;
			diff2= memgap_high-base;

			if (diff1 < diff2)
				memgap_low= base+size;
			else
				memgap_high= base;
		}
	}

	if (debug)
	{
		printk("complete_bars: intermediate gap: [0x%x .. 0x%x>\n",
			memgap_low, memgap_high);
	}

	/* Should check main memory size */
	if (memgap_high < memgap_low)
	{
		printk("PCI: bad memory gap: [0x%x .. 0x%x>\n",
			memgap_low, memgap_high);
		panic(NULL, NULL, NO_NUM);
	}

	iogap_high= 0x10000;
	iogap_low= 0x400;

	/* Find the free I/O space */
	for (i= 0; i<nr_pcidev; i++)
	{
		for (j= 0; j<pcidev[i].pd_bar_nr; j++)
		{
			if (!(pcidev[i].pd_bar[j].pb_flags & PBF_IO))
				continue;
			if (pcidev[i].pd_bar[j].pb_flags & PBF_INCOMPLETE)
				continue;
			base= pcidev[i].pd_bar[j].pb_base;
			size= pcidev[i].pd_bar[j].pb_size;
			if (base >= iogap_high)
				continue;
			if (base+size <= iogap_low)
				continue;
#if 0
			if (debug)
			{
				printk(
		"pci device %d (%04x/%04x), bar %d: base 0x%x, size 0x%x\n",
					i, pcidev[i].pd_vid, pcidev[i].pd_did,
					j, base, size);
			}
#endif
			if (base+size-iogap_low < iogap_high-base)
				iogap_low= base+size;
			else
				iogap_high= base;
		}
	}

	if (iogap_high < iogap_low)
	{
		if (debug)
		{
			printk("iogap_high too low, should panic\n");
		}
		else
			panic("pci", "iogap_high too low", iogap_high);
	}
	if (debug)
		printk("I/O range = [0x%x..0x%x>\n", iogap_low, iogap_high);

	for (i= 0; i<nr_pcidev; i++)
	{
		for (j= 0; j<pcidev[i].pd_bar_nr; j++)
		{
			if (pcidev[i].pd_bar[j].pb_flags & PBF_IO)
				continue;
			if (!(pcidev[i].pd_bar[j].pb_flags & PBF_INCOMPLETE))
				continue;
			size= pcidev[i].pd_bar[j].pb_size;
			if (size < I386_PAGE_SIZE)
				size= I386_PAGE_SIZE;
			base= memgap_high-size;
			base &= ~(u32_t)(size-1);
			if (base < memgap_low)
				panic("pci", "memory base too low", base);
			memgap_high= base;
			bar_nr= pcidev[i].pd_bar[j].pb_nr;
			reg= PCI_BAR + 4*bar_nr;
			v32= pci_attr_r32_u(i, reg);
			pci_attr_w32(i, reg, v32 | base);
			if (debug)
			{
				printk(
		"complete_bars: allocated 0x%x size %d to %d.%d.%d, bar_%d\n",
					base, size, pcidev[i].pd_busnr,
					pcidev[i].pd_dev, pcidev[i].pd_func,
					bar_nr);
			}
			pcidev[i].pd_bar[j].pb_base= base;
			pcidev[i].pd_bar[j].pb_flags &= ~PBF_INCOMPLETE;
		}

		io_high= iogap_high;
		for (j= 0; j<pcidev[i].pd_bar_nr; j++)
		{
			if (!(pcidev[i].pd_bar[j].pb_flags & PBF_IO))
				continue;
			if (!(pcidev[i].pd_bar[j].pb_flags & PBF_INCOMPLETE))
				continue;
			size= pcidev[i].pd_bar[j].pb_size;
			base= iogap_high-size;
			base &= ~(u32_t)(size-1);

			/* Assume that ISA compatibility is required. Only
			 * use the lowest 256 bytes out of every 1024 bytes.
			 */
			base &= 0xfcff;

			if (base < iogap_low)
				panic("pci", "I/O base too low", base);

			iogap_high= base;
			bar_nr= pcidev[i].pd_bar[j].pb_nr;
			reg= PCI_BAR + 4*bar_nr;
			v32= pci_attr_r32_u(i, reg);
			pci_attr_w32(i, reg, v32 | base);
			if (debug)
			{
				printk(
		"complete_bars: allocated 0x%x size %d to %d.%d.%d, bar_%d\n",
					base, size, pcidev[i].pd_busnr,
					pcidev[i].pd_dev, pcidev[i].pd_func,
					bar_nr);
			}
			pcidev[i].pd_bar[j].pb_base= base;
			pcidev[i].pd_bar[j].pb_flags &= ~PBF_INCOMPLETE;

		}
		if (iogap_high != io_high)
		{
			update_bridge4dev_io(i, iogap_high,
				io_high-iogap_high);
		}
	}

	for (i= 0; i<nr_pcidev; i++)
	{
		for (j= 0; j<pcidev[i].pd_bar_nr; j++)
		{
			if (!(pcidev[i].pd_bar[j].pb_flags & PBF_INCOMPLETE))
				continue;
			printk("should allocate resources for device %d\n", i);
		}
	}
	return;

bad_mem_string:
	printk("PCI: bad memory environment string '%s'\n", memstr);
	panic(NULL, NULL, NO_NUM);
}

/*===========================================================================*
 *				update_bridge4dev_io			     *
 *===========================================================================*/
static void update_bridge4dev_io(devind, io_base, io_size)
int devind;
u32_t io_base;
u32_t io_size;
{
	int busnr, busind, type, br_devind;
	u16_t v16;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	type= pcibus[busind].pb_type;
	if (type == PBT_INTEL_HOST)
		return;	/* Nothing to do for host controller */
	if (type == PBT_PCIBRIDGE)
	{
		printk(
		"update_bridge4dev_io: not implemented for PCI bridges\n");
		return;	
	}
	if (type != PBT_CARDBUS)
		panic("pci", "update_bridge4dev_io: strange bus type", type);

	if (debug)
	{
		printk("update_bridge4dev_io: adding 0x%x at 0x%x\n",
			io_size, io_base);
	}
	br_devind= pcibus[busind].pb_devind;
	pci_attr_w32(br_devind, CBB_IOLIMIT_0, io_base+io_size-1);
	pci_attr_w32(br_devind, CBB_IOBASE_0, io_base);

	/* Enable I/O access. Enable busmaster access as well. */
	v16= pci_attr_r16(devind, PCI_CR);
	pci_attr_w16(devind, PCI_CR, v16 | PCI_CR_IO_EN | PCI_CR_MAST_EN);
}

/*===========================================================================*
 *				get_freebus				     *
 *===========================================================================*/
static int get_freebus()
{
	int i, freebus;

	freebus= 1;
	for (i= 0; i<nr_pcibus; i++)
	{
		if (pcibus[i].pb_needinit)
			continue;
		if (pcibus[i].pb_type == PBT_INTEL_HOST)
			continue;
		if (pcibus[i].pb_busnr <= freebus)
			freebus= pcibus[i].pb_busnr+1;
		printk("get_freebus: should check suboridinate bus number\n");
	}
	return freebus;
}

/*===========================================================================*
 *				do_isabridge				     *
 *===========================================================================*/
static int do_isabridge(busind)
int busind;
{
	int i, j, r, type, busnr, unknown_bridge, bridge_dev;
	u16_t vid, did;
	u32_t t3;
	char *dstr;

	unknown_bridge= -1;
	bridge_dev= -1;
	j= 0;	/* lint */
	vid= did= 0;	/* lint */
	busnr= pcibus[busind].pb_busnr;
	for (i= 0; i< nr_pcidev; i++)
	{
		if (pcidev[i].pd_busnr != busnr)
			continue;
		t3= ((pcidev[i].pd_baseclass << 16) |
			(pcidev[i].pd_subclass << 8) | pcidev[i].pd_infclass);
		if (t3 == PCI_T3_ISA)
		{
			/* ISA bridge. Report if no supported bridge is
			 * found.
			 */
			unknown_bridge= i;
		}

		vid= pcidev[i].pd_vid;
		did= pcidev[i].pd_did;
		for (j= 0; pci_isabridge[j].vid != 0; j++)
		{
			if (pci_isabridge[j].vid != vid)
				continue;
			if (pci_isabridge[j].did != did)
				continue;
			if (pci_isabridge[j].checkclass &&
				unknown_bridge != i)
			{
				/* This part of multifunction device is
				 * not the bridge.
				 */
				continue;
			}
			break;
		}
		if (pci_isabridge[j].vid)
		{
			bridge_dev= i;
			break;
		}
	}

	if (bridge_dev != -1)
	{
		dstr= pci_dev_name(vid, did);
		if (!dstr)
			dstr= "unknown device";
		if (debug)
		{
			printk("found ISA bridge (%04X/%04X) %s\n",
				vid, did, dstr);
		}
		pcibus[busind].pb_isabridge_dev= bridge_dev;
		type= pci_isabridge[j].type;
		pcibus[busind].pb_isabridge_type= type;
		switch(type)
		{
		case PCI_IB_PIIX:
			r= do_piix(bridge_dev);
			break;
		case PCI_IB_VIA:
			r= do_via_isabr(bridge_dev);
			break;
		case PCI_IB_AMD:
			r= do_amd_isabr(bridge_dev);
			break;
		case PCI_IB_SIS:
			r= do_sis_isabr(bridge_dev);
			break;
		default:
			panic("PCI","unknown ISA bridge type", type);
		}
		return r;
	}

	if (unknown_bridge == -1)
	{
		if (debug)
		{
			printk("(warning) no ISA bridge found on bus %d\n",
				busind);
		}
		return 0;
	}
	if (debug)
	{
		printk(
		"(warning) unsupported ISA bridge %04X/%04X for bus %d\n",
			pcidev[unknown_bridge].pd_vid,
			pcidev[unknown_bridge].pd_did, busind);
	}
	return 0;
}

/*===========================================================================*
 *				do_pcibridge				     *
 *===========================================================================*/
static void do_pcibridge(busind)
int busind;
{
	int i, devind, busnr;
	int ind, type;
	u16_t vid, did;
	u8_t sbusn, baseclass, subclass, infclass, headt;
	u32_t t3;

	vid= did= 0;	/* lint */
	busnr= pcibus[busind].pb_busnr;
	for (devind= 0; devind< nr_pcidev; devind++)
	{
#if 0
		printk("do_pcibridge: trying %u.%u.%u\n",
			pcidev[devind].pd_busind, pcidev[devind].pd_dev, 
			pcidev[devind].pd_func);
#endif

		if (pcidev[devind].pd_busnr != busnr)
		{
#if 0
			printk("wrong bus\n");
#endif
			continue;
		}

		vid= pcidev[devind].pd_vid;
		did= pcidev[devind].pd_did;
		for (i= 0; pci_pcibridge[i].vid != 0; i++)
		{
			if (pci_pcibridge[i].vid != vid)
				continue;
			if (pci_pcibridge[i].did != did)
				continue;
			break;
		}
		type= pci_pcibridge[i].type;
		if (pci_pcibridge[i].vid == 0)
		{
			headt= pci_attr_r8_u(devind, PCI_HEADT);
			type= 0;
			if ((headt & PHT_MASK) == PHT_BRIDGE)
				type= PCI_PPB_STD;
			else if ((headt & PHT_MASK) == PHT_CARDBUS)
				type= PCI_PPB_CB;
			else
			{
#if 0
				printk("not a bridge\n");
#endif
				continue;	/* Not a bridge */
			}

			baseclass= pci_attr_r8_u(devind, PCI_BCR);
			subclass= pci_attr_r8_u(devind, PCI_SCR);
			infclass= pci_attr_r8_u(devind, PCI_PIFR);
			t3= ((baseclass << 16) | (subclass << 8) | infclass);
			if (type == PCI_PPB_STD &&
				t3 != PCI_T3_PCI2PCI &&
				t3 != PCI_T3_PCI2PCI_SUBTR)
			{
				printk(
"Unknown PCI class %02x:%02x:%02x for PCI-to-PCI bridge, device %04X/%04X\n",
					baseclass, subclass, infclass,
					vid, did);
				continue;
			 }
			if (type == PCI_PPB_CB &&
				t3 != PCI_T3_CARDBUS)
			{
				printk(
"Unknown PCI class %02x:%02x:%02x for Cardbus bridge, device %04X/%04X\n",
					baseclass, subclass, infclass,
					vid, did);
				continue;
			 }
		}

		if (debug)
		{
			printk("%u.%u.%u: PCI-to-PCI bridge: %04X/%04X\n",
				pcidev[devind].pd_busnr,
				pcidev[devind].pd_dev, 
				pcidev[devind].pd_func, vid, did);
		}

		/* Assume that the BIOS initialized the secondary bus
		 * number.
		 */
		sbusn= pci_attr_r8_u(devind, PPB_SECBN);

		if (nr_pcibus >= NR_PCIBUS)
			panic("PCI","too many PCI busses", nr_pcibus);
		ind= nr_pcibus;
		nr_pcibus++;
		pcibus[ind].pb_type= PBT_PCIBRIDGE;
		pcibus[ind].pb_needinit= 1;
		pcibus[ind].pb_isabridge_dev= -1;
		pcibus[ind].pb_isabridge_type= 0;
		pcibus[ind].pb_devind= devind;
		pcibus[ind].pb_busnr= sbusn;
		pcibus[ind].pb_rreg8= pcibus[busind].pb_rreg8;
		pcibus[ind].pb_rreg16= pcibus[busind].pb_rreg16;
		pcibus[ind].pb_rreg32= pcibus[busind].pb_rreg32;
		pcibus[ind].pb_wreg8= pcibus[busind].pb_wreg8;
		pcibus[ind].pb_wreg16= pcibus[busind].pb_wreg16;
		pcibus[ind].pb_wreg32= pcibus[busind].pb_wreg32;
		switch(type)
		{
		case PCI_PPB_STD:
			pcibus[ind].pb_rsts= pcibr_std_rsts;
			pcibus[ind].pb_wsts= pcibr_std_wsts;
			break;
		case PCI_PPB_CB:
			pcibus[ind].pb_type= PBT_CARDBUS;
			pcibus[ind].pb_rsts= pcibr_cb_rsts;
			pcibus[ind].pb_wsts= pcibr_cb_wsts;
			break;
		case PCI_AGPB_VIA:
			pcibus[ind].pb_rsts= pcibr_via_rsts;
			pcibus[ind].pb_wsts= pcibr_via_wsts;
			break;
		default:
		    panic("PCI","unknown PCI-PCI bridge type", type);
		}
		if (debug)
		{
			printk(
			"bus(table) = %d, bus(sec) = %d, bus(subord) = %d\n",
				ind, sbusn, pci_attr_r8_u(devind, PPB_SUBORDBN));
		}
		if (sbusn == 0)
		{
			printk("Secondary bus number not initialized\n");
			continue;
		}
		pcibus[ind].pb_needinit= 0;

		probe_bus(ind);

		/* Look for PCI bridges */
		do_pcibridge(ind);
	}
}

/*===========================================================================*
 *				get_busind					     *
 *===========================================================================*/
static int get_busind(busnr)
int busnr;
{
	int i;

	for (i= 0; i<nr_pcibus; i++)
	{
		if (pcibus[i].pb_busnr == busnr)
			return i;
	}
	panic("pci", "get_busind: can't find bus", busnr);
}

/*===========================================================================*
 *				do_piix					     *
 *===========================================================================*/
static int do_piix(devind)
int devind;
{
	int i, s, dev, func, irqrc, irq;
	u32_t elcr1, elcr2, elcr;

#if DEBUG
	printk("in piix\n");
#endif
	dev= pcidev[devind].pd_dev;
	func= pcidev[devind].pd_func;
#if USER_SPACE
	if ((s=sys_inb(PIIX_ELCR1, (unsigned long*)&elcr1)) != 0)
		printk("Warning, sys_inb failed: %d\n", s);
	if ((s=sys_inb(PIIX_ELCR2, (unsigned long*)&elcr2)) != 0)
		printk("Warning, sys_inb failed: %d\n", s);
#else
	elcr1= inb(PIIX_ELCR1);
	elcr2= inb(PIIX_ELCR2);
#endif
	elcr= elcr1 | (elcr2 << 8);
	for (i= 0; i<4; i++)
	{
		irqrc= pci_attr_r8_u(devind, PIIX_PIRQRCA+i);
		if (irqrc & PIIX_IRQ_DI)
		{
			if (debug)
				printk("INT%c: disabled\n", 'A'+i);
		}
		else
		{
			irq= irqrc & PIIX_IRQ_MASK;
			if (debug)
				printk("INT%c: %d\n", 'A'+i, irq);
			if (!(elcr & (1 << irq)))
			{
				if (debug)
				{
					printk(
				"(warning) IRQ %d is not level triggered\n", 
						irq);
				}
			}
			irq_mode_pci(irq);
		}
	}
	return 0;
}

/*===========================================================================*
 *				do_amd_isabr				     *
 *===========================================================================*/
static int do_amd_isabr(devind)
int devind;
{
	int i, busnr, dev, func, xdevind, irq, edge;
	u8_t levmask;
	u16_t pciirq;

	/* Find required function */
	func= AMD_ISABR_FUNC;
	busnr= pcidev[devind].pd_busnr;
	dev= pcidev[devind].pd_dev;

	/* Fake a device with the required function */
	if (nr_pcidev >= NR_PCIDEV)
		panic("PCI","too many PCI devices", nr_pcidev);
	xdevind= nr_pcidev;
	pcidev[xdevind].pd_busnr= busnr;
	pcidev[xdevind].pd_dev= dev;
	pcidev[xdevind].pd_func= func;
	pcidev[xdevind].pd_inuse= 1;
	nr_pcidev++;

	levmask= pci_attr_r8_u(xdevind, AMD_ISABR_PCIIRQ_LEV);
	pciirq= pci_attr_r16(xdevind, AMD_ISABR_PCIIRQ_ROUTE);
	for (i= 0; i<4; i++)
	{
		edge= (levmask >> i) & 1;
		irq= (pciirq >> (4*i)) & 0xf;
		if (!irq)
		{
			if (debug)
				printk("INT%c: disabled\n", 'A'+i);
		}
		else
		{
			if (debug)
				printk("INT%c: %d\n", 'A'+i, irq);
			if (edge && debug)
			{
				printk(
				"(warning) IRQ %d is not level triggered\n",
					irq);
			}
			irq_mode_pci(irq);
		}
	}
	nr_pcidev--;
	return 0;
}

/*===========================================================================*
 *				do_sis_isabr				     *
 *===========================================================================*/
static int do_sis_isabr(devind)
int devind;
{
	int i, dev, func, irq;

	dev= pcidev[devind].pd_dev;
	func= pcidev[devind].pd_func;
	irq= 0;	/* lint */
	for (i= 0; i<4; i++)
	{
		irq= pci_attr_r8_u(devind, SIS_ISABR_IRQ_A+i);
		if (irq & SIS_IRQ_DISABLED)
		{
			if (debug)
				printk("INT%c: disabled\n", 'A'+i);
		}
		else
		{
			irq &= SIS_IRQ_MASK;
			if (debug)
				printk("INT%c: %d\n", 'A'+i, irq);
			irq_mode_pci(irq);
		}
	}
	return 0;
}

/*===========================================================================*
 *				do_via_isabr				     *
 *===========================================================================*/
static int do_via_isabr(devind)
int devind;
{
	int i, dev, func, irq, edge;
	u8_t levmask;

	dev= pcidev[devind].pd_dev;
	func= pcidev[devind].pd_func;
	levmask= pci_attr_r8_u(devind, VIA_ISABR_EL);
	irq= 0;	/* lint */
	edge= 0; /* lint */
	for (i= 0; i<4; i++)
	{
		switch(i)
		{
		case 0:
			edge= (levmask & VIA_ISABR_EL_INTA);
			irq= pci_attr_r8_u(devind, VIA_ISABR_IRQ_R2) >> 4;
			break;
		case 1:
			edge= (levmask & VIA_ISABR_EL_INTB);
			irq= pci_attr_r8_u(devind, VIA_ISABR_IRQ_R2);
			break;
		case 2:
			edge= (levmask & VIA_ISABR_EL_INTC);
			irq= pci_attr_r8_u(devind, VIA_ISABR_IRQ_R3) >> 4;
			break;
		case 3:
			edge= (levmask & VIA_ISABR_EL_INTD);
			irq= pci_attr_r8_u(devind, VIA_ISABR_IRQ_R1) >> 4;
			break;
		default:
			assert(0);
		}
		irq &= 0xf;
		if (!irq)
		{
			if (debug)
				printk("INT%c: disabled\n", 'A'+i);
		}
		else
		{
			if (debug)
				printk("INT%c: %d\n", 'A'+i, irq);
			if (edge && debug)
			{
				printk(
				"(warning) IRQ %d is not level triggered\n",
					irq);
			}
			irq_mode_pci(irq);
		}
	}
	return 0;
}


#if 0
/*===========================================================================*
 *				report_vga				     *
 *===========================================================================*/
static void report_vga(devind)
int devind;
{
	/* Report the amount of video memory. This is needed by the X11R6
	 * postinstall script to chmem the X server. Hopefully this can be
	 * removed when we get virtual memory.
	 */
	size_t amount, size;
	int i;

	amount= 0;
	for (i= 0; i<pcidev[devind].pd_bar_nr; i++)
	{
		if (pcidev[devind].pd_bar[i].pb_flags & PBF_IO)
			continue;
		size= pcidev[devind].pd_bar[i].pb_size;
		if (size < amount)
			continue;
		amount= size;
	}
	if (size != 0)
	{
		printk("PCI: video memory for device at %d.%d.%d: %d bytes\n",
			pcidev[devind].pd_busnr,
			pcidev[devind].pd_dev,
			pcidev[devind].pd_func,
			amount);
	}
}
#endif


/*===========================================================================*
 *				pci_vid_name				     *
 *===========================================================================*/
static char *pci_vid_name(vid)
u16_t vid;
{
	int i;

	for (i= 0; pci_vendor_table[i].name; i++)
	{
		if (pci_vendor_table[i].vid == vid)
			return pci_vendor_table[i].name;
	}
	return "unknown";
}

/*===========================================================================*
 *				pci_baseclass_name			     *
 *===========================================================================*/
static char *pci_baseclass_name(baseclass)
u8_t baseclass;
{
	int i;

	for (i= 0; pci_baseclass_table[i].name; i++)
	{
		if (pci_baseclass_table[i].baseclass == baseclass)
			return pci_baseclass_table[i].name;
	}
	return NULL;
}

/*===========================================================================*
 *				pci_subclass_name			     *
 *===========================================================================*/
static char *pci_subclass_name(baseclass, subclass, infclass)
u8_t baseclass;
u8_t subclass;
u8_t infclass;
{
	int i;

	for (i= 0; pci_subclass_table[i].name; i++)
	{
		if (pci_subclass_table[i].baseclass != baseclass)
			continue;
		if (pci_subclass_table[i].subclass != subclass)
			continue;
		if (pci_subclass_table[i].infclass != infclass &&
			pci_subclass_table[i].infclass != (u16_t)-1)
		{
			continue;
		}
		return pci_subclass_table[i].name;
	}
	return NULL;
}

/*===========================================================================*
 *				ntostr					     *
 *===========================================================================*/
static void ntostr(n, str, end)
unsigned n;
char **str;
char *end;
{
	char tmpstr[20];
	int i;

	if (n == 0)
	{
		tmpstr[0]= '0';
		i= 1;
	}
	else
	{
		for (i= 0; n; i++)
		{
			tmpstr[i]= '0' + (n%10);
			n /= 10;
		}
	}
	for (; i>0; i--)
	{
		if (*str == end)
		{
			break;
		}
		**str= tmpstr[i-1];
		(*str)++;
	}
	if (*str == end)	
		end[-1]= '\0';
	else
		**str= '\0';
}

/*===========================================================================*
 *				pci_attr_rsts				     *
 *===========================================================================*/
static u16_t pci_attr_rsts(devind)
int devind;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	return pcibus[busind].pb_rsts(busind);
}
				

/*===========================================================================*
 *				pcibr_std_rsts				     *
 *===========================================================================*/
static u16_t pcibr_std_rsts(busind)
int busind;
{
	int devind;

	devind= pcibus[busind].pb_devind;
	return pci_attr_r16(devind, PPB_SSTS);
}

/*===========================================================================*
 *				pcibr_std_wsts				     *
 *===========================================================================*/
static void pcibr_std_wsts(busind, value)
int busind;
u16_t value;
{
	int devind;
	devind= pcibus[busind].pb_devind;

#if 0
	printk("pcibr_std_wsts(%d, 0x%X), devind= %d\n", 
		busind, value, devind);
#endif
	pci_attr_w16(devind, PPB_SSTS, value);
}

/*===========================================================================*
 *				pcibr_cb_rsts				     *
 *===========================================================================*/
static u16_t pcibr_cb_rsts(busind)
int busind;
{
	int devind;
	devind= pcibus[busind].pb_devind;

	return pci_attr_r16(devind, CBB_SSTS);
}

/*===========================================================================*
 *				pcibr_cb_wsts				     *
 *===========================================================================*/
static void pcibr_cb_wsts(busind, value)
int busind;
u16_t value;
{
	int devind;
	devind= pcibus[busind].pb_devind;

#if 0
	printk("pcibr_cb_wsts(%d, 0x%X), devind= %d\n", 
		busind, value, devind);
#endif
	pci_attr_w16(devind, CBB_SSTS, value);
}

/*===========================================================================*
 *				pcibr_via_rsts				     *
 *===========================================================================*/
static u16_t pcibr_via_rsts(busind)
int busind;
{
	int devind;
	devind= pcibus[busind].pb_devind;

	return 0;
}

/*===========================================================================*
 *				pcibr_via_wsts				     *
 *===========================================================================*/
static void pcibr_via_wsts(busind, value)
int busind;
u16_t value;
{
	int devind;
	devind= pcibus[busind].pb_devind;

#if 0
	printk("pcibr_via_wsts(%d, 0x%X), devind= %d (not implemented)\n", 
		busind, value, devind);
#endif
}

/*===========================================================================*
 *				pci_attr_wsts				     *
 *===========================================================================*/
static void pci_attr_wsts(devind, value)
int devind;
u16_t value;
{
	int busnr, busind;

	busnr= pcidev[devind].pd_busnr;
	busind= get_busind(busnr);
	pcibus[busind].pb_wsts(busind, value);
}
				

/*===========================================================================*
 *				pcii_rreg8				     *
 *===========================================================================*/
static u8_t pcii_rreg8(busind, devind, port)
int busind;
int devind;
int port;
{
	u8_t v;
	int s;

	v= PCII_RREG8_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
#if 0
	printk("pcii_rreg8(%d, %d, 0x%X): %d.%d.%d= 0x%X\n",
		busind, devind, port,
		pcibus[busind].pb_bus, pcidev[devind].pd_dev,
		pcidev[devind].pd_func, v);
#endif
	return v;
}

/*===========================================================================*
 *				pcii_rreg16				     *
 *===========================================================================*/
static u16_t pcii_rreg16(busind, devind, port)
int busind;
int devind;
int port;
{
	u16_t v;
	int s;

	v= PCII_RREG16_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n");
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
#if 0
	printk("pcii_rreg16(%d, %d, 0x%X): %d.%d.%d= 0x%X\n",
		busind, devind, port,
		pcibus[busind].pb_bus, pcidev[devind].pd_dev,
		pcidev[devind].pd_func, v);
#endif
	return v;
}

/*===========================================================================*
 *				pcii_rreg32				     *
 *===========================================================================*/
static u32_t pcii_rreg32(busind, devind, port)
int busind;
int devind;
int port;
{
	u32_t v;
	int s;

	v= PCII_RREG32_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
#if 0
	printk("pcii_rreg32(%d, %d, 0x%X): %d.%d.%d= 0x%X\n",
		busind, devind, port,
		pcibus[busind].pb_bus, pcidev[devind].pd_dev,
		pcidev[devind].pd_func, v);
#endif
	return v;
}

/*===========================================================================*
 *				pcii_wreg8				     *
 *===========================================================================*/
static void pcii_wreg8(busind, devind, port, value)
int busind;
int devind;
int port;
u8_t value;
{
	int s;
#if 0
	printk("pcii_wreg8(%d, %d, 0x%X, 0x%X): %d.%d.%d\n",
		busind, devind, port, value,
		pcibus[busind].pb_bus, pcidev[devind].pd_dev,
		pcidev[devind].pd_func);
#endif
	PCII_WREG8_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port, value);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
}

/*===========================================================================*
 *				pcii_wreg16				     *
 *===========================================================================*/
static void pcii_wreg16(busind, devind, port, value)
int busind;
int devind;
int port;
u16_t value;
{
	int s;
#if 0
	printk("pcii_wreg16(%d, %d, 0x%X, 0x%X): %d.%d.%d\n",
		busind, devind, port, value,
		pcibus[busind].pb_bus, pcidev[devind].pd_dev,
		pcidev[devind].pd_func);
#endif
	PCII_WREG16_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port, value);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
}

/*===========================================================================*
 *				pcii_wreg32				     *
 *===========================================================================*/
static void pcii_wreg32(busind, devind, port, value)
int busind;
int devind;
int port;
u32_t value;
{
	int s;
#if 0
	printk("pcii_wreg32(%d, %d, 0x%X, 0x%X): %d.%d.%d\n",
		busind, devind, port, value,
		pcibus[busind].pb_busnr, pcidev[devind].pd_dev,
		pcidev[devind].pd_func);
#endif
	PCII_WREG32_(pcibus[busind].pb_busnr, 
		pcidev[devind].pd_dev, pcidev[devind].pd_func,
		port, value);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n");
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
}

/*===========================================================================*
 *				pcii_rsts				     *
 *===========================================================================*/
static u16_t pcii_rsts(busind)
int busind;
{
	u16_t v;
	int s;

	v= PCII_RREG16_(pcibus[busind].pb_busnr, 0, 0, PCI_SR);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
	return v;
}

/*===========================================================================*
 *				pcii_wsts				     *
 *===========================================================================*/
static void pcii_wsts(busind, value)
int busind;
u16_t value;
{
	int s;
	PCII_WREG16_(pcibus[busind].pb_busnr, 0, 0, PCI_SR, value);
#if USER_SPACE
	if ((s=sys_outl(PCII_CONFADD, PCII_UNSEL)) != 0)
		printk("PCI: warning, sys_outl failed: %d\n", s);
#else
	outl(PCII_CONFADD, PCII_UNSEL);
#endif
}


/*===========================================================================*
 *				print_capabilities			     *
 *===========================================================================*/
static void print_capabilities(devind)
int devind;
{
	u8_t status, capptr, type, next, subtype;
	char *str;

	/* Check capabilities bit in the device status register */
	status= pci_attr_r16(devind, PCI_SR);
	if (!(status & PSR_CAPPTR))
		return;

	capptr= (pci_attr_r8_u(devind, PCI_CAPPTR) & PCI_CP_MASK);
	while (capptr != 0)
	{
		type = pci_attr_r8_u(devind, capptr+CAP_TYPE);
		next= (pci_attr_r8_u(devind, capptr+CAP_NEXT) & PCI_CP_MASK);
		switch(type)
		{
		case 1: str= "PCI Power Management"; break;
		case 2: str= "AGP"; break;
		case 3: str= "Vital Product Data"; break;
		case 4:	str= "Slot Identification"; break;
		case 5: str= "Message Signaled Interrupts"; break;
		case 6: str= "CompactPCI Hot Swap"; break;
		case 8: str= "AMD HyperTransport"; break;
		case 0xf: str= "Secure Device"; break;
		default: str= "(unknown type)"; break;
		}

		printk(" @0x%x (0x%08x): capability type 0x%x: %s",
			capptr, pci_attr_r32_u(devind, capptr), type, str);
		if (type == 0x08)
			print_hyper_cap(devind, capptr);
		else if (type == 0x0f)
		{
			subtype= (pci_attr_r8_u(devind, capptr+2) & 0x07);
			switch(subtype)
			{
			case 0: str= "Device Exclusion Vector"; break;
			case 3: str= "IOMMU"; break;
			default: str= "(unknown type)"; break;
			}
			printk(", sub type 0%o: %s", subtype, str);
		}
		printk("\n");
		capptr= next;
	}
}


/*===========================================================================*
 *				visible					     *
 *===========================================================================*/
static int visible(aclp, devind)
struct rs_pci *aclp;
int devind;
{
	int i;
	u32_t class_id;

	if (!aclp)
		return TRUE;	/* Should be changed when ACLs become
				 * mandatory.
				 */
	/* Check whether the caller is allowed to get this device. */
	for (i= 0; i<aclp->rsp_nr_device; i++)
	{
		if (aclp->rsp_device[i].vid == pcidev[devind].pd_vid &&
			aclp->rsp_device[i].did == pcidev[devind].pd_did)
		{
			return TRUE;
		}
	}
	if (!aclp->rsp_nr_class)
		return FALSE;

	class_id= (pcidev[devind].pd_baseclass << 16) |
		(pcidev[devind].pd_subclass << 8) |
		pcidev[devind].pd_infclass;
	for (i= 0; i<aclp->rsp_nr_class; i++)
	{
		if (aclp->rsp_class[i].class ==
			(class_id & aclp->rsp_class[i].mask))
		{
			return TRUE;
		}
	}

	return FALSE;
}

/*===========================================================================*
 *				print_hyper_cap				     *
 *===========================================================================*/
static void print_hyper_cap(devind, capptr)
int devind;
u8_t capptr;
{ 
	u32_t v;
	u16_t cmd;
	int type0, type1;

	printk("\n");
	v= pci_attr_r32_u(devind, capptr);
	printk("print_hyper_cap: @0x%x, off 0 (cap):", capptr);
	cmd= (v >> 16) & 0xffff;
#if 0
	if (v & 0x10000)
	{
		printk(" WarmReset");
		v &= ~0x10000;
	}
	if (v & 0x20000)
	{
		printk(" DblEnded");
		v &= ~0x20000;
	}
	printk(" DevNum %d", (v & 0x7C0000) >> 18);
	v &= ~0x7C0000;
#endif
	type0= (cmd & 0xE000) >> 13;
	type1= (cmd & 0xF800) >> 11;
	if (type0 == 0 || type0 == 1)
	{
		printk("Capability Type: %s\n",
			type0 == 0 ? "Slave or Primary Interface" :
			"Host or Secondary Interface");
		cmd &= ~0xE000;
	}
	else
	{
		printk(" Capability Type 0x%x", type1);
		cmd &= ~0xF800;
	}
	if (cmd)
		printk(" undecoded 0x%x\n", cmd);

#if 0
	printk("print_hyper_cap: off 4 (ctl): 0x%x\n", 
		pci_attr_r32_u(devind, capptr+4));
	printk("print_hyper_cap: off 8 (freq/rev): 0x%x\n", 
		pci_attr_r32_u(devind, capptr+8));
	printk("print_hyper_cap: off 12 (cap): 0x%x\n", 
		pci_attr_r32_u(devind, capptr+12));
	printk("print_hyper_cap: off 16 (buf count): 0x%x\n", 
		pci_attr_r32_u(devind, capptr+16));
	v= pci_attr_r32_u(devind, capptr+20);
	printk("print_hyper_cap: @0x%x, off 20 (bus nr): ", 
		capptr+20);
	printk("prim %d", v & 0xff);
	printk(", sec %d", (v >> 8) & 0xff);
	printk(", sub %d", (v >> 16) & 0xff);
	if (v >> 24)
		printk(", reserved %d", (v >> 24) & 0xff);
	printk("\n");
	printk("print_hyper_cap: off 24 (type): 0x%x\n", 
		pci_attr_r32_u(devind, capptr+24));
#endif
}
