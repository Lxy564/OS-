#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#ifdef USE_SLUB
#include <slub.h>
#endif
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    // lab2 2411846: select the page allocator for SLUB tests.
#ifdef USE_BUDDY
    pmm_manager = &buddy_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1
// page_init - build the page descriptor array and register the free memory
static void page_init(void) {
    extern char end[];

    uint64_t mem_base, mem_size, mem_end;
    uintptr_t pages_pa, pages_end_pa, free_start_pa, free_end_pa;
    size_t nr_page, nr_free, i;

    // 1. get the physical memory range detected from the device tree
    mem_base = get_memory_base();
    mem_size = get_memory_size();

    // the range must start in DRAM, be non-empty and not overflow
    if (mem_base != DRAM_BASE || mem_size == 0 || mem_base + mem_size < mem_base) {
        panic("page_init: invalid physical memory info from DTB");
    }
    mem_end = mem_base + mem_size;   // physical address, exclusive
    cprintf("physical memory range: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base, (unsigned long)mem_end);

    // 2. kernel is mapped at VA = PA + va_pa_offset
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // npage is the first physical page number beyond DRAM;
    // page descriptors are indexed from nbase
    // lab2 2411846: cap and align the manageable RAM limit.
    if (mem_end > PHYSICAL_MEMORY_END) {
        mem_end = PHYSICAL_MEMORY_END;
    }
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: memory range contains no physical page");
    }

    // 3. place the page descriptor array right after the kernel image
    //    (virtual address here), page aligned
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    pages_pa = PADDR((uintptr_t)pages);
    nr_page = npage - nbase;
    // lab2 2411846: validate descriptor capacity before writing.
    if (pages_pa >= mem_end ||
        nr_page > (mem_end - pages_pa) / sizeof(struct Page)) {
        panic("page_init: Page descriptors do not fit into RAM");
    }
    pages_end_pa = pages_pa + nr_page * sizeof(struct Page);

    // 4. reserve every page first (OpenSBI, kernel image, page descriptors);
    //    init_memmap() will clear PG_reserved for the pages it receives
    for (i = 0; i < nr_page; i ++) {
        pages[i].flags = 0;
        pages[i].property = 0;
        set_page_ref(&pages[i], 0);
        SetPageReserved(&pages[i]);
    }

    // 5. free area: from the end of the descriptor array up to the DRAM end,
    //    with both bounds page aligned
    free_start_pa = ROUNDUP(pages_end_pa, PGSIZE);
    free_end_pa = ROUNDDOWN(mem_end, PGSIZE);

    // never call init_memmap() unless at least one whole free page exists
    if (free_start_pa < free_end_pa) {
        nr_free = (free_end_pa - free_start_pa) / PGSIZE;
        init_memmap(pa2page(free_start_pa), nr_free);
        cprintf("free physical memory: [0x%016lx, 0x%016lx), %lu pages\n",
                (unsigned long)free_start_pa, (unsigned long)free_end_pa,
                (unsigned long)nr_free);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();
#ifdef USE_SLUB
    slub_init();
    slub_check();
#endif

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
