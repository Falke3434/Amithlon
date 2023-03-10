/**
 * @file op_x86_model.h
 * interface to x86 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Graydon Hoare
 */

#ifndef OP_X86_MODEL_H
#define OP_X86_MODEL_H

/* Pentium IV needs all these */
#define MAX_MSR 63
 
struct op_saved_msr {
	unsigned int high;
	unsigned int low;
};

struct op_msr_group {
	unsigned int addrs[MAX_MSR];
	struct op_saved_msr saved[MAX_MSR];
};

struct op_msrs {
	struct op_msr_group counters;
	struct op_msr_group controls;
};

struct pt_regs;

/* The model vtable abstracts the differences between
 * various x86 CPU model's perfctr support.
 */
struct op_x86_model_spec {
	unsigned int const num_counters;
	unsigned int const num_controls;
	void (*fill_in_addresses)(struct op_msrs * const msrs);
	void (*setup_ctrs)(struct op_msrs const * const msrs);
	int (*check_ctrs)(unsigned int const cpu, 
		struct op_msrs const * const msrs,
		struct pt_regs * const regs);
	void (*start)(struct op_msrs const * const msrs);
	void (*stop)(struct op_msrs const * const msrs);
};

extern struct op_x86_model_spec const op_ppro_spec;
extern struct op_x86_model_spec const op_p4_spec;
extern struct op_x86_model_spec const op_p4_ht2_spec;
extern struct op_x86_model_spec const op_athlon_spec;

#endif /* OP_X86_MODEL_H */
