#include <slub.h>
#include <pmm.h>
#include <stdio.h>

/* Lab2 Challenge 2411846: independent externally visible behavior tests. */
static void *objects[600];
static size_t requests[600];
static unsigned char pattern[600];
static const size_t sizes[] = {
    1,15,16,17,31,32,33,63,64,65,127,128,129,255,256,257,
    511,512,513,1023,1024,1025,2047,2048,2049,4095,4096,4097,
    8192,65535,65536
};
static size_t expected_size(size_t n) {
    if (n>2048) return n;
    size_t x=16;while(x<n)x*=2;return x;
}
static void fill(void *p,size_t n,unsigned char v) {
    unsigned char *q=p;for(size_t i=0;i<n;i++)q[i]=v;
}
static void verify(void *p,size_t n,unsigned char v) {
    unsigned char *q=p;for(size_t i=0;i<n;i++)assert(q[i]==v);
}
static void baseline(size_t free_before) {
    assert(nr_free_pages()==free_before);
    assert(slub_live_objects()==0 && slub_backing_pages()==0);
}
void slub_check(void) {
    size_t before=nr_free_pages();
    assert(slub_live_objects()==0 && slub_backing_pages()==0);
    assert(slub_alloc(0)==NULL);
    assert(slub_alloc(SLUB_MAX_ALLOC+1)==NULL);
    assert(slub_alloc((size_t)-1)==NULL);
    assert(slub_free(NULL)==0);
    baseline(before);
    cprintf("[SLUB 1] zero/oversized/overflow requests: PASS\n");

    size_t count=sizeof(sizes)/sizeof(sizes[0]);
    for(size_t i=0;i<count;i++) {
        objects[i]=slub_alloc(sizes[i]);assert(objects[i]);
        assert(((uintptr_t)objects[i]&(SLUB_ALIGNMENT-1))==0);
        assert(slub_usable_size(objects[i])==expected_size(sizes[i]));
        fill(objects[i],sizes[i],(unsigned char)(i+1));
        for(size_t j=0;j<i;j++) {
            uintptr_t a=(uintptr_t)objects[i],b=(uintptr_t)objects[j];
            assert(a+sizes[i]<=b || b+sizes[j]<=a);
        }
    }
    for(size_t i=0;i<count;i++)verify(objects[i],sizes[i],(unsigned char)(i+1));
    for(size_t i=count;i>0;i--)assert(slub_free(objects[i-1])==0);
    baseline(before);
    cprintf("[SLUB 2] classes/boundaries/alignment/data isolation: PASS\n");

    void *a=slub_alloc(64),*b=slub_alloc(64);assert(a&&b);
    assert(ROUNDDOWN((uintptr_t)a,PGSIZE)==ROUNDDOWN((uintptr_t)b,PGSIZE));
    size_t used=nr_free_pages();
    assert(slub_free(a)==0 && nr_free_pages()==used);
    assert(slub_free(a)==-1); /* same slab still registered */
    void *c=slub_alloc(64);assert(c==a);
    assert(slub_free((char *)b+1)==-1);
    assert(slub_free((void *)(uintptr_t)0x123)==-1);
    assert(slub_free(c)==0 && slub_free(b)==0);
    assert(slub_free(b)==-1); /* empty slab has already been returned */
    baseline(before);
    cprintf("[SLUB 3] shared slab/reuse/invalid free/empty reclaim: PASS\n");

    for(size_t i=0;i<600;i++) {
        objects[i]=slub_alloc(16);assert(objects[i]);
        fill(objects[i],16,(unsigned char)(i%251));
        for(size_t j=0;j<i;j++)assert(objects[i]!=objects[j]);
    }
    assert(slub_backing_pages()>=3);
    for(size_t i=0;i<600;i++)verify(objects[i],16,(unsigned char)(i%251));
    for(size_t i=0;i<600;i+=2)assert(slub_free(objects[i])==0);
    for(size_t i=1;i<600;i+=2)assert(slub_free(objects[i])==0);
    baseline(before);
    cprintf("[SLUB 4] multi-slab/nonsequential free/page recovery: PASS\n");

    for(unsigned round=0;round<50;round++) {
        for(size_t i=0;i<40;i++) {
            size_t n=sizes[(round*7+i*11)%count];
            objects[i]=slub_alloc(n);requests[i]=n;
            pattern[i]=(unsigned char)(round+i+3);assert(objects[i]);
            fill(objects[i],n,pattern[i]);
        }
        for(size_t i=0;i<40;i++)verify(objects[i],requests[i],pattern[i]);
        for(size_t i=0;i<40;i++) {
            size_t j=(i*17)%40;assert(slub_free(objects[j])==0);
        }
        baseline(before);
    }
    cprintf("[SLUB 5] mixed sizes/repeated cycles/no page leak: PASS\n");
    /* Exhaust backing RAM without a large kernel-stack or heap allocation. */
    struct Page *held[64];
    size_t held_n[64], held_count=0;
    a=slub_alloc(16);assert(a);
    for(size_t n=16384;n>0;n>>=1) {
        struct Page *q;
        while((q=alloc_pages(n))!=NULL) {
            assert(held_count<64);
            held[held_count]=q;held_n[held_count++]=n;
        }
    }
    assert(nr_free_pages()==0);
    b=slub_alloc(16);assert(b); /* reuse needs no additional backing page */
    assert(slub_alloc(2048)==NULL && slub_alloc(4097)==NULL);
    assert(slub_live_objects()==2 && slub_backing_pages()==1);
    assert(slub_free(b)==0 && slub_free(a)==0);
    while(held_count) {
        held_count--;
        free_pages(held[held_count],held_n[held_count]);
    }
    baseline(before);
    cprintf("[SLUB 6] backing OOM/cache reuse/failure state recovery: PASS\n");
    cprintf("slub_check() succeeded! free pages restored: %lu\n",before);
}
