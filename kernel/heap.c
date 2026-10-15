/*
 * Kernel heap: an address-ordered free list with first-fit allocation,
 * block splitting and coalescing on free. Plenty for a single address
 * space where the biggest users are a few screen-sized pixel buffers.
 */

#include "kernel.h"

#define HDR_MAGIC 0x4B504C52u /* "KPLR" */
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

typedef struct block {
    uint32_t magic;
    uint32_t size;      /* payload bytes */
    struct block *next; /* free list only */
    uint32_t free;
} block_t;

static block_t *free_list;
static size_t total_bytes, used_bytes;

void heap_init(uintptr_t start, uintptr_t end)
{
    start = ALIGN_UP(start, 16);
    free_list = (block_t *)start;
    free_list->magic = HDR_MAGIC;
    free_list->size = (uint32_t)(end - start - sizeof(block_t));
    free_list->next = NULL;
    free_list->free = 1;
    total_bytes = free_list->size;
    used_bytes = 0;
}

void *kmalloc(size_t n)
{
    if (n == 0)
        n = 1;
    n = ALIGN_UP(n, 16);
    uint32_t fl = irq_save();
    block_t **link = &free_list;
    for (block_t *b = free_list; b; link = &b->next, b = b->next) {
        if (b->size < n)
            continue;
        if (b->size >= n + sizeof(block_t) + 64) {
            block_t *rest = (block_t *)((uint8_t *)(b + 1) + n);
            rest->magic = HDR_MAGIC;
            rest->size = b->size - n - sizeof(block_t);
            rest->next = b->next;
            rest->free = 1;
            b->size = n;
            *link = rest;
        } else {
            *link = b->next;
        }
        b->free = 0;
        b->next = NULL;
        used_bytes += b->size + sizeof(block_t);
        irq_restore(fl);
        return b + 1;
    }
    irq_restore(fl);
    kprintf("kmalloc: out of memory (%u bytes)\n", (unsigned)n);
    return NULL;
}

void *kzalloc(size_t n)
{
    void *p = kmalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

void kfree(void *p)
{
    if (!p)
        return;
    block_t *b = (block_t *)p - 1;
    if (b->magic != HDR_MAGIC || b->free)
        panic("kfree: bad pointer %p", p);
    uint32_t fl = irq_save();
    used_bytes -= b->size + sizeof(block_t);
    b->free = 1;

    /* insert in address order */
    block_t *prev = NULL, *cur = free_list;
    while (cur && cur < b) {
        prev = cur;
        cur = cur->next;
    }
    b->next = cur;
    if (prev)
        prev->next = b;
    else
        free_list = b;

    /* merge with the following block */
    if (cur && (uint8_t *)(b + 1) + b->size == (uint8_t *)cur) {
        b->size += sizeof(block_t) + cur->size;
        b->next = cur->next;
    }
    /* merge with the preceding block */
    if (prev && (uint8_t *)(prev + 1) + prev->size == (uint8_t *)b) {
        prev->size += sizeof(block_t) + b->size;
        prev->next = b->next;
    }
    irq_restore(fl);
}

void *krealloc(void *p, size_t n)
{
    if (!p)
        return kmalloc(n);
    block_t *b = (block_t *)p - 1;
    if (b->size >= n)
        return p;
    void *q = kmalloc(n);
    if (!q)
        return NULL;
    memcpy(q, p, b->size);
    kfree(p);
    return q;
}

size_t heap_total(void) { return total_bytes; }
size_t heap_used(void) { return used_bytes; }
