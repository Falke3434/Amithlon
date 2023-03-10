#ifndef __ASM_MACH_IPI_H
#define __ASM_MACH_IPI_H

inline void send_IPI_mask_sequence(int mask, int vector);

static inline void send_IPI_mask(int mask, int vector)
{
	send_IPI_mask_sequence(mask, vector);
}

static inline void send_IPI_allbutself(int vector)
{
	unsigned long mask = cpu_online_map & ~(1 << smp_processor_id());

	if (mask)
		send_IPI_mask(mask, vector);
}

static inline void send_IPI_all(int vector)
{
	send_IPI_mask(cpu_online_map, vector);
}

#endif /* __ASM_MACH_IPI_H */
