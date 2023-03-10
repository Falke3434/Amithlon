
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/platform.h>

#include <asm/io.h>
#include "piix4.h"

void (*pm_power_off)(void);

int reboot_thru_bios;
int reboot_smp;

void machine_restart(char * __unused)
{
#ifdef CONFIG_SMP
	smp_send_stop();
#endif

	/*
	 * Visual Workstations restart after this
	 * register is poked on the PIIX4
	 */
	outb(PIIX4_RESET_VAL, PIIX4_RESET_PORT);
}

void machine_power_off(void)
{
	unsigned short pm_status;
	extern unsigned int pci_bus0;

	while ((pm_status = inw(PMSTS_PORT)) & 0x100)
		outw(pm_status, PMSTS_PORT);

	outw(PM_SUSPEND_ENABLE, PMCNTRL_PORT);

	mdelay(10);

#define PCI_CONF1_ADDRESS(bus, devfn, reg) \
	(0x80000000 | (bus << 16) | (devfn << 8) | (reg & ~3))

	outl(PCI_CONF1_ADDRESS(pci_bus0, SPECIAL_DEV, SPECIAL_REG), 0xCF8);
	outl(PIIX_SPECIAL_STOP, 0xCFC);
}

void machine_halt(void)
{
}
