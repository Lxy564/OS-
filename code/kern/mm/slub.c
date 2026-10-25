#include <slub.h>
#include <pmm.h>
#include <list.h>

/*
 * Lab2 Challenge 2411846.
 * Layer 1: existing alloc_pages/free_pages (Best-Fit or Buddy).
 * Layer 2: fixed-size objects in slab pages; large objects use page runs.
 * No per-CPU caches, constructors, concurrency or Linux ABI compatibility.
 */
#define NCLASS 8
#define USED_BYTES 32
#define SLAB_MAGIC 0x534c55424c414232UL
static const size_t class_sizes[NCLASS] = {16,32,64,128,256,512,1024,2048};
struct slab {
    uint64_t magic;
    struct Page *page;
    list_entry_t link;
    void *free_head;
    size_t stride, capacity, inuse, requested_pages, actual_pages;
    unsigned class_index;
    unsigned char used[USED_BYTES];
};
static list_entry_t caches[NCLASS], large_objects;
static size_t live_count, page_count;
static int initialized;

static size_t data_offset(void) {
    return ROUNDUP(sizeof(struct slab), SLUB_ALIGNMENT);
}
static int bit_get(struct slab *s, size_t i) {
    return (s->used[i / 8] >> (i % 8)) & 1;
}
static void bit_set(struct slab *s, size_t i, int value) {
    if (value) s->used[i / 8] |= 1U << (i % 8);
    else s->used[i / 8] &= ~(1U << (i % 8));
}
void slub_init(void) {
    if (initialized) return;
    for (unsigned i=0;i<NCLASS;i++) list_init(&caches[i]);
    list_init(&large_objects);
    live_count=page_count=0;
    initialized=1;
}
static struct slab *new_slab(unsigned ci, size_t n) {
    size_t offset=data_offset();
    size_t npages=ci < NCLASS ? 1 : (offset+n+PGSIZE-1)/PGSIZE;
    size_t before=nr_free_pages();
    struct Page *p=alloc_pages(npages);
    if (!p) return NULL;
    /* PADDR cannot be used on a Page descriptor to get its described page. */
    struct slab *s=(struct slab *)(page2pa(p)+va_pa_offset);
    s->magic=SLAB_MAGIC;
    s->page=p;
    s->class_index=ci;
    s->requested_pages=npages;
    s->actual_pages=before-nr_free_pages();
    s->stride=ci < NCLASS ? class_sizes[ci] : n;
    s->capacity=ci < NCLASS ? (PGSIZE-offset)/s->stride : 1;
    assert(s->capacity>0 && s->capacity<=USED_BYTES*8);
    s->inuse=0;
    s->free_head=NULL;
    for (size_t i=0;i<USED_BYTES;i++) s->used[i]=0;
    if (ci<NCLASS) {
        for (size_t i=s->capacity;i>0;i--) {
            void *obj=(char *)s+offset+(i-1)*s->stride;
            *(void **)obj=s->free_head;
            s->free_head=obj;
        }
        list_add(&caches[ci],&s->link);
    } else list_add(&large_objects,&s->link);
    page_count+=s->actual_pages;
    return s;
}
void *slub_alloc(size_t n) {
    if (!initialized || n==0 || n>SLUB_MAX_ALLOC) return NULL;
    unsigned ci=0;
    while (ci<NCLASS && class_sizes[ci]<n) ci++;
    struct slab *s=NULL;
    if (ci<NCLASS) {
        list_entry_t *le=&caches[ci];
        while ((le=list_next(le))!=&caches[ci]) {
            struct slab *candidate=to_struct(le,struct slab,link);
            if (candidate->free_head) { s=candidate; break; }
        }
    }
    if (!s) s=new_slab(ci,n);
    if (!s) return NULL;
    void *obj;
    size_t ix=0;
    if (ci<NCLASS) {
        obj=s->free_head;
        s->free_head=*(void **)obj;
        ix=((uintptr_t)obj-(uintptr_t)s-data_offset())/s->stride;
    } else obj=(char *)s+data_offset();
    assert(!bit_get(s,ix));
    bit_set(s,ix,1);
    s->inuse++;
    live_count++;
    return obj;
}
/* Locate through our registered lists, not through an unchecked pointer. */
static struct slab *find_owner(const void *ptr) {
    if (!initialized || !ptr) return NULL;
    uintptr_t base=ROUNDDOWN((uintptr_t)ptr,PGSIZE);
    for (unsigned ci=0;ci<=NCLASS;ci++) {
        list_entry_t *head=ci<NCLASS ? &caches[ci] : &large_objects;
        list_entry_t *le=head;
        while ((le=list_next(le))!=head) {
            struct slab *s=to_struct(le,struct slab,link);
            if ((uintptr_t)s==base) return s;
        }
    }
    return NULL;
}
static int object_index(struct slab *s,const void *ptr,size_t *ix) {
    uintptr_t begin=(uintptr_t)s+data_offset(), addr=(uintptr_t)ptr;
    if (s->magic!=SLAB_MAGIC || addr<begin) return -1;
    size_t delta=addr-begin;
    if (delta%s->stride || delta/s->stride>=s->capacity) return -1;
    *ix=delta/s->stride;
    if (!bit_get(s,*ix)) return -1;
    return 0;
}
size_t slub_usable_size(const void *ptr) {
    struct slab *s=find_owner(ptr);size_t ix;
    if (!s || object_index(s,ptr,&ix)) return 0;
    return s->stride;
}
int slub_free(void *ptr) {
    if (!ptr) return 0;
    struct slab *s=find_owner(ptr);size_t ix;
    if (!s || object_index(s,ptr,&ix)) return -1;
    bit_set(s,ix,0);
    s->inuse--;
    live_count--;
    if (s->class_index<NCLASS) {
        *(void **)ptr=s->free_head;
        s->free_head=ptr;
    }
    if (s->inuse==0) {
        struct Page *page=s->page;
        size_t requested=s->requested_pages,actual=s->actual_pages;
        list_del(&s->link);
        s->magic=0;
        page_count-=actual;
        /* Pass the ORIGINAL request, allowing Buddy to reclaim its rounded run. */
        free_pages(page,requested);
    }
    return 0;
}
size_t slub_live_objects(void) { return live_count; }
size_t slub_backing_pages(void) { return page_count; }
