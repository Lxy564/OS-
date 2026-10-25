#ifndef LAB2_SLUB_H
#define LAB2_SLUB_H
#include <defs.h>
#define SLUB_ALIGNMENT 16
#define SLUB_MAX_ALLOC (64UL * 1024)
/* Lab2 Challenge 2411846: single-core teaching SLUB-style object layer. */
void slub_init(void);
void *slub_alloc(size_t n);
/* NULL is a no-op. Invalid or already-freed pointers return -1. */
int slub_free(void *ptr);
size_t slub_usable_size(const void *ptr);
size_t slub_live_objects(void);
size_t slub_backing_pages(void);
void slub_check(void);
#endif
