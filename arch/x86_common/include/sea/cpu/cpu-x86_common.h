#ifndef __CPU_X86_COMMON_H
#define __CPU_X86_COMMON_H

#include <sea/mutex.h>
#include <sea/types.h>
#include <sea/cpu/processor.h>

#define APIC_BCAST_ID			0xFF
#define	APIC_VERSION(x)			((x) & 0xFF)
#define	APIC_MAXREDIR(x)		(((x) >> 16) & 0xFF)
#define	APIC_ID(x)				((x) >> 24)
#define APIC_VER_NEW			0x10

#define IOAPIC_REGSEL			0
#define IOAPIC_RW				0x10
#define	IOAPIC_ID				0
#define	IOAPIC_VER				1
#define	IOAPIC_REDIR			0x10

#define LAPIC_DISABLE           0x10000
#define LAPIC_ID				0x20
#define LAPIC_VER				0x30
#define LAPIC_TPR				0x80
#define LAPIC_APR				0x90
#define LAPIC_PPR				0xA0
#define LAPIC_EOI				0xB0
#define LAPIC_LDR				0xD0
#define LAPIC_DFR				0xE0
#define LAPIC_SPIV				0xF0
#define	LAPIC_SPIV_ENABLE_APIC	0x100
#define LAPIC_ISR				0x100
#define LAPIC_TMR				0x180
#define LAPIC_IRR				0x200
#define LAPIC_ESR				0x280
#define LAPIC_ICR				0x300
#define	LAPIC_ICR_DS_SELF		0x40000
#define	LAPIC_ICR_DS_ALLINC		0x80000
#define	LAPIC_ICR_DS_ALLEX		0xC0000
#define	LAPIC_ICR_TM_LEVEL		0x8000
#define	LAPIC_ICR_LEVELASSERT	0x4000
#define	LAPIC_ICR_STATUS_PEND	0x1000
#define	LAPIC_ICR_DM_LOGICAL	0x800
#define	LAPIC_ICR_DM_LOWPRI		0x100
#define	LAPIC_ICR_DM_SMI		0x200
#define	LAPIC_ICR_DM_NMI		0x400
#define	LAPIC_ICR_DM_INIT		0x500
#define	LAPIC_ICR_DM_SIPI		0x600
#define LAPIC_ICR_SHORT_DEST    0x0
#define LAPIC_ICR_SHORT_SELF    0x1
#define LAPIC_ICR_SHORT_ALL     0x2
#define LAPIC_ICR_SHORT_OTHERS  0x3

#define CPU_IPI_DEST_ALL LAPIC_ICR_SHORT_ALL
#define CPU_IPI_DEST_SELF LAPIC_ICR_SHORT_SELF
#define CPU_IPI_DEST_OTHERS LAPIC_ICR_SHORT_OTHERS

#define LAPIC_LVTT				0x320
#define LAPIC_LVTPC		       	0x340
#define LAPIC_LVT0				0x350
#define LAPIC_LVT1				0x360
#define LAPIC_LVTE				0x370
#define LAPIC_TICR				0x380
#define LAPIC_TCCR				0x390
#define LAPIC_TDCR				0x3E0

#define EBDA_SEG_ADDR			0x40E
#define BIOS_RESET_VECTOR		0x467
#define LAPIC_ADDR_DEFAULT		0xFEE00000uL
#define IOAPIC_ADDR_DEFAULT		0xFEC00000uL
#define CMOS_RESET_CODE			0xF
#define	CMOS_RESET_JUMP			0xa
#define CMOS_BASE_MEMORY		0x15

#define CMOS_WRITE_BYTE(x,y) cmos_write(x,y)
#define CMOS_READ_BYTE(x)    cmos_read(x)

#define CR0_EM          (1 << 2)
#define CR0_MP          (1 << 1)
#define CR4_OSFXSR      (1 << 9)
#define CR4_OSXMMEXCPT  (1 << 10)

#define LAPIC_READ(x)  (*((volatile unsigned *) (lapic_addr+(x))))
#define LAPIC_WRITE(x, y)   \
   (*((volatile unsigned *) (lapic_addr+(x))) = (y))
extern addr_t lapic_addr;
extern unsigned lapic_timer_start;
extern mutex_t ipi_mutex;

void load_tables_ap(cpu_t *cpu);
extern cpu_t cpu_array[CONFIG_MAX_CPUS];
void parse_cpuid(cpu_t *);
extern unsigned cpu_array_num;
extern volatile unsigned num_halted_cpus;
#if CONFIG_SMP
/* The following definitions are taken from http://www.uruk.org/mps/ */
extern unsigned num_cpus, num_booted_cpus, num_failed_cpus;
int boot_cpu(unsigned id, unsigned apic_ver);
void calibrate_lapic_timer(unsigned freq);
extern unsigned bootstrap;
void init_ioapic();
#endif /* CONFIG_SMP */

#endif