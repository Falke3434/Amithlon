#ifndef _ASM_GENAPIC_H
#define _ASM_GENAPIC_H 1

/* 
 * Generic APIC driver interface.
 *  
 * An straight forward mapping of the APIC related parts of the 
 * x86 subarchitecture interface to a dynamic object.
 *	
 * This is used by the "generic" x86 subarchitecture. 
 *
 * Copyright 2003 Andi Kleen, SuSE Labs.
 */

struct mpc_config_translation;
struct mpc_config_bus;
struct mp_config_table;
struct mpc_config_processor;

struct genapic { 
	char *name; 
	int (*probe)(void); 

	int (*apic_id_registered)(void);
	unsigned long (*target_cpus)(void); 
	int int_delivery_mode;
	int int_dest_mode; 
	int apic_broadcast_id; 
	int esr_disable;
	unsigned long (*check_apicid_used)(unsigned long bitmap, int apicid); 
	unsigned long (*check_apicid_present)(int apicid); 
	int no_balance_irq;
	void (*init_apic_ldr)(void);
	unsigned long (*ioapic_phys_id_map)(unsigned long map); 

	void (*clustered_apic_check)(void);
	int (*multi_timer_check)(int apic, int irq);
	int (*apicid_to_node)(int logical_apicid); 
	int (*cpu_to_logical_apicid)(int cpu);
	int (*cpu_present_to_apicid)(int mps_cpu);
	unsigned long (*apicid_to_cpu_present)(int phys_apicid); 
	int (*mpc_apic_id)(struct mpc_config_processor *m, 
			   struct mpc_config_translation *t); 
	void (*setup_portio_remap)(void); 
	int (*check_phys_apicid_present)(int boot_cpu_physical_apicid);
	void (*enable_apic_mode)(void);

	/* mpparse */
	void (*mpc_oem_bus_info)(struct mpc_config_bus *, char *, 
				 struct mpc_config_translation *);
	void (*mpc_oem_pci_bus)(struct mpc_config_bus *, 
				struct mpc_config_translation *); 

	/* When one of the next two hooks returns 1 the genapic
	   is switched to this. Essentially they are additional probe 
	   functions. */
	int (*mps_oem_check)(struct mp_config_table *mpc, char *oem, 
			      char *productid);
	int (*acpi_madt_oem_check)(char *oem_id, char *oem_table_id);

	unsigned (*get_apic_id)(unsigned long x);
	unsigned long apic_id_mask; 
	unsigned int (*cpu_mask_to_apicid)(unsigned long cpumask);
	
	/* ipi */
	void (*send_IPI_mask)(int mask, int vector);
	void (*send_IPI_allbutself)(int vector);
	void (*send_IPI_all)(int vector);
}; 

#define APICFUNC(x) .x = x

#define APIC_INIT(aname, aprobe) { \
	.name = aname, \
	.probe = aprobe, \
	.int_delivery_mode = INT_DELIVERY_MODE, \
	.int_dest_mode = INT_DEST_MODE, \
	.apic_broadcast_id = APIC_BROADCAST_ID, \
	.no_balance_irq = NO_BALANCE_IRQ, \
	APICFUNC(apic_id_registered), \
	APICFUNC(target_cpus), \
	APICFUNC(check_apicid_used), \
	APICFUNC(check_apicid_present), \
	APICFUNC(init_apic_ldr), \
	APICFUNC(ioapic_phys_id_map), \
	APICFUNC(clustered_apic_check), \
	APICFUNC(multi_timer_check), \
	APICFUNC(apicid_to_node), \
	APICFUNC(cpu_to_logical_apicid), \
	APICFUNC(cpu_present_to_apicid), \
	APICFUNC(apicid_to_cpu_present), \
	APICFUNC(mpc_apic_id), \
	APICFUNC(setup_portio_remap), \
	APICFUNC(check_phys_apicid_present), \
	APICFUNC(mpc_oem_bus_info), \
	APICFUNC(mpc_oem_pci_bus), \
	APICFUNC(mps_oem_check), \
	APICFUNC(get_apic_id), \
	.apic_id_mask = APIC_ID_MASK, \
	APICFUNC(cpu_mask_to_apicid), \
	APICFUNC(acpi_madt_oem_check), \
	APICFUNC(send_IPI_mask), \
	APICFUNC(send_IPI_allbutself), \
	APICFUNC(send_IPI_all), \
	APICFUNC(enable_apic_mode), \
	}

extern struct genapic *genapic;

#endif
