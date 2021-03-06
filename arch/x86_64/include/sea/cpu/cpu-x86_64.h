#ifndef _CPU_X86_64_H
#define _CPU_X86_64_H
#include <sea/types.h>
#define CPU_EXT_FEATURES_GBPAGE (1 << 26)

extern addr_t lapic_addr;

void init_ioapic();
void lapic_eoi();

extern int trampoline_start(void);
extern int trampoline_end(void);
extern int pmode_enter(void);
extern int pmode_enter_end(void);
extern int rm_gdt(void);
extern int rm_gdt_end(void);
extern int rm_gdt_pointer(void);

#define RM_GDT_SIZE 0x18
#define GDT_POINTER_SIZE 0x4
#define RM_GDT_START 0x7100
#define BOOTFLAG_ADDR 0x7200

void add_ioapic(addr_t address, int id, int int_start);

#include <sea/cpu/cpu-x86_common.h>

#undef LAPIC_READ
#undef LAPIC_WRITE

extern int lapic_inited;
#define LAPIC_READ(x)  \
		(lapic_inited ? (*((volatile unsigned *) (lapic_addr+(x)))) : 0)

#define LAPIC_WRITE(x,y)   do {\
	if(lapic_inited) \
		(*((volatile unsigned *) (lapic_addr+(x))) = (y)); \
	} while(0)
#endif

