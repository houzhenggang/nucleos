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
pci.h

Created:	Jan 2000 by Philip Homburg <philip@cs.vu.nl>
*/

/* tempory functions: to be replaced later (see pci_intel.h) */
unsigned pci_inb(U16_t port);
unsigned pci_inw(U16_t port);
unsigned pci_inl(U16_t port);

void pci_outb(U16_t port, U8_t value);
void pci_outw(U16_t port, U16_t value);
void pci_outl(U16_t port, U32_t value);

struct pci_vendor
{
	u16_t vid;
	char *name;
};

struct pci_device
{
	u16_t vid;
	u16_t did;
	char *name;
};

struct pci_baseclass
{
	u8_t baseclass;
	char *name;
};

struct pci_subclass
{
	u8_t baseclass;
	u8_t subclass;
	u16_t infclass;
	char *name;
};

struct pci_intel_ctrl
{
	u16_t vid;
	u16_t did;
};

struct pci_isabridge
{
	u16_t vid;
	u16_t did;
	int checkclass;
	int type;
};

struct pci_pcibridge
{
	u16_t vid;
	u16_t did;
	int type;
};

#define PCI_IB_PIIX	1	/* Intel PIIX compatible ISA bridge */
#define PCI_IB_VIA	2	/* VIA compatible ISA bridge */
#define PCI_IB_AMD	3	/* AMD compatible ISA bridge */
#define PCI_IB_SIS	4	/* SIS compatible ISA bridge */

#define PCI_PPB_STD	1	/* Standard PCI-to-PCI bridge */
#define PCI_PPB_CB	2	/* Cardbus bridge */
/* Still needed? */
#define PCI_AGPB_VIA	3	/* VIA compatible AGP bridge */

extern struct pci_vendor pci_vendor_table[];
extern struct pci_device pci_device_table[];
extern struct pci_baseclass pci_baseclass_table[];
extern struct pci_subclass pci_subclass_table[];
#if 0
extern struct pci_intel_ctrl pci_intel_ctrl[];
#endif
extern struct pci_isabridge pci_isabridge[];
extern struct pci_pcibridge pci_pcibridge[];

/* Utility functions */
int pci_reserve2(int devind, endpoint_t proc);
void pci_release(endpoint_t proc);
int pci_first_dev_a(struct rs_pci *aclp, int *devindp, u16_t *vidp, u16_t *didp);
int pci_next_dev_a(struct rs_pci *aclp, int *devindp, u16_t *vidp, u16_t *didp);

int pci_attr_r8_s(int devind, int port, u8_t *vp);
int pci_attr_r32_s(int devind, int port, u32_t *vp);
int pci_slot_name_s(int devind, char **cpp);
int pci_ids_s(int devind, u16_t *vidp, u16_t *didp);
