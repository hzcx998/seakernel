/* generic page-fault handling */
#include <sea/kernel.h>
#include <sea/mm/vmm.h>
#include <sea/mm/map.h>
#include <sea/string.h>
#include <sea/cpu/processor.h>
#include <sea/vsprintf.h>
#include <sea/cpu/interrupt.h>
#include <sea/syscall.h>
#include <sea/mm/pmm.h>
static int do_map_page(addr_t addr, unsigned attr)
{
	mm_virtual_trymap(addr, attr, mm_page_size(0));
	return 1;
}

static int map_in_page(addr_t address)
{
	/* check if the memory is for the heap */
	if(address >= current_process->heap_start && address <= current_process->heap_end)
		return do_map_page(address, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	/* and check if the memory is for the stack */
	if(address >= MEMMAP_IMAGE_MAXIMUM && address < (MEMMAP_USERSPACE_MAXIMUM)) {
		printk(0, "map for stack: %x!\n", address);
#warning: "don't do this, rely on mmap"
		return do_map_page(address, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	}
	return 0;
}

void mm_page_fault_handler(registers_t *regs, addr_t address, int pf_cause)
{
	/* here the story of the horrible Page Fault. If this function gets
	 * called, some part of code has accessed some dark corners of memory
	 * that don't exist. Or it has accessed something that the kernel's
	 * lazy ass decided to put off mapping until later.
	 *
	 * should this function return, the instruction will be re-tried. In
	 * theory, this doesn't matter - if this function is implemented correctly,
	 * then it will always either prevent another page fault or kill the task.
	 *
	 * if there is a pagefault in the kernel, we (almost always) assume that
	 * we're screwed. In almost all cases, a pagefault in the kernel is a
	 * bug in the kernel.
	 */
	assert(regs);
	if(pf_cause & PF_CAUSE_USER) {
		/* check if we need to map a page for mmap, etc */
		if(pd_cur_data) {
			if(mm_page_fault_test_mappings(address, pf_cause) == 0) {
				return;
			}
			/* if that didn't work, lets see if we should map a page
		 	 * for the heap, or whatever */
			//mutex_acquire(&pd_cur_data->lock);
#warning "DONT DO THIS"
			if(map_in_page(address)) {
				//mutex_release(&pd_cur_data->lock);
				return;
			}
			/* ...and if that didn't work, the task did something evil */
			//mutex_release(&pd_cur_data->lock);
		} else {
			panic(PANIC_MEM | PANIC_NOSYNC, "page fault was from userspace, but pd_cur_data is invalid");
		}
		kprintf("[mm]: %d: segmentation fault at eip=%x\n", current_thread->tid, regs->eip);
		printk(0, "[mm]: %d: cause = %x, address = %x\n", current_thread->tid, pf_cause, address);
		printk(0, "[mm]: %d: heap %x -> %x, flags = %x\n",
				current_thread->tid, current_process->heap_start, 
				current_process->heap_end, current_thread->flags);

		tm_signal_send_thread(current_thread, SIGSEGV);
		return;
	} else {
		if(pd_cur_data && !IS_KERN_MEM(address)) {
			/* okay, maybe we have a section of memory that a process
			 * allocated, that wasn't mapped in during sbrk. If this
			 * memory is used as a buffer, and passed to the kernel
			 * during read, for example, the kernel might try to write
			 * something to it, causing a fault. So, even though we're
			 * in the kernel, check that case. */
			if(mm_page_fault_test_mappings(address, pf_cause) == 0)
				return;
			//mutex_acquire(&pd_cur_data->lock);
			if(map_in_page(address)) {
				//mutex_release(&pd_cur_data->lock);
				return;
			}
			/* ...and if that didn't work, the task did something evil */
			//mutex_release(&pd_cur_data->lock);
			tm_signal_send_thread(current_thread, SIGSEGV);
			return;
		}
	}
	if(!current_thread) {
		panic(PANIC_MEM | PANIC_NOSYNC, "early page fault (addr=%x, cause=%x, from=%x)", address, pf_cause, regs->eip);
	}
	panic(PANIC_MEM | PANIC_NOSYNC, "page fault (addr=%x, cause=%x, from=%x)", address, pf_cause, regs->eip);
}

