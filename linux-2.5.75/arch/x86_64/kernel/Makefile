#
# Makefile for the linux kernel.
#

extra-y 	:= head.o head64.o init_task.o
EXTRA_AFLAGS	:= -traditional
obj-y	:= process.o semaphore.o signal.o entry.o traps.o irq.o \
		ptrace.o i8259.o ioport.o ldt.o setup.o time.o sys_x86_64.o \
		pci-dma.o x8664_ksyms.o i387.o syscall.o vsyscall.o \
		setup64.o bluesmoke.o bootflag.o e820.o reboot.o warmreboot.o

obj-$(CONFIG_MTRR)	+= mtrr/
obj-$(CONFIG_ACPI)	+= acpi/
obj-$(CONFIG_X86_MSR)	+= msr.o
obj-$(CONFIG_X86_CPUID)	+= cpuid.o
obj-$(CONFIG_SMP)	+= smp.o smpboot.o trampoline.o
obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o  nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o mpparse.o
obj-$(CONFIG_SOFTWARE_SUSPEND)	+= suspend.o suspend_asm.o
obj-$(CONFIG_EARLY_PRINTK)    += early_printk.o
obj-$(CONFIG_GART_IOMMU) += pci-gart.o aperture.o
obj-$(CONFIG_DUMMY_IOMMU) += pci-nommu.o
obj-$(CONFIG_MODULES) += module.o

$(obj)/bootflag.c: 
	@ln -sf ../../i386/kernel/bootflag.c $(obj)/bootflag.c
$(obj)/cpuid.c: 
	@ln -sf ../../i386/kernel/cpuid.c $(obj)/cpuid.c

clean-files += bootflag.c cpuid.c

