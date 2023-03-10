/*
 * AMD K7 AGPGART routines.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/gfp.h>
#include <linux/page-flags.h>
#include <linux/mm.h>
#include "agp.h"

static int agp_try_unsupported __initdata = 0;

struct amd_page_map {
	unsigned long *real;
	unsigned long *remapped;
};

static struct _amd_irongate_private {
	volatile u8 *registers;
	struct amd_page_map **gatt_pages;
	int num_tables;
} amd_irongate_private;

static int amd_create_page_map(struct amd_page_map *page_map)
{
	int i;

	page_map->real = (unsigned long *) __get_free_page(GFP_KERNEL);
	if (page_map->real == NULL)
		return -ENOMEM;

	SetPageReserved(virt_to_page(page_map->real));
	global_cache_flush();
	page_map->remapped = ioremap_nocache(virt_to_phys(page_map->real), 
					    PAGE_SIZE);
	if (page_map->remapped == NULL) {
		ClearPageReserved(virt_to_page(page_map->real));
		free_page((unsigned long) page_map->real);
		page_map->real = NULL;
		return -ENOMEM;
	}
	global_cache_flush();

	for (i = 0; i < PAGE_SIZE / sizeof(unsigned long); i++)
		page_map->remapped[i] = agp_bridge->scratch_page;

	return 0;
}

static void amd_free_page_map(struct amd_page_map *page_map)
{
	iounmap(page_map->remapped);
	ClearPageReserved(virt_to_page(page_map->real));
	free_page((unsigned long) page_map->real);
}

static void amd_free_gatt_pages(void)
{
	int i;
	struct amd_page_map **tables;
	struct amd_page_map *entry;

	tables = amd_irongate_private.gatt_pages;
	for (i = 0; i < amd_irongate_private.num_tables; i++) {
		entry = tables[i];
		if (entry != NULL) {
			if (entry->real != NULL)
				amd_free_page_map(entry);
			kfree(entry);
		}
	}
	kfree(tables);
	amd_irongate_private.gatt_pages = NULL;
}

static int amd_create_gatt_pages(int nr_tables)
{
	struct amd_page_map **tables;
	struct amd_page_map *entry;
	int retval = 0;
	int i;

	tables = kmalloc((nr_tables + 1) * sizeof(struct amd_page_map *), 
			 GFP_KERNEL);
	if (tables == NULL)
		return -ENOMEM;

	memset (tables, 0, sizeof(struct amd_page_map *) * (nr_tables + 1));
	for (i = 0; i < nr_tables; i++) {
		entry = kmalloc(sizeof(struct amd_page_map), GFP_KERNEL);
		if (entry == NULL) {
			retval = -ENOMEM;
			break;
		}
		memset (entry, 0, sizeof(struct amd_page_map));
		tables[i] = entry;
		retval = amd_create_page_map(entry);
		if (retval != 0)
			break;
	}
	amd_irongate_private.num_tables = nr_tables;
	amd_irongate_private.gatt_pages = tables;

	if (retval != 0)
		amd_free_gatt_pages();

	return retval;
}

/* Since we don't need contigious memory we just try
 * to get the gatt table once
 */

#define GET_PAGE_DIR_OFF(addr) (addr >> 22)
#define GET_PAGE_DIR_IDX(addr) (GET_PAGE_DIR_OFF(addr) - \
	GET_PAGE_DIR_OFF(agp_bridge->gart_bus_addr))
#define GET_GATT_OFF(addr) ((addr & 0x003ff000) >> 12) 
#define GET_GATT(addr) (amd_irongate_private.gatt_pages[\
	GET_PAGE_DIR_IDX(addr)]->remapped)

static int amd_create_gatt_table(void)
{
	struct aper_size_info_lvl2 *value;
	struct amd_page_map page_dir;
	unsigned long addr;
	int retval;
	u32 temp;
	int i;

	value = A_SIZE_LVL2(agp_bridge->current_size);
	retval = amd_create_page_map(&page_dir);
	if (retval != 0)
		return retval;

	retval = amd_create_gatt_pages(value->num_entries / 1024);
	if (retval != 0) {
		amd_free_page_map(&page_dir);
		return retval;
	}

	agp_bridge->gatt_table_real = (u32 *)page_dir.real;
	agp_bridge->gatt_table = (u32 *)page_dir.remapped;
	agp_bridge->gatt_bus_addr = virt_to_phys(page_dir.real);

	/* Get the address for the gart region.
	 * This is a bus address even on the alpha, b/c its
	 * used to program the agp master not the cpu
	 */

	pci_read_config_dword(agp_bridge->dev, AMD_APBASE, &temp);
	addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	agp_bridge->gart_bus_addr = addr;

	/* Calculate the agp offset */
	for (i = 0; i < value->num_entries / 1024; i++, addr += 0x00400000) {
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] =
			virt_to_phys(amd_irongate_private.gatt_pages[i]->real);
		page_dir.remapped[GET_PAGE_DIR_OFF(addr)] |= 0x00000001;
	}

	return 0;
}

static int amd_free_gatt_table(void)
{
	struct amd_page_map page_dir;
   
	page_dir.real = (unsigned long *)agp_bridge->gatt_table_real;
	page_dir.remapped = (unsigned long *)agp_bridge->gatt_table;

	amd_free_gatt_pages();
	amd_free_page_map(&page_dir);
	return 0;
}

static int amd_irongate_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_lvl2 *values;

	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = (temp & 0x0000000e);
	values = A_SIZE_LVL2(agp_bridge->driver->aperture_sizes);
	for (i = 0; i < agp_bridge->driver->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);

			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static int amd_irongate_configure(void)
{
	struct aper_size_info_lvl2 *current_size;
	u32 temp;
	u16 enable_reg;

	current_size = A_SIZE_LVL2(agp_bridge->current_size);

	/* Get the memory mapped registers */
	pci_read_config_dword(agp_bridge->dev, AMD_MMBASE, &temp);
	temp = (temp & PCI_BASE_ADDRESS_MEM_MASK);
	amd_irongate_private.registers = (volatile u8 *) ioremap(temp, 4096);

	/* Write out the address of the gatt table */
	OUTREG32(amd_irongate_private.registers, AMD_ATTBASE,
		 agp_bridge->gatt_bus_addr);

	/* Write the Sync register */
	pci_write_config_byte(agp_bridge->dev, AMD_MODECNTL, 0x80);
   
   	/* Set indexing mode */
   	pci_write_config_byte(agp_bridge->dev, AMD_MODECNTL2, 0x00);

	/* Write the enable register */
	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg | 0x0004);
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write out the size register */
	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = (((temp & ~(0x0000000e)) | current_size->size_value)
		| 0x00000001);
	pci_write_config_dword(agp_bridge->dev, AMD_APSIZE, temp);

	/* Flush the tlb */
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);

	return 0;
}

static void amd_irongate_cleanup(void)
{
	struct aper_size_info_lvl2 *previous_size;
	u32 temp;
	u16 enable_reg;

	previous_size = A_SIZE_LVL2(agp_bridge->previous_size);

	enable_reg = INREG16(amd_irongate_private.registers, AMD_GARTENABLE);
	enable_reg = (enable_reg & ~(0x0004));
	OUTREG16(amd_irongate_private.registers, AMD_GARTENABLE, enable_reg);

	/* Write back the previous size and disable gart translation */
	pci_read_config_dword(agp_bridge->dev, AMD_APSIZE, &temp);
	temp = ((temp & ~(0x0000000f)) | previous_size->size_value);
	pci_write_config_dword(agp_bridge->dev, AMD_APSIZE, temp);
	iounmap((void *) amd_irongate_private.registers);
}

/*
 * This routine could be implemented by taking the addresses
 * written to the GATT, and flushing them individually.  However
 * currently it just flushes the whole table.  Which is probably
 * more efficent, since agp_memory blocks can be a large number of
 * entries.
 */

static void amd_irongate_tlbflush(struct agp_memory *temp)
{
	OUTREG32(amd_irongate_private.registers, AMD_TLBFLUSH, 0x00000001);
}

static int amd_insert_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i, j, num_entries;
	unsigned long *cur_gatt;
	unsigned long addr;

	num_entries = A_SIZE_LVL2(agp_bridge->current_size)->num_entries;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	j = pg_start;
	while (j < (pg_start + mem->page_count)) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		if (!PGE_EMPTY(agp_bridge, cur_gatt[GET_GATT_OFF(addr)]))
			return -EBUSY;
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = pg_start; i < mem->page_count; i++, j++) {
		addr = (j * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] =
			agp_generic_mask_memory(mem->memory[i], mem->type);
	}
	amd_irongate_tlbflush(mem);
	return 0;
}

static int amd_remove_memory(struct agp_memory *mem, off_t pg_start, int type)
{
	int i;
	unsigned long *cur_gatt;
	unsigned long addr;

	if (type != 0 || mem->type != 0)
		return -EINVAL;

	for (i = pg_start; i < (mem->page_count + pg_start); i++) {
		addr = (i * PAGE_SIZE) + agp_bridge->gart_bus_addr;
		cur_gatt = GET_GATT(addr);
		cur_gatt[GET_GATT_OFF(addr)] = 
			(unsigned long) agp_bridge->scratch_page;
	}

	amd_irongate_tlbflush(mem);
	return 0;
}

static struct aper_size_info_lvl2 amd_irongate_sizes[7] =
{
	{2048, 524288, 0x0000000c},
	{1024, 262144, 0x0000000a},
	{512, 131072, 0x00000008},
	{256, 65536, 0x00000006},
	{128, 32768, 0x00000004},
	{64, 16384, 0x00000002},
	{32, 8192, 0x00000000}
};

static struct gatt_mask amd_irongate_masks[] =
{
	{.mask = 1, .type = 0}
};

struct agp_bridge_driver amd_irongate_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= amd_irongate_sizes,
	.size_type		= LVL2_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= amd_irongate_configure,
	.fetch_size		= amd_irongate_fetch_size,
	.cleanup		= amd_irongate_cleanup,
	.tlb_flush		= amd_irongate_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.masks			= amd_irongate_masks,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= amd_create_gatt_table,
	.free_gatt_table	= amd_free_gatt_table,
	.insert_memory		= amd_insert_memory,
	.remove_memory		= amd_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
};

struct agp_device_ids amd_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_7006,
		.chipset_name	= "Irongate",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700E,
		.chipset_name	= "761",
	},
	{
		.device_id	= PCI_DEVICE_ID_AMD_FE_GATE_700C,
		.chipset_name	= "760MP",
	},
	{ }, /* dummy final entry, always present */
};

static int __init agp_amdk7_probe(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = amd_agp_device_ids;
	struct agp_bridge_data *bridge;
	u8 cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id) {
			printk (KERN_INFO PFX "Detected AMD %s chipset\n",
					devs[j].chipset_name);
			goto found;
		}
	}

	if (!agp_try_unsupported) {
		printk(KERN_ERR PFX
		    "Unsupported AMD chipset (device id: %04x),"
		    " you might want to try agp_try_unsupported=1.\n",
		    pdev->device);
		return -ENODEV;
	}

	printk(KERN_WARNING PFX "Trying generic AMD routines"
	       " for device id: %04x\n", pdev->device);

found:
	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;

	bridge->driver = &amd_irongate_driver;
	bridge->dev_private_data = &amd_irongate_private,
	bridge->dev = pdev;
	bridge->capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			bridge->capndx+PCI_AGP_STATUS,
			&bridge->mode);

	pci_set_drvdata(pdev, bridge);
	return agp_add_bridge(bridge);
}

static void __devexit agp_amdk7_remove(struct pci_dev *pdev)
{
	struct agp_bridge_data *bridge = pci_get_drvdata(pdev);

	agp_remove_bridge(bridge);
	agp_put_bridge(bridge);
}

static struct pci_device_id agp_amdk7_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AMD,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_amdk7_pci_table);

static struct pci_driver agp_amdk7_pci_driver = {
	.name		= "agpgart-amdk7",
	.id_table	= agp_amdk7_pci_table,
	.probe		= agp_amdk7_probe,
	.remove		= agp_amdk7_remove,
};

static int __init agp_amdk7_init(void)
{
	return pci_module_init(&agp_amdk7_pci_driver);
}

static void __exit agp_amdk7_cleanup(void)
{
	pci_unregister_driver(&agp_amdk7_pci_driver);
}

module_init(agp_amdk7_init);
module_exit(agp_amdk7_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_LICENSE("GPL and additional rights");
