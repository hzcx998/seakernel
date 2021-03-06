#include <sea/asm/system.h>
#include <sea/cpu/interrupt.h>
#include <sea/cpu/processor.h>
#include <sea/kernel.h>
#include <sea/lib/linkedlist.h>
#include <sea/loader/symbol.h>
#include <sea/mm/kmalloc.h>
#include <sea/mm/vmm.h>
#include <sea/syscall.h>
#include <sea/tm/kthread.h>
#include <sea/tm/process.h>
#include <sea/tm/thread.h>
#include <sea/tm/timing.h>
#include <sea/tm/tqueue.h>
#include <sea/vsprintf.h>
#include <stdatomic.h>
#include <sea/mm/map.h>
#include <sea/tm/blocking.h>

extern struct mutex process_refs_lock;
extern struct mutex thread_refs_lock;
extern int initial_kernel_stack;
struct process *kernel_process = 0;
addr_t sysgate_page;
void tm_init_multitasking(void)
{
	printk(KERN_DEBUG, "[sched]: Starting multitasking system...\n");
	sysgate_page = mm_physical_allocate(PAGE_SIZE, true);
	mm_physical_memcpy((void *)sysgate_page,
				(void *)signal_return_injector, MEMMAP_SYSGATE_ADDRESS_SIZE, PHYS_MEMCPY_MODE_DEST);

	process_table = hash_create(0, 0, 128);

	process_list = linkedlist_create(0, LINKEDLIST_MUTEX);
	mutex_create(&process_refs_lock, 0);
	mutex_create(&thread_refs_lock, 0);
	
	thread_table = hash_create(0, 0, 128);

	struct thread *thread = kmalloc(sizeof(struct thread));
	struct process *proc = kernel_process = kmalloc(sizeof(struct process));

	proc->refs = 2;
	thread->refs = 1;
	hash_insert(process_table, &proc->pid, sizeof(proc->pid), &proc->hash_elem, proc);
	hash_insert(thread_table, &thread->tid, sizeof(thread->tid), &thread->hash_elem, thread);
	linkedlist_insert(process_list, &proc->listnode, proc);

	valloc_create(&proc->mmf_valloc, MEMMAP_MMAP_BEGIN, MEMMAP_MMAP_END, PAGE_SIZE, 0);
	linkedlist_create(&proc->threadlist, 0);
	mutex_create(&proc->map_lock, 0);
	mutex_create(&proc->stacks_lock, 0);
	mutex_create(&proc->fdlock, 0);
	hash_create(&proc->files, HASH_LOCKLESS, 64);
	proc->magic = PROCESS_MAGIC;
	blocklist_create(&proc->waitlist, 0, "process-waitlist");
	mutex_create(&proc->fdlock, 0);
	memcpy(&proc->vmm_context, &kernel_context, sizeof(kernel_context));
	thread->process = proc; /* we have to do this early, so that the vmm system can use the lock... */
	thread->state = THREADSTATE_RUNNING;
	thread->magic = THREAD_MAGIC;
	workqueue_create(&thread->resume_work, 0);
	thread->kernel_stack = (addr_t)&initial_kernel_stack;
	spinlock_create(&thread->status_lock);

	primary_cpu->active_queue = tqueue_create(0, 0);
	primary_cpu->idle_thread = thread;
	primary_cpu->numtasks=1;
	ticker_create(&primary_cpu->ticker, 0);
	workqueue_create(&primary_cpu->work, 0);
	tm_thread_add_to_process(thread, proc);
	tm_thread_add_to_cpu(thread, primary_cpu);
	atomic_fetch_add_explicit(&running_processes, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&running_threads, 1, memory_order_relaxed);
	set_ksf(KSF_THREADING);
	*(struct thread **)(thread->kernel_stack) = thread;
	primary_cpu->flags |= CPU_RUNNING;

#if CONFIG_MODULES
	loader_add_kernel_symbol(tm_thread_delay_sleep);
	loader_add_kernel_symbol(tm_thread_delay);
	loader_add_kernel_symbol(tm_timing_get_microseconds);
	loader_add_kernel_symbol(tm_thread_set_state);
	loader_add_kernel_symbol(tm_thread_exit);
	loader_add_kernel_symbol(tm_thread_poke);
	loader_add_kernel_symbol(tm_thread_block);
	loader_add_kernel_symbol(tm_thread_got_signal);
	loader_add_kernel_symbol(tm_thread_unblock);
	loader_add_kernel_symbol(tm_blocklist_wakeall);
	loader_add_kernel_symbol(kthread_create);
	loader_add_kernel_symbol(kthread_wait);
	loader_add_kernel_symbol(kthread_join);
	loader_add_kernel_symbol(kthread_kill);
	loader_add_kernel_symbol(tm_schedule);
	loader_add_kernel_symbol(arch_tm_get_current_thread);
#endif
}

void tm_thread_user_mode_jump(void (*fn)(void))
{
	cpu_set_kernel_stack(current_thread->cpu, (addr_t)current_thread->kernel_stack,
			(addr_t)current_thread->kernel_stack + (KERN_STACK_SIZE-STACK_ELEMENT_SIZE));
	arch_tm_jump_to_user_mode((addr_t)fn);
}

