# Makefile for the AT disk driver (AT_WINI)
app-y := pci.elf32
pci.elf32-type := driver
pci.elf32-obj-y := main.o pci.o pci_table.o

LDFLAGS_pci.elf32 := -L$(lib-arch) -Llib \
		     -lsys -ltimers -lnucc \
		     -Tdrivers/driver_32.lds

$(src)/pci.elf32: drivers/driver_32.lds

# Create in minix3's aout format
pci_a_flags   := 0x00
pci_a_hdrlen  := 0x20
pci_a_cpu     := i386
pci_stackheap := 32k

e2a-y := pci.elf32,pci
