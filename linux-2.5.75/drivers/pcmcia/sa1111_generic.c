/*
 * linux/drivers/pcmcia/sa1111_generic.c
 *
 * We implement the generic parts of a SA1111 PCMCIA driver.  This
 * basically means we handle everything except controlling the
 * power.  Power is machine specific...
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <pcmcia/ss.h>

#include <asm/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/irq.h>

#include "sa1111_generic.h"

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_S0_CD_VALID,    "SA1111 PCMCIA card detect" },
	{ 0, IRQ_S0_BVD1_STSCHG, "SA1111 PCMCIA BVD1"        },
	{ 1, IRQ_S1_CD_VALID,    "SA1111 CF card detect"     },
	{ 1, IRQ_S1_BVD1_STSCHG, "SA1111 CF BVD1"            },
};

int sa1111_pcmcia_hw_init(struct sa1100_pcmcia_socket *skt)
{
	if (skt->irq == NO_IRQ)
		skt->irq = skt->nr ? IRQ_S1_READY_NINT : IRQ_S0_READY_NINT;

	return sa11xx_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

void sa1111_pcmcia_hw_shutdown(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

void sa1111_pcmcia_socket_state(struct sa1100_pcmcia_socket *skt, struct pcmcia_state *state)
{
	struct sa1111_dev *sadev = SA1111_DEV(skt->dev);
	unsigned long status = sa1111_readl(sadev->mapbase + SA1111_PCSR);

	switch (skt->nr) {
	case 0:
		state->detect = status & PCSR_S0_DETECT ? 0 : 1;
		state->ready  = status & PCSR_S0_READY  ? 1 : 0;
		state->bvd1   = status & PCSR_S0_BVD1   ? 1 : 0;
		state->bvd2   = status & PCSR_S0_BVD2   ? 1 : 0;
		state->wrprot = status & PCSR_S0_WP     ? 1 : 0;
		state->vs_3v  = status & PCSR_S0_VS1    ? 0 : 1;
		state->vs_Xv  = status & PCSR_S0_VS2    ? 0 : 1;
		break;

	case 1:
		state->detect = status & PCSR_S1_DETECT ? 0 : 1;
		state->ready  = status & PCSR_S1_READY  ? 1 : 0;
		state->bvd1   = status & PCSR_S1_BVD1   ? 1 : 0;
		state->bvd2   = status & PCSR_S1_BVD2   ? 1 : 0;
		state->wrprot = status & PCSR_S1_WP     ? 1 : 0;
		state->vs_3v  = status & PCSR_S1_VS1    ? 0 : 1;
		state->vs_Xv  = status & PCSR_S1_VS2    ? 0 : 1;
		break;
	}
}

int sa1111_pcmcia_configure_socket(struct sa1100_pcmcia_socket *skt, const socket_state_t *state)
{
	struct sa1111_dev *sadev = SA1111_DEV(skt->dev);
	unsigned int pccr_skt_mask, pccr_set_mask, val;
	unsigned long flags;

	switch (skt->nr) {
	case 0:
		pccr_skt_mask = PCCR_S0_RST|PCCR_S0_FLT|PCCR_S0_PWAITEN|PCCR_S0_PSE;
		break;

	case 1:
		pccr_skt_mask = PCCR_S1_RST|PCCR_S1_FLT|PCCR_S1_PWAITEN|PCCR_S1_PSE;
		break;

	default:
		return -1;
	}

	pccr_set_mask = 0;

	if (state->Vcc != 0)
		pccr_set_mask |= PCCR_S0_PWAITEN|PCCR_S1_PWAITEN;
	if (state->Vcc == 50)
		pccr_set_mask |= PCCR_S0_PSE|PCCR_S1_PSE;
	if (state->flags & SS_RESET)
		pccr_set_mask |= PCCR_S0_RST|PCCR_S1_RST;
	if (state->flags & SS_OUTPUT_ENA)
		pccr_set_mask |= PCCR_S0_FLT|PCCR_S1_FLT;

	local_irq_save(flags);
	val = sa1111_readl(sadev->mapbase + SA1111_PCCR);
	val &= ~pccr_skt_mask;
	val |= pccr_set_mask & pccr_skt_mask;
	sa1111_writel(val, sadev->mapbase + SA1111_PCCR);
	local_irq_restore(flags);

	return 0;
}

void sa1111_pcmcia_socket_init(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

void sa1111_pcmcia_socket_suspend(struct sa1100_pcmcia_socket *skt)
{
	sa11xx_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static int pcmcia_probe(struct device *dev)
{
	struct sa1111_dev *sadev = SA1111_DEV(dev);
	char *base;

	if (!request_mem_region(sadev->res.start, 512,
				SA1111_DRIVER_NAME(sadev)))
		return -EBUSY;

	base = sadev->mapbase;

	/*
	 * Initialise the suspend state.
	 */
	sa1111_writel(PCSSR_S0_SLEEP | PCSSR_S1_SLEEP, base + SA1111_PCSSR);
	sa1111_writel(PCCR_S0_FLT | PCCR_S1_FLT, base + SA1111_PCCR);

#ifdef CONFIG_SA1100_ADSBITSY
	pcmcia_adsbitsy_init(dev);
#endif
#ifdef CONFIG_SA1100_BADGE4
	pcmcia_badge4_init(dev);
#endif
#ifdef CONFIG_SA1100_GRAPHICSMASTER
	pcmcia_graphicsmaster_init(dev);
#endif
#ifdef CONFIG_SA1100_JORNADA720
	pcmcia_jornada720_init(dev);
#endif
#ifdef CONFIG_ASSABET_NEPONSET
	pcmcia_neponset_init(dev);
#endif
#ifdef CONFIG_SA1100_PFS168
	pcmcia_pfs_init(dev);
#endif
#ifdef CONFIG_SA1100_PT_SYSTEM3
	pcmcia_system3_init(dev);
#endif
#ifdef CONFIG_SA1100_XP860
	pcmcia_xp860_init(dev);
#endif
	return 0;
}

static int __devexit pcmcia_remove(struct device *dev)
{
	struct sa1111_dev *sadev = SA1111_DEV(dev);

	sa11xx_drv_pcmcia_remove(dev);
	release_mem_region(sadev->res.start, 512);
	return 0;
}

static struct sa1111_driver pcmcia_driver = {
	.drv = {
		.name		= "sa1111-pcmcia",
		.bus		= &sa1111_bus_type,
		.probe		= pcmcia_probe,
		.remove		= __devexit_p(pcmcia_remove),
		.suspend 	= pcmcia_socket_dev_suspend,
		.resume 	= pcmcia_socket_dev_resume,
	},
	.devid			= SA1111_DEVID_PCMCIA,
};

static int __init sa1111_drv_pcmcia_init(void)
{
	return driver_register(&pcmcia_driver.drv);
}

static void __exit sa1111_drv_pcmcia_exit(void)
{
	driver_unregister(&pcmcia_driver.drv);
}

module_init(sa1111_drv_pcmcia_init);
module_exit(sa1111_drv_pcmcia_exit);

MODULE_DESCRIPTION("SA1111 PCMCIA card socket driver");
MODULE_LICENSE("GPL");
