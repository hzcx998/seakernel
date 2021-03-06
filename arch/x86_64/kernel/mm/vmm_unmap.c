/* Functions for unmapping addresses */
#include <sea/mm/vmm.h>
#include <sea/cpu/interrupt.h>
#include <sea/tm/process.h>
#include <sea/cpu/processor.h>
#include <sea/cpu/cpu-x86_64.h>
#include <sea/asm/system.h>

addr_t arch_mm_context_virtual_unmap(struct vmm_context *ctx, addr_t address)
{
	int pml4idx = PML4_INDEX(address);
	int pdptidx = PDPT_INDEX(address);
	int pdidx = PD_INDEX(address);

	addr_t destp, offset;
	addr_t *pml4v = (addr_t *)ctx->root_virtual;
	if(!pml4v[pml4idx]) {
		return 0;
	}
	addr_t *pdptv = (addr_t *)((pml4v[pml4idx] & PAGE_MASK_PHYSICAL) + PHYS_PAGE_MAP);
	if(!pdptv[pdptidx]) {
		return 0;
	}
	addr_t *pdv = (addr_t *)((pdptv[pdptidx] & PAGE_MASK_PHYSICAL) + PHYS_PAGE_MAP);
	if(!(pdv[pdidx] & PAGE_LARGE)) {
		int ptidx = PT_INDEX(address);

		if(!pdv[pdidx]) {
			return 0;
		}
		addr_t *ptv = (addr_t *)((pdv[pdidx] & PAGE_MASK_PHYSICAL) + PHYS_PAGE_MAP);
		if(!ptv[ptidx]) {
			return 0;
		}
		destp = ptv[ptidx] & PAGE_MASK_PHYSICAL;
		ptv[ptidx] = 0;
	} else {
		if(!pdv[pdidx]) {
			return 0;
		}
		destp = pdv[pdidx] & PAGE_MASK_PHYSICAL;
		pdv[pdidx] = 0;
	}
	asm volatile("invlpg (%0)" :: "r"(address));
#if CONFIG_SMP
	x86_maybe_tlb_shootdown(address);
#endif
	return destp;
}

addr_t arch_mm_virtual_unmap(addr_t address)
{
	struct vmm_context *ctx;
	if(current_process) {
		ctx = &current_process->vmm_context;
	} else {
		ctx = &kernel_context;
	}

	return arch_mm_context_virtual_unmap(ctx, address);
}


