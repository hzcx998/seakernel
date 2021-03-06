#include <sea/kernel.h>
#include <sea/mm/vmm.h>
#include <sea/boot/multiboot.h>
#include <sea/tm/process.h>
#include <sea/cpu/processor.h>
#include <sea/loader/symbol.h>
#include <sea/mm/pmm.h>
#include <sea/mm/vmm.h>
#include <sea/mm/dma.h>
#include <sea/mm/kmalloc.h>
#include <sea/mm/reclaim.h>
#include <sea/vsprintf.h>
#include <sea/cpu/interrupt.h>
#include <stdbool.h>
#include <sea/fs/inode.h>
#include <sea/fs/kerfs.h>
struct pagedata {
	size_t count;
};

static struct pagedata *frames;

struct vmm_context kernel_context;

unsigned long pm_num_pages=0;

extern addr_t initrd_start_page, initrd_end_page, kernel_start;
static addr_t pm_location = 0;
static size_t maximum_page_number;
extern int kernel_end;

int mm_physical_get_count(addr_t page)
{
	if(!frames)
		return 0;
	if((page / PAGE_SIZE) < maximum_page_number)
		return atomic_load(&frames[page / PAGE_SIZE].count);
	return 0;
}

void mm_physical_increment_count(addr_t page)
{
	if(!frames)
		return;
	if((page / PAGE_SIZE) < maximum_page_number)
		return atomic_fetch_add(&frames[page / PAGE_SIZE].count, 1);
}

int mm_physical_decrement_count(addr_t page)
{
	if(!frames)
		return 0;
	assert(page);
	if((page / PAGE_SIZE) < maximum_page_number) {
		size_t old;
		if((old=atomic_fetch_sub(&frames[page / PAGE_SIZE].count, 1)) == 1) {
			mm_physical_deallocate(page);
		}
		assert(old);
		return old;
	}
	return 0;
}

int kerfs_frames_report(int direction, void *param, size_t size, size_t offset, size_t length, unsigned char *buf)
{
	size_t current = 0;
	for(size_t i=0;i<maximum_page_number;i++) {
		size_t count = atomic_load(&frames[i].count);
		if(count) {
			KERFS_PRINTF(offset, length, buf, current,
					"page %8d: %d\n", i, count);
		}
	}
	return current;
}

static inline bool __is_actually_free(addr_t x)
{
	if(x == 0)
		return false;
	if(x <= pm_location)
		return false;
	if(x >= initrd_start_page && x <= initrd_end_page)
		return false;
	if(x < 0x1000000) //TODO: fix this!
		return false;
	return true;
}

static void process_memorymap(struct multiboot *mboot)
{
	addr_t i = mboot->mmap_addr;
	unsigned long num_pages=0, unusable=0;
	uint64_t j=0, address, length, highest_page = 0, lowest_page = ~0;
	int found_contiguous=0;
	pm_location = ((((addr_t)&kernel_end - MEMMAP_KERNEL_START) & ~(PAGE_SIZE-1)) + PAGE_SIZE + 0x100000 /* HACK */);
	while((pm_location >= initrd_start_page && pm_location <= initrd_end_page))
		pm_location += PAGE_SIZE;

	while(i < (mboot->mmap_addr + mboot->mmap_length)){
		mmap_entry_t *me = (mmap_entry_t *)(i + MEMMAP_KERNEL_START);
		address = ((uint64_t)me->base_addr_high << 32) | (uint64_t)me->base_addr_low;
		length = (((uint64_t)me->length_high<<32) | me->length_low);
		if(me->type == 1 && length > 0)
		{
			for (j=address; 
				j < (address+length); j += PAGE_SIZE)
			{
				addr_t page;
#if ADDR_BITS == 32
				/* 32-bit can only handle the lower 32 bits of the address. If we're
				 * considering an address above 0xFFFFFFFF, we have to ignore it */
				page = (addr_t)(j & 0xFFFFFFFF);
				if((j >> 32) != 0)
					break;
#else
				page = j;
#endif
				if(__is_actually_free(page)) {
					if(lowest_page > page)
						lowest_page=page;
					if(page > highest_page)
						highest_page=page;
					num_pages++;
					mm_physical_deallocate(page);
				}
			}
		}
		i += me->size + sizeof (uint32_t);
	}
	printk(1, "[mm]: Highest page = %x, Lowest page = %x, num_pages = %d               \n", highest_page, lowest_page, num_pages);
	if(!j)
		panic(PANIC_MEM | PANIC_NOSYNC, "Memory map corrupted");
	int gbs=0;
	int mbs = ((num_pages * PAGE_SIZE)/1024)/1024;
	if(mbs < 4){
		panic(PANIC_MEM | PANIC_NOSYNC, 
				"Not enough memory, system wont work (%d MB, %d pages)", 
				mbs, num_pages);
	}
	gbs = mbs/1024;
	if(gbs > 0)
	{
		printk(KERN_MILE, "%d GB and ", gbs);
		mbs = mbs % 1024;
	}
	printk(KERN_MILE, "%d MB available memory (page size=%d KB, kmalloc=slab: ok)\n"
 			, mbs, PAGE_SIZE/1024);
	printk(1, "[mm]: num pages = %d\n", num_pages);
	pm_num_pages=num_pages;
	maximum_page_number = highest_page / PAGE_SIZE;
	set_ksf(KSF_MEMMAPPED);
}

void pmm_buddy_init();
void arch_mm_virtual_init(struct vmm_context *context);
void mm_init(struct multiboot *m)
{
	printk(KERN_DEBUG, "[mm]: Setting up Memory Management...\n");
	arch_mm_virtual_init(&kernel_context);
	cpu_interrupt_register_handler (14, &arch_mm_page_fault_handle);
	pmm_buddy_init();
	process_memorymap(m);
	slab_init(MEMMAP_KMALLOC_START, MEMMAP_KMALLOC_END);
	set_ksf(KSF_MMU);
	/* hey, look at that, we have happy memory times! */
	mm_reclaim_init();
	for(size_t i=0;i<=(sizeof(struct pagedata) * maximum_page_number) / mm_page_size(1);i++) {
		mm_virtual_map(MEMMAP_FRAMECOUNT_START + i * mm_page_size(1),
				mm_physical_allocate(mm_page_size(1), true),
				PAGE_PRESENT | PAGE_WRITE, mm_page_size(1));
	}
	frames = (struct pagedata *)(MEMMAP_FRAMECOUNT_START);
	printk(0, "[mm]: allocated %d KB for page-frame counting.\n", sizeof(struct pagedata) * maximum_page_number / 1024);
#if CONFIG_MODULES
	loader_add_kernel_symbol(slab_kmalloc);
	loader_add_kernel_symbol(slab_kfree);
	loader_add_kernel_symbol(mm_virtual_map);
	loader_add_kernel_symbol(mm_virtual_getmap);
	loader_add_kernel_symbol(mm_allocate_dma_buffer);
	loader_add_kernel_symbol(mm_free_dma_buffer);
	loader_add_kernel_symbol(mm_physical_allocate);
	loader_add_kernel_symbol(mm_physical_deallocate);
#endif
}

/* TODO: specify maximum */
static struct valloc dma_virtual;
static bool dma_virtual_init = false;
int mm_allocate_dma_buffer(struct dma_region *d)
{
	if(!atomic_exchange(&dma_virtual_init, true)) {
		valloc_create(&dma_virtual, MEMMAP_VIRTDMA_START, MEMMAP_VIRTDMA_END, mm_page_size(0), 0);
	}
	d->p.address = mm_physical_allocate(d->p.size, false);
	if(d->p.address == 0)
		return -1;

	struct valloc_region reg;
	int npages = (d->p.size - 1) / mm_page_size(0) + 1;
	valloc_allocate(&dma_virtual, &reg, npages);

	for(int i = 0;i<npages;i++)
		mm_virtual_map(reg.start + i * mm_page_size(0), d->p.address + i*mm_page_size(0), PAGE_PRESENT | PAGE_WRITE, mm_page_size(0));
	d->v = reg.start;
	return 0;
}

int mm_free_dma_buffer(struct dma_region *d)
{
	int npages = ((d->p.size-1) / mm_page_size(0)) + 1;
	for(int i=0;i<npages;i++)
		mm_virtual_unmap(d->v + i * mm_page_size(0));
	mm_physical_deallocate(d->p.address);
	struct valloc_region reg;
	reg.flags = 0;
	reg.start = d->v;
	reg.npages = npages;
	valloc_deallocate(&dma_virtual, &reg);
	d->p.address = d->v = 0;
	return 0;
}

void arch_mm_physical_memcpy(void *dest, void *src, size_t length, int mode);
void mm_physical_memcpy(void *dest, void *src, size_t length, int mode)
{
	return arch_mm_physical_memcpy(dest, src, length, mode);
}

