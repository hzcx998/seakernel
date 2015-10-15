/* pit.c: Copyright (c) 2010 Daniel Bittman
 * Functions for handling the PIT
 */
#include <sea/cpu/interrupt.h>
#include <sea/tm/process.h>

#include <sea/cpu/interrupt.h>
#include <sea/cpu/cpu-io.h>
#include <sea/tm/timing.h>

void arch_cpu_timer_install(int hz)
{
	cpu_interrupt_register_handler(IRQ0, &tm_timer_handler);
	u32int divisor = 1193180 / hz;
	outb(0x43, 0x36);
	u8int l = (u8int)(divisor & 0xFF);
	u8int h = (u8int)( (divisor>>8) & 0xFF );
	outb(0x40, l);
	outb(0x40, h);
}