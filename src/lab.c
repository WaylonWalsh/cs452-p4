#include "lab.h"
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <bits/mman-linux.h>

// Debug macro - uncomment to enable debug prints
// #define DEBUG_PRINT(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
// #define DEBUG_PRINT(fmt, ...) /* disabled */

size_t btok(size_t bytes) {
    size_t k = SMALLEST_K;
    size_t size = UINT64_C(1) << k;
    
    // Find the smallest k where 2^k >= bytes
    while (size < bytes && k < MAX_K) {
        k++;
        size = UINT64_C(1) << k;
    }
    
    return k;
}

struct avail *buddy_calc(struct buddy_pool *pool, struct avail *block) {
    if (!pool || !block || block < (struct avail *)pool->base) {
        return NULL;
    }

    // Calculate offset from base
    uintptr_t offset = (uintptr_t)block - (uintptr_t)pool->base;
    uintptr_t buddy_offset = offset ^ (UINT64_C(1) << block->kval);
    
    // Calculate buddy address
    struct avail *buddy = (struct avail *)((uintptr_t)pool->base + buddy_offset);
    
    // Validate buddy address
    if ((uintptr_t)buddy < (uintptr_t)pool->base || 
        (uintptr_t)buddy >= (uintptr_t)pool->base + pool->numbytes) {
        return NULL;
    }
    
    return buddy;
}

void buddy_init(struct buddy_pool *pool, size_t size) {
    if (!pool) return;

    // If size is 0, use DEFAULT_K
    if (size == 0) {
        size = UINT64_C(1) << DEFAULT_K;
    }

    // Calculate required kval
    size_t kval = btok(size);
    size_t actual_size = UINT64_C(1) << kval;

    // Map memory
    void *mem = mmap(NULL, actual_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        errno = ENOMEM;
        return;
    }

    // Initialize pool
    pool->kval_m = kval;
    pool->numbytes = actual_size;
    pool->base = mem;

    // Initialize avail array
    for (size_t i = 0; i <= MAX_K - 1; i++) {
        pool->avail[i].tag = BLOCK_UNUSED;
        pool->avail[i].kval = i;
        pool->avail[i].next = &pool->avail[i];
        pool->avail[i].prev = &pool->avail[i];
    }

    // Initialize base block
    struct avail *base = (struct avail *)mem;
    base->tag = BLOCK_AVAIL;
    base->kval = kval;
    base->next = &pool->avail[kval];
    base->prev = &pool->avail[kval];
    
    // Link base block into avail array
    pool->avail[kval].next = base;
    pool->avail[kval].prev = base;
}

void buddy_destroy(struct buddy_pool *pool) {
    if (!pool || !pool->base) return;
    munmap(pool->base, pool->numbytes);
    pool->base = NULL;
}

void *buddy_malloc(struct buddy_pool *pool, size_t size) {
    if (!pool || !pool->base || size == 0) {
        errno = ENOMEM;
        return NULL;
    }

    // Calculate required block size including header
    size_t total_size = size + sizeof(struct avail);
    size_t k = btok(total_size);
    
    // DEBUG_PRINT("Malloc request: %zu bytes (k=%zu)\n", size, k);

    // Find smallest available block that fits
    size_t current_k = k;
    struct avail *block = NULL;
    
    while (current_k <= pool->kval_m) {
        if (pool->avail[current_k].next != &pool->avail[current_k]) {
            block = pool->avail[current_k].next;
            break;
        }
        current_k++;
    }

    if (!block) {
        errno = ENOMEM;
        return NULL;
    }

    // Remove block from avail list
    block->prev->next = block->next;
    block->next->prev = block->prev;

    // Split block if necessary
    while (current_k > k) {
        current_k--;
        
        // Create buddy block
        struct avail *buddy = (struct avail *)((char *)block + (UINT64_C(1) << current_k));
        buddy->tag = BLOCK_AVAIL;
        buddy->kval = current_k;
        
        // Add buddy to avail list
        buddy->next = pool->avail[current_k].next;
        buddy->prev = &pool->avail[current_k];
        pool->avail[current_k].next->prev = buddy;
        pool->avail[current_k].next = buddy;
        
        // Update original block
        block->kval = current_k;
    }

    // Mark block as reserved
    block->tag = BLOCK_RESERVED;
    
    // DEBUG_PRINT("Allocated block at %p (k=%u)\n", block, block->kval);
    
    return (void *)(block + 1);
}

void buddy_free(struct buddy_pool *pool, void *ptr) {
    if (!pool || !ptr) return;

    // Get block header
    struct avail *block = ((struct avail *)ptr) - 1;
    
    // DEBUG_PRINT("Freeing block at %p (k=%u)\n", block, block->kval);

    // Mark block as available
    block->tag = BLOCK_AVAIL;

    // Coalesce with buddy if possible
    while (block->kval < pool->kval_m) {
        struct avail *buddy = buddy_calc(pool, block);
        
        // Check if buddy is available for merging
        if (!buddy || buddy->tag != BLOCK_AVAIL || buddy->kval != block->kval) {
            break;
        }

        // Remove buddy from its avail list
        buddy->prev->next = buddy->next;
        buddy->next->prev = buddy->prev;

        // Choose the lower address as the new block
        if (buddy < block) {
            block = buddy;
        }

        // Update block size
        block->kval++;
    }

    // Add block to appropriate avail list
    block->next = pool->avail[block->kval].next;
    block->prev = &pool->avail[block->kval];
    pool->avail[block->kval].next->prev = block;
    pool->avail[block->kval].next = block;
}

void *buddy_realloc(struct buddy_pool *pool, void *ptr, size_t size) {
    if (!pool) {
        errno = ENOMEM;
        return NULL;
    }

    // Handle special cases
    if (!ptr) return buddy_malloc(pool, size);
    if (size == 0) {
        buddy_free(pool, ptr);
        return NULL;
    }

    // Get current block information
    struct avail *old_block = ((struct avail *)ptr) - 1;
    size_t old_size = (UINT64_C(1) << old_block->kval) - sizeof(struct avail);

    // If new size fits in current block, just return the same pointer
    size_t new_k = btok(size + sizeof(struct avail));
    if (new_k <= old_block->kval) {
        return ptr;
    }

    // Allocate new block
    void *new_ptr = buddy_malloc(pool, size);
    if (!new_ptr) {
        return NULL;
    }

    // Copy data
    memcpy(new_ptr, ptr, old_size);
    
    // Free old block
    buddy_free(pool, ptr);

    return new_ptr;
}