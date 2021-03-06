/* valloc.c: 2014
 * Allocates regions of virtual memory, of nearly arbitrary granularity
 * (must be multiples of PAGE_SIZE)
 *
 * current implementation is done as a bitmap with next-fit. This
 * isn't particularly good, or anything, but it's reasonably fast
 * and simple.
 *
 * WARNING: This is REALLY SLOW for large areas (>32 bits) of virtual
 * memory */

#include <sea/kernel.h>
#include <sea/mm/valloc.h>
#include <sea/mm/vmm.h>
#include <sea/mm/kmalloc.h>
#include <sea/kobj.h>
#define SET_BIT(start,index) \
	(*((uint8_t *)start + (index / 8)) |= (1 << (index % 8)))

#define CLEAR_BIT(start,index) \
	(*((uint8_t *)start + (index / 8)) &= ~(1 << (index % 8)))

#define TEST_BIT(start,index) \
	(*((uint8_t *)start + (index / 8)) & (1 << (index % 8)))

static void __valloc_set_bits(struct valloc *va, long start, long count)
{
	for(long idx = start;idx < (start + count); idx++) {
		if(start >= va->nindex)
			assert(!TEST_BIT(va->start, idx));
		SET_BIT(va->start, idx);
	}
}

static int __valloc_count_bits(struct valloc *va)
{
	int count = 0;
	for(long idx = 0;idx < va->npages; idx++) {
		if(TEST_BIT(va->start, idx))
			++count;
	}
	return count;
}

static void __valloc_clear_bits(struct valloc *va, long start, long count)
{
	for(long idx = start;idx < (start + count); idx++) {
		if(start >= va->nindex)
			assert(TEST_BIT(va->start, idx));
		CLEAR_BIT(va->start, idx);
		assert(!TEST_BIT(va->start, idx));
	}
}

/* performs a linear next-fit search */
static long __valloc_get_start_index(struct valloc *va, long np)
{
	long start = -1;
	long count = 0;
	long idx = va->last;
	if(idx >= va->npages)
		idx=0;
	long prev = idx;
	do {
		int res = TEST_BIT(va->start, idx);
		if(start == -1 && res == 0) {
			/* we aren't checking a region for length, 
			 * and we found an empty bit. Start checking
			 * this region for length */
			start = idx;
			count=1;
			if(count == np)
				break;
		} else {
			/* we're checking a region for length. If this
			 * bit is 0, then we add to the count. Otherwise,
			 * we reset start to start looking for a new region */
			if(res == 0)
				count++;
			else {
				start = -1;
				count = 0;
			}
			/* if this region is long enough, break out */
			if(count == np)
				break;
		}
		idx++;
		/* need to wrap around due to next-fit */
		if(idx >= va->npages) {
			/* the region can't wrap around... */
			start = -1;
			count=0;
			idx=0;
		}
	} while(idx != prev);
	/* if it's a reasonable and legal region, return it */
	if(start != -1 && start < va->npages && count >= np) {
		/* and update 'last' for next-fit */
		va->last = start + np;
		return start;
	}
	return -1;
}

static void __valloc_populate_index(struct valloc *va, int flags)
{
	/* we can have larger pages than PAGE_SIZE, but here
	 * we need to map it in, and we need to work in increments
	 * of the system page size */
	size_t map_size = mm_page_size_closest(va->psize * va->nindex);
	int mm_pages = ((va->nindex * va->psize - 1) / map_size) + 1;
	for(int i=0;i<mm_pages;i++) {
		int attr = PAGE_PRESENT | PAGE_WRITE;
		if(!mm_virtual_getmap(va->start + i * map_size, NULL, NULL)) {
			addr_t phys = mm_physical_allocate(map_size, true);
			if(!mm_virtual_map(va->start + i * map_size, phys, attr, map_size)) {
				mm_physical_deallocate(phys);
			} else {
				memset((void *)(va->start + i * map_size), 0, map_size);
			}
		} else {
			memset((void *)(va->start + i * map_size), 0, map_size);
		}

	}
	__valloc_set_bits(va, 0, va->nindex);
}

static void __valloc_depopulate_index(struct valloc *va)
{
	int mm_pages = (va->nindex * va->psize) / PAGE_SIZE;
	for(int i=0;i<mm_pages;i++) {
		addr_t phys;
		if((phys = mm_virtual_unmap(va->start + i * PAGE_SIZE))) {
			mm_physical_deallocate(phys);
		}
	}
}

struct valloc *valloc_create(struct valloc *va, addr_t start, addr_t end, size_t page_size,
		int flags)
{
	int leftover = (end - start) % page_size;
	end -= leftover;
	assert(!((end - start) % page_size));
	KOBJ_CREATE(va, flags, VALLOC_ALLOC);
	va->magic = VALLOC_MAGIC;
	va->start = start;
	va->end = end;
	va->psize = page_size;
	mutex_create(&va->lock, 0);

	va->npages = (end - start) / page_size;
	va->nindex = ((va->npages-1) / (8 * page_size)) + 1;
	__valloc_populate_index(va, flags);
	return va;
}

void valloc_destroy(struct valloc *va)
{
	assert(va->magic == VALLOC_MAGIC);
	mutex_destroy(&va->lock);
	__valloc_depopulate_index(va);
	KOBJ_DESTROY(va, VALLOC_ALLOC);
}

struct valloc_region *valloc_allocate(struct valloc *va, struct valloc_region *reg,
		size_t np)
{
	assert(va->magic == VALLOC_MAGIC);
	mutex_acquire(&va->lock);
	/* find and set the region */
	long index = __valloc_get_start_index(va, np);
	__valloc_set_bits(va, index, np);
	assert(index < va->npages);
	mutex_release(&va->lock);
	if(index == -1)
		return 0;
	KOBJ_CREATE(reg, 0, VALLOC_ALLOC);
	reg->start = va->start + index * va->psize;
	reg->npages = np;
	return reg;
}

void valloc_reserve(struct valloc *va, struct valloc_region *reg)
{
	assert(va->magic == VALLOC_MAGIC);
	mutex_acquire(&va->lock);
	int start_index = (reg->start - va->start) / va->psize;
	assert(start_index+reg->npages <= va->npages && start_index >= va->nindex);
	__valloc_set_bits(va, start_index, reg->npages);
	mutex_release(&va->lock);
}

int valloc_count_used(struct valloc *va)
{
	assert(va->magic == VALLOC_MAGIC);
	mutex_acquire(&va->lock);
	int ret = __valloc_count_bits(va);
	mutex_release(&va->lock);
	return ret;
}

void valloc_deallocate(struct valloc *va, struct valloc_region *reg)
{
	assert(va->magic == VALLOC_MAGIC);
	mutex_acquire(&va->lock);
	int start_index = (reg->start - va->start) / va->psize;
	assert(start_index+reg->npages <= va->npages && start_index >= va->nindex);
	__valloc_clear_bits(va, start_index, reg->npages);
	va->last = start_index;
	mutex_release(&va->lock);
	KOBJ_DESTROY(reg, VALLOC_ALLOC);
}

struct valloc_region *valloc_split_region(struct valloc *va, struct valloc_region *reg,
		struct valloc_region *nr, size_t np)
{
	assert(va->magic == VALLOC_MAGIC);
	KOBJ_CREATE(nr, 0, VALLOC_ALLOC);
	mutex_acquire(&va->lock);
	/* with the newly created valloc_region, we can fix up the
	 * data (resize and set the pointer for the new one).
	 * The bitmap doesn't need to be updated! */
	long old = reg->npages;
	reg->npages = np;
	long start_index_reg = (reg->start - va->start) / va->psize;
	long start_index_nr = start_index_reg + np;
	nr->start = va->start + start_index_nr * va->psize;
	nr->npages = old - np;
	mutex_release(&va->lock);
	return nr;
}

