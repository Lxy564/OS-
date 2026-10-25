#include <pmm.h>
#include <list.h>
#include <buddy_pmm.h>

// buddy system: bucket[k] holds the free blocks of 2^k pages
#define BUDDY_MAX_ORDER 31

static free_area_t buddy_free_area[BUDDY_MAX_ORDER + 1];
static size_t buddy_nr_free;

// order of the smallest power-of-two block that can hold n pages
static int
buddy_order(size_t n) {
    int order = 0;
    size_t size = 1;
    while (size < n) {
        size <<= 1;
        order ++;
    }
    return order;
}

// put a free block of 2^order pages into its bucket
static void
buddy_add_block(struct Page *base, int order) {
    base->property = (unsigned int)((size_t)1 << order);
    SetPageProperty(base);
    list_add(&(buddy_free_area[order].free_list), &(base->page_link));
    buddy_free_area[order].nr_free += (unsigned int)((size_t)1 << order);
}

// take a free block out of its bucket
static void
buddy_del_block(struct Page *base, int order) {
    list_del(&(base->page_link));
    ClearPageProperty(base);
    base->property = 0;
    buddy_free_area[order].nr_free -= (unsigned int)((size_t)1 << order);
}

static void
buddy_init(void) {
    int i;
    for (i = 0; i <= BUDDY_MAX_ORDER; i ++) {
        list_init(&(buddy_free_area[i].free_list));
        buddy_free_area[i].nr_free = 0;
    }
    buddy_nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    // split [base, base + n) into maximal blocks which are aligned to
    // their own size relative to the page descriptor array
    size_t offset = base - pages;
    struct Page *block = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t size = 1;
        while (size * 2 <= remaining && (offset & (size * 2 - 1)) == 0) {
            size *= 2;
        }
        buddy_add_block(block, buddy_order(size));
        block += size;
        offset += size;
        remaining -= size;
    }
    buddy_nr_free += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    if (n == 0) {
        return NULL;
    }
    if (n > buddy_nr_free) {
        return NULL;
    }
    int order = buddy_order(n);
    if (order > BUDDY_MAX_ORDER) {
        return NULL;
    }

    // find the smallest free block that can satisfy the request
    int k = order;
    while (k <= BUDDY_MAX_ORDER && list_empty(&(buddy_free_area[k].free_list))) {
        k ++;
    }
    if (k > BUDDY_MAX_ORDER) {
        return NULL;
    }

    list_entry_t *le = list_next(&(buddy_free_area[k].free_list));
    struct Page *page = le2page(le, page_link);
    buddy_del_block(page, k);

    // split repeatedly: the upper half goes back to the smaller bucket
    while (k > order) {
        k --;
        buddy_add_block(page + ((size_t)1 << k), k);
    }
    buddy_nr_free -= ((size_t)1 << order);
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    int order = buddy_order(n);
    assert(order <= BUDDY_MAX_ORDER);
    size_t size = (size_t)1 << order;
    assert(((size_t)(base - pages) & (size - 1)) == 0);

    struct Page *p = base;
    for (; p != base + size; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    // merge with the buddy while it is free and of the same size
    while (order < BUDDY_MAX_ORDER) {
        size_t buddy_idx = (size_t)(base - pages) ^ ((size_t)1 << order);
        if (buddy_idx >= npage - nbase) {
            break;
        }
        struct Page *buddy = pages + buddy_idx;
        if (!PageProperty(buddy) ||
            buddy->property != (unsigned int)((size_t)1 << order)) {
            break;
        }
        buddy_del_block(buddy, order);
        if (buddy < base) {
            base = buddy;
        }
        order ++;
    }
    buddy_add_block(base, order);
    buddy_nr_free += size;
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

// check that every bucket is well formed and no two buddies are both free
static void
buddy_check_state(void) {
    size_t total = 0;
    int k;
    for (k = 0; k <= BUDDY_MAX_ORDER; k ++) {
        size_t count = 0;
        list_entry_t *le = &(buddy_free_area[k].free_list);
        while ((le = list_next(le)) != &(buddy_free_area[k].free_list)) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == (unsigned int)((size_t)1 << k));
            assert(((size_t)(p - pages) & (((size_t)1 << k) - 1)) == 0);

            size_t buddy_idx = (size_t)(p - pages) ^ ((size_t)1 << k);
            if (buddy_idx < npage - nbase) {
                struct Page *buddy = pages + buddy_idx;
                assert(!(PageProperty(buddy) &&
                         buddy->property == (unsigned int)((size_t)1 << k)));
            }
            count += (size_t)1 << k;
        }
        assert(count == buddy_free_area[k].nr_free);
        total += count;
    }
    assert(total == buddy_nr_free);
}

static void
buddy_check(void) {
    struct Page *p0, *p1, *p2, *p;
    struct Page *blocks[8];
    free_area_t bucket_store[BUDDY_MAX_ORDER + 1];
    size_t request[5] = {1, 2, 3, 5, 7};
    size_t actual[5] = {1, 2, 4, 8, 8};
    size_t nr_free_store = buddy_nr_free;
    size_t size, total, nr_free_temp;
    int empty_order, big_order, found;
    size_t i, k;

    assert(buddy_nr_free == nr_free_pages());
    buddy_check_state();

    // 1. basic allocation and free of single pages
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);
    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);
    assert(!PageProperty(p0) && !PageProperty(p1) && !PageProperty(p2));
    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);
    assert(buddy_nr_free == nr_free_store - 3);

    // an allocated page must not appear in any free bucket, so it cannot
    // be handed out a second time
    for (i = 0; i <= BUDDY_MAX_ORDER; i ++) {
        list_entry_t *le = &(buddy_free_area[i].free_list);
        while ((le = list_next(le)) != &(buddy_free_area[i].free_list)) {
            p = le2page(le, page_link);
            assert(p != p0 && p != p1 && p != p2);
        }
    }

    // a freed page goes back to a bucket (it may merge with its buddy)
    free_page(p0);
    assert(PageProperty(p0));
    assert(buddy_nr_free == nr_free_store - 2);
    free_page(p1);
    free_page(p2);
    assert(buddy_nr_free == nr_free_store);
    buddy_check_state();

    // 2. a non power-of-two request is rounded up: 3 pages use a 4-page block
    assert((p0 = alloc_pages(3)) != NULL);
    assert(!PageProperty(p0));
    assert(((size_t)(p0 - pages) & 3) == 0);  // 4-page aligned
    assert(buddy_nr_free == nr_free_store - 4);
    for (p = p0; p != p0 + 4; p ++) {
        assert(!PageProperty(p));
    }
    free_pages(p0, 3);
    assert(buddy_nr_free == nr_free_store);
    buddy_check_state();

    // 3. split: take 2^j pages from a larger order-K block (K > j); the
    //    unused half must be left in bucket j, i.e. the request is served
    //    by splitting instead of consuming the whole block
    // find the lowest empty order j with a non-empty order K above it
    found = 0;
    for (empty_order = 0; empty_order <= BUDDY_MAX_ORDER && !found; empty_order ++) {
        if (!list_empty(&(buddy_free_area[empty_order].free_list))) {
            continue;
        }
        for (big_order = empty_order + 1; big_order <= BUDDY_MAX_ORDER; big_order ++) {
            if (!list_empty(&(buddy_free_area[big_order].free_list))) {
                found = 1;
                break;
            }
        }
    }
    assert(found);
    empty_order --;
    size = (size_t)1 << empty_order;

    assert((p0 = alloc_pages(size)) != NULL);
    assert(!PageProperty(p0));
    assert(((size_t)(p0 - pages) & (size - 1)) == 0);
    assert(!list_empty(&(buddy_free_area[empty_order].free_list)));
    assert(buddy_nr_free == nr_free_store - size);

    free_pages(p0, size);
    assert(buddy_nr_free == nr_free_store);
    buddy_check_state();

    // 4. buddy merge: allocate the two halves of the same 2^K block and
    //    free them one by one; freeing the first half alone cannot merge
    assert((p0 = alloc_pages(size)) != NULL);
    assert((p1 = alloc_pages(size)) != NULL);
    assert(p0 != p1);
    assert(p0 < p1 && p1 == p0 + size);
    assert(((size_t)(p0 - pages) ^ size) == (size_t)(p1 - pages));
    assert(!PageProperty(p0) && !PageProperty(p1));
    assert(buddy_nr_free == nr_free_store - 2 * size);

    // p1 is still allocated, so p0 merely goes back to bucket j
    free_pages(p0, size);
    assert(PageProperty(p0) && p0->property == size);
    assert(buddy_nr_free == nr_free_store - size);

    // 5. chain merge: freeing the buddy merges the pair, and the merged
    //    block keeps merging with the split remainders until the original
    //    2^K block is rebuilt
    free_pages(p1, size);
    assert(PageProperty(p0) && p0->property == ((size_t)1 << big_order));
    assert(buddy_nr_free == nr_free_store);
    buddy_check_state();

    // 6. invalid requests: zero pages and more pages than the free space
    assert(alloc_pages(0) == NULL);
    assert(alloc_pages(nr_free_pages() + 1) == NULL);

    // an allocator with no free memory must fail as well; the buckets are
    // hidden temporarily and restored afterwards (as in basic_check)
    nr_free_temp = buddy_nr_free;
    for (i = 0; i <= BUDDY_MAX_ORDER; i ++) {
        bucket_store[i] = buddy_free_area[i];
        list_init(&(buddy_free_area[i].free_list));
        buddy_free_area[i].nr_free = 0;
    }
    buddy_nr_free = 0;
    assert(alloc_page() == NULL);
    for (i = 0; i <= BUDDY_MAX_ORDER; i ++) {
        buddy_free_area[i] = bucket_store[i];
    }
    buddy_nr_free = nr_free_temp;

    // 7. fragment accounting: the pages counted from every bucket and the
    //    free page counter must agree after the rounds above
    total = 0;
    for (i = 0; i <= BUDDY_MAX_ORDER; i ++) {
        list_entry_t *le = &(buddy_free_area[i].free_list);
        while ((le = list_next(le)) != &(buddy_free_area[i].free_list)) {
            p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == ((size_t)1 << i));
            total += p->property;
        }
    }
    assert(total == nr_free_pages());
    assert(total == nr_free_store);
    assert(buddy_nr_free == nr_free_pages());
    buddy_check_state();

    // 8. mixed sizes allocated together and freed out of order, simulating
    //    a random alloc/free workload
    size = 0;
    for (i = 0; i < 5; i ++) {
        assert((blocks[i] = alloc_pages(request[i])) != NULL);
        assert(!PageProperty(blocks[i]));
        assert(((size_t)(blocks[i] - pages) & (actual[i] - 1)) == 0);
        size += actual[i];
    }
    // the returned blocks must be pairwise disjoint
    for (i = 0; i < 5; i ++) {
        for (k = i + 1; k < 5; k ++) {
            assert(blocks[i] + actual[i] <= blocks[k] ||
                   blocks[k] + actual[k] <= blocks[i]);
        }
    }
    assert(buddy_nr_free == nr_free_store - size);

    free_pages(blocks[2], request[2]);
    free_pages(blocks[0], request[0]);
    free_pages(blocks[4], request[4]);
    free_pages(blocks[1], request[1]);
    free_pages(blocks[3], request[3]);
    assert(buddy_nr_free == nr_free_store);
    buddy_check_state();
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
