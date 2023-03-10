/*  Kernel module help for parisc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    (c) 2003 Randolph Chung <tausq@debian.org>

    The best reference for this stuff is probably the Processor-
    Specific ELF Supplement for PA-RISC:
	http://ftp.parisc-linux.org/docs/elf-pa-hp.pdf
*/
#include <linux/moduleloader.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/kernel.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(fmt...)
#endif

#define CHECK_RELOC(val, bits) \
	if ( ( !((val) & (1<<((bits)-1))) && ((val)>>(bits)) != 0 )  ||	\
	     ( ((val) & (1<<((bits)-1))) && ((val)>>(bits)) != (((__typeof__(val))(~0))>>((bits)+2)))) { \
		printk(KERN_ERR "module %s relocation of symbol %s is out of range (0x%lx in %d bits)\n", \
		me->name, strtab + sym->st_name, (unsigned long)val, bits); \
		return -ENOEXEC;			\
	}

/* three functions to determine where in the module core
 * or init pieces the location is */
static inline int is_init(struct module *me, void *loc)
{
	return (loc >= me->module_init &&
		loc <= (me->module_init + me->init_size));
}

static inline int is_core(struct module *me, void *loc)
{
	return (loc >= me->module_core &&
		loc <= (me->module_core + me->core_size));
}

static inline int is_local(struct module *me, void *loc)
{
	return is_init(me, loc) || is_core(me, loc);
}


#ifndef __LP64__
struct got_entry {
	Elf32_Addr addr;
};

struct fdesc_entry {
	Elf32_Addr addr;
	Elf32_Addr gp;
};

struct stub_entry {
	Elf32_Word insns[2]; /* each stub entry has two insns */
};
#else
struct got_entry {
	Elf64_Addr addr;
};

struct fdesc_entry {
	Elf64_Addr dummy[2];
	Elf64_Addr addr;
	Elf64_Addr gp;
};

struct stub_entry {
	Elf64_Word insns[4]; /* each stub entry has four insns */
};
#endif

/* Field selection types defined by hppa */
#define rnd(x)			(((x)+0x1000)&~0x1fff)
/* fsel: full 32 bits */
#define fsel(v,a)		((v)+(a))
/* lsel: select left 21 bits */
#define lsel(v,a)		(((v)+(a))>>11)
/* rsel: select right 11 bits */
#define rsel(v,a)		(((v)+(a))&0x7ff)
/* lrsel with rounding of addend to nearest 8k */
#define lrsel(v,a)		(((v)+rnd(a))>>11)
/* rrsel with rounding of addend to nearest 8k */
#define rrsel(v,a)		((((v)+rnd(a))&0x7ff)+((a)-rnd(a)))

#define mask(x,sz)		((x) & ~((1<<(sz))-1))


/* The reassemble_* functions prepare an immediate value for
   insertion into an opcode. pa-risc uses all sorts of weird bitfields
   in the instruction to hold the value.  */
static inline int reassemble_14(int as14)
{
	return (((as14 & 0x1fff) << 1) |
		((as14 & 0x2000) >> 13));
}

static inline int reassemble_17(int as17)
{
	return (((as17 & 0x10000) >> 16) |
		((as17 & 0x0f800) << 5) |
		((as17 & 0x00400) >> 8) |
		((as17 & 0x003ff) << 3));
}

static inline int reassemble_21(int as21)
{
	return (((as21 & 0x100000) >> 20) |
		((as21 & 0x0ffe00) >> 8) |
		((as21 & 0x000180) << 7) |
		((as21 & 0x00007c) << 14) |
		((as21 & 0x000003) << 12));
}

static inline int reassemble_22(int as22)
{
	return (((as22 & 0x200000) >> 21) |
		((as22 & 0x1f0000) << 5) |
		((as22 & 0x00f800) << 5) |
		((as22 & 0x000400) >> 8) |
		((as22 & 0x0003ff) << 3));
}

void *module_alloc(unsigned long size)
{
	if (size == 0)
		return NULL;
	return vmalloc(size);
}

#ifndef __LP64__
static inline unsigned long count_gots(const Elf_Rela *rela, unsigned long n)
{
	return 0;
}

static inline unsigned long count_fdescs(const Elf_Rela *rela, unsigned long n)
{
	return 0;
}

static inline unsigned long count_stubs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF32_R_TYPE(rela->r_info)) {
			case R_PARISC_PCREL17F:
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}
#else
static inline unsigned long count_gots(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_LTOFF21L:
			case R_PARISC_LTOFF14R:
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}

static inline unsigned long count_fdescs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_FPTR64:
				cnt++;
		}
	}

	return cnt;
}

static inline unsigned long count_stubs(const Elf_Rela *rela, unsigned long n)
{
	unsigned long cnt = 0;

	for (; n > 0; n--, rela++)
	{
		switch (ELF64_R_TYPE(rela->r_info)) {
			case R_PARISC_PCREL22F:
				cnt++;
		}
	}

	return cnt;
}
#endif


/* Free memory returned from module_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

#define CONST 
int module_frob_arch_sections(CONST Elf_Ehdr *hdr,
			      CONST Elf_Shdr *sechdrs,
			      CONST char *secstrings,
			      struct module *me)
{
	unsigned long gots = 0, fdescs = 0, stubs = 0, init_stubs = 0;
	unsigned int i;

	for (i = 1; i < hdr->e_shnum; i++) {
		const Elf_Rela *rels = (void *)hdr + sechdrs[i].sh_offset;
		unsigned long nrels = sechdrs[i].sh_size / sizeof(*rels);

		if (sechdrs[i].sh_type != SHT_RELA)
			continue;

		/* some of these are not relevant for 32-bit/64-bit
		 * we leave them here to make the code common. the
		 * compiler will do its thing and optimize out the
		 * stuff we don't need
		 */
		gots += count_gots(rels, nrels);
		fdescs += count_fdescs(rels, nrels);
		if(strncmp(secstrings + sechdrs[i].sh_name,
			   ".rela.init", 10) == 0)
			init_stubs += count_stubs(rels, nrels);
		else
			stubs += count_stubs(rels, nrels);
	}

	/* align things a bit */
	me->core_size = ALIGN(me->core_size, 16);
	me->arch.got_offset = me->core_size;
	me->core_size += gots * sizeof(struct got_entry);

	me->core_size = ALIGN(me->core_size, 16);
	me->arch.fdesc_offset = me->core_size;
	me->core_size += fdescs * sizeof(struct fdesc_entry);

	me->core_size = ALIGN(me->core_size, 16);
	me->arch.stub_offset = me->core_size;
	me->core_size += stubs * sizeof(struct stub_entry);

	me->init_size = ALIGN(me->init_size, 16);
	me->arch.init_stub_offset = me->init_size;
	me->init_size += init_stubs * sizeof(struct stub_entry);

	me->arch.got_max = gots;
	me->arch.fdesc_max = fdescs;
	me->arch.stub_max = stubs;
	me->arch.init_stub_max = init_stubs;

	return 0;
}

#ifdef __LP64__
static Elf64_Word get_got(struct module *me, unsigned long value, long addend)
{
	unsigned int i;
	struct got_entry *got;

	value += addend;

	BUG_ON(value == 0);

	got = me->module_core + me->arch.got_offset;
	for (i = 0; got[i].addr; i++)
		if (got[i].addr == value)
			return i * sizeof(struct got_entry);

	BUG_ON(++me->arch.got_count > me->arch.got_max);

	got[i].addr = value;
	return i * sizeof(struct got_entry);
}
#endif /* __LP64__ */

#ifdef __LP64__
static Elf_Addr get_fdesc(struct module *me, unsigned long value)
{
	struct fdesc_entry *fdesc = me->module_core + me->arch.fdesc_offset;

	if (!value) {
		printk(KERN_ERR "%s: zero OPD requested!\n", me->name);
		return 0;
	}

	/* Look for existing fdesc entry. */
	while (fdesc->addr) {
		if (fdesc->addr == value)
			return (Elf_Addr)fdesc;
		fdesc++;
	}

	BUG_ON(++me->arch.fdesc_count > me->arch.fdesc_max);

	/* Create new one */
	fdesc->addr = value;
	fdesc->gp = (Elf_Addr)me->module_core + me->arch.got_offset;
	return (Elf_Addr)fdesc;
}
#endif /* __LP64__ */

static Elf_Addr get_stub(struct module *me, unsigned long value, long addend,
	int millicode, int init_section)
{
	unsigned long i;
	struct stub_entry *stub;

	if(init_section) {
		i = me->arch.init_stub_count++;
		BUG_ON(me->arch.init_stub_count > me->arch.init_stub_max);
		stub = me->module_init + me->arch.init_stub_offset + 
			i * sizeof(struct stub_entry);
	} else {
		i = me->arch.stub_count++;
		BUG_ON(me->arch.stub_count > me->arch.stub_max);
		stub = me->module_core + me->arch.stub_offset + 
			i * sizeof(struct stub_entry);
	}

#ifndef __LP64__
/* for 32-bit the stub looks like this:
 * 	ldil L'XXX,%r1
 * 	be,n R'XXX(%sr4,%r1)
 */
	//value = *(unsigned long *)((value + addend) & ~3); /* why? */

	stub->insns[0] = 0x20200000;	/* ldil L'XXX,%r1	*/
	stub->insns[1] = 0xe0202002;	/* be,n R'XXX(%sr4,%r1)	*/

	stub->insns[0] |= reassemble_21(lrsel(value, addend));
	stub->insns[1] |= reassemble_17(rrsel(value, addend) / 4);

#else
/* for 64-bit we have two kinds of stubs:
 * for normal function calls:
 * 	ldd 0(%dp),%dp
 * 	ldd 10(%dp), %r1
 * 	bve (%r1)
 * 	ldd 18(%dp), %dp
 *
 * for millicode:
 * 	ldil 0, %r1
 * 	ldo 0(%r1), %r1
 * 	ldd 10(%r1), %r1
 * 	bve,n (%r1)
 */
	if (!millicode)
	{
		stub->insns[0] = 0x537b0000;	/* ldd 0(%dp),%dp	*/
		stub->insns[1] = 0x53610020;	/* ldd 10(%dp),%r1	*/
		stub->insns[2] = 0xe820d000;	/* bve (%r1)		*/
		stub->insns[3] = 0x537b0030;	/* ldd 18(%dp),%dp	*/

		stub->insns[0] |= reassemble_14(rrsel(get_got(me, value, addend),0));
	}
	else
	{
		stub->insns[0] = 0x20200000;	/* ldil 0,%r1		*/
		stub->insns[1] = 0x34210000;	/* ldo 0(%r1), %r1	*/
		stub->insns[2] = 0x50210020;	/* ldd 10(%r1),%r1	*/
		stub->insns[3] = 0xe820d002;	/* bve,n (%r1)		*/

		stub->insns[0] |= reassemble_21(lrsel(value, addend));
		stub->insns[1] |= reassemble_14(rrsel(value, addend));
	}
#endif

	return (Elf_Addr)stub;
}

int apply_relocate(Elf_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	/* parisc should not need this ... */
	printk(KERN_ERR "module %s: RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

#ifndef __LP64__
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf32_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf32_Sym *sym;
	Elf32_Word *loc;
	Elf32_Addr val;
	Elf32_Sword addend;
	Elf32_Addr dot;
	//unsigned long dp = (unsigned long)$global$;
	register unsigned long dp asm ("r27");

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
		      + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf32_Sym *)sechdrs[symindex].sh_addr
			+ ELF32_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}
		//dot = (sechdrs[relsec].sh_addr + rel->r_offset) & ~0x03;
		dot =  (Elf32_Addr)loc & ~0x03;

		val = sym->st_value;
		addend = rel[i].r_addend;

#if 0
#define r(t) ELF32_R_TYPE(rel[i].r_info)==t ? #t :
		DEBUGP("Symbol %s loc 0x%x val 0x%x addend 0x%x: %s\n",
			strtab + sym->st_name,
			(uint32_t)loc, val, addend,
			r(R_PARISC_PLABEL32)
			r(R_PARISC_DIR32)
			r(R_PARISC_DIR21L)
			r(R_PARISC_DIR14R)
			r(R_PARISC_SEGREL32)
			r(R_PARISC_DPREL21L)
			r(R_PARISC_DPREL14R)
			r(R_PARISC_PCREL17F)
			r(R_PARISC_PCREL22F)
			"UNKNOWN");
#undef r
#endif

		switch (ELF32_R_TYPE(rel[i].r_info)) {
		case R_PARISC_PLABEL32:
			/* 32-bit function address */
			/* no function descriptors... */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_DIR32:
			/* direct 32-bit ref */
			*loc = fsel(val, addend);
			break;
		case R_PARISC_DIR21L:
			/* left 21 bits of effective address */
			val = lrsel(val, addend);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_DIR14R:
			/* right 14 bits of effective address */
			val = rrsel(val, addend);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			val -= (uint32_t)me->module_core;
			*loc = fsel(val, addend); 
			break;
		case R_PARISC_DPREL21L:
			/* left 21 bit of relative address */
			val = lrsel(val - dp, addend);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_DPREL14R:
			/* right 14 bit of relative address */
			val = rrsel(val - dp, addend);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_PCREL17F:
			/* 17-bit PC relative address */
			val = get_stub(me, val, addend, 0, is_init(me, loc));
			val = (val - dot - 8)/4;
			CHECK_RELOC(val, 17)
			*loc = (*loc & ~0x1f1ffd) | reassemble_17(val);
			break;
		case R_PARISC_PCREL22F:
			/* 22-bit PC relative address; only defined for pa20 */
			val = get_stub(me, val, addend, 0, is_init(me, loc));
			val = (val - dot - 8)/4;
			CHECK_RELOC(val, 22);
			*loc = (*loc & ~0x3ff1ffd) | reassemble_22(val);
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %u\n",
			       me->name, ELF32_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}

	return 0;
}

#else
int apply_relocate_add(Elf_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	int i;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_addr;
	Elf64_Sym *sym;
	Elf64_Word *loc;
	Elf64_Xword *loc64;
	Elf64_Addr val;
	Elf64_Sxword addend;
	Elf64_Addr dot;

	DEBUGP("Applying relocate section %u to %u\n", relsec,
	       sechdrs[relsec].sh_info);
	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		/* This is where to make the change */
		loc = (void *)sechdrs[sechdrs[relsec].sh_info].sh_addr
		      + rel[i].r_offset;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_addr
			+ ELF64_R_SYM(rel[i].r_info);
		if (!sym->st_value) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}
		//dot = (sechdrs[relsec].sh_addr + rel->r_offset) & ~0x03;
		dot = (Elf64_Addr)loc & ~0x03;
		loc64 = (Elf64_Xword *)loc;

		val = sym->st_value;
		addend = rel[i].r_addend;

#if 0
#define r(t) ELF64_R_TYPE(rel[i].r_info)==t ? #t :
		printk("Symbol %s loc %p val 0x%Lx addend 0x%Lx: %s\n",
			strtab + sym->st_name,
			loc, val, addend,
			r(R_PARISC_LTOFF14R)
			r(R_PARISC_LTOFF21L)
			r(R_PARISC_PCREL22F)
			r(R_PARISC_DIR64)
			r(R_PARISC_SEGREL32)
			r(R_PARISC_FPTR64)
			"UNKNOWN");
#undef r
#endif

		switch (ELF64_R_TYPE(rel[i].r_info)) {
		case R_PARISC_LTOFF21L:
			/* LT-relative; left 21 bits */
			val = get_got(me, val, addend);
			DEBUGP("LTOFF21L Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			val = lrsel(val, 0);
			*loc = mask(*loc, 21) | reassemble_21(val);
			break;
		case R_PARISC_LTOFF14R:
			/* L(ltoff(val+addend)) */
			/* LT-relative; right 14 bits */
			val = get_got(me, val, addend);
			val = rrsel(val, 0);
			DEBUGP("LTOFF14R Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			*loc = mask(*loc, 14) | reassemble_14(val);
			break;
		case R_PARISC_PCREL22F:
			/* PC-relative; 22 bits */
			DEBUGP("PCREL22F Symbol %s loc %p val %lx\n",
			       strtab + sym->st_name,
			       loc, val);
			/* can we reach it locally? */
			if(!is_local(me, (void *)val)) {
				if (strncmp(strtab + sym->st_name, "$$", 2)
				    == 0)
					val = get_stub(me, val, addend, 1,
						       is_init(me, loc));
				else
					val = get_stub(me, val, addend, 0,
						       is_init(me, loc));
			}
			/* FIXME: local symbols work as long as the
			 * core and init pieces aren't separated too
			 * far.  If this is ever broken, you will trip
			 * the check below.  The way to fix it would
			 * be to generate local stubs to go between init
			 * and core */
			if((Elf64_Sxword)(val - dot - 8) > 0x800000 -1 ||
			   (Elf64_Sxword)(val - dot - 8) < -0x800000) {
				printk(KERN_ERR "Module %s, symbol %s is out of range for PCREL22F relocation\n",
				       me->name, strtab + sym->st_name);
				return -ENOEXEC;
			}
			val = (val - dot - 8)/4;
			*loc = (*loc & ~0x3ff1ffd) | reassemble_22(val);
			break;
		case R_PARISC_DIR64:
			/* 64-bit effective address */
			*loc64 = val + addend;
			break;
		case R_PARISC_SEGREL32:
			/* 32-bit segment relative address */
			val -= (uint64_t)me->module_core;
			*loc = fsel(val, addend); 
			break;
		case R_PARISC_FPTR64:
			/* 64-bit function address */
			if(is_local(me, (void *)(val + addend))) {
				*loc64 = get_fdesc(me, val+addend);
			} else {
				/* if the symbol is not local to this
				 * module then val+addend is a pointer
				 * to the function descriptor */
				DEBUGP("Non local FPTR64 Symbol %s loc %p val %lx\n",
				       strtab + sym->st_name,
				       loc, val);
				*loc64 = val + addend;
			}
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %Lu\n",
			       me->name, ELF64_R_TYPE(rel[i].r_info));
			return -ENOEXEC;
		}
	}
	return 0;
}
#endif

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
#ifdef DEBUG
	struct fdesc_entry *entry;
	u32 *addr;

	entry = (struct fdesc_entry *)me->init;
	printk("FINALIZE, ->init FPTR is %p, GP %lx ADDR %lx\n", entry,
	       entry->gp, entry->addr);
	addr = (u32 *)entry->addr;
	printk("INSNS: %x %x %x %x\n",
	       addr[0], addr[1], addr[2], addr[3]);
	printk("stubs used %ld, stubs max %ld\n"
	       "init_stubs used %ld, init stubs max %ld\n"
	       "got entries used %ld, gots max %ld\n"
	       "fdescs used %ld, fdescs max %ld\n",
	       me->arch.stub_count, me->arch.stub_max,
	       me->arch.init_stub_count, me->arch.init_stub_max,
	       me->arch.got_count, me->arch.got_max,
	       me->arch.fdesc_count, me->arch.fdesc_max);
#endif
		
	return 0;
}

void module_arch_cleanup(struct module *mod)
{
}
