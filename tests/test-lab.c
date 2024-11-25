#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "harness/unity.h"
#include "../src/lab.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

// Helper functions
void check_buddy_pool_full(struct buddy_pool *pool)
{
    //A full pool should have all values 0-(kval-1) as empty
    for (size_t i = 0; i < pool->kval_m; i++)
    {
        assert(pool->avail[i].next == &pool->avail[i]);
        assert(pool->avail[i].prev == &pool->avail[i]);
        assert(pool->avail[i].tag == BLOCK_UNUSED);
        assert(pool->avail[i].kval == i);
    }

    //The avail array at kval should have the base block
    assert(pool->avail[pool->kval_m].next->tag == BLOCK_AVAIL);
    assert(pool->avail[pool->kval_m].next->next == &pool->avail[pool->kval_m]);
    assert(pool->avail[pool->kval_m].prev->prev == &pool->avail[pool->kval_m]);

    //Check to make sure the base address points to the starting pool
    assert(pool->avail[pool->kval_m].next == pool->base);
}

void check_buddy_pool_empty(struct buddy_pool *pool)
{
    //An empty pool should have all values 0-(kval) as empty
    for (size_t i = 0; i <= pool->kval_m; i++)
    {
        assert(pool->avail[i].next == &pool->avail[i]);
        assert(pool->avail[i].prev == &pool->avail[i]);
        assert(pool->avail[i].tag == BLOCK_UNUSED);
        assert(pool->avail[i].kval == i);
    }
}

// Original test functions
void test_buddy_init(void)
{
    fprintf(stderr, "->Testing buddy init\n");
    //Loop through all kval MIN_k-DEFAULT_K and make sure we get the correct amount allocated.
    //Will check all the pointer offsets to ensure the pool is all configured correctly
    for (size_t i = MIN_K; i <= DEFAULT_K; i++)
    {
        size_t size = UINT64_C(1) << i;
        struct buddy_pool pool;
        buddy_init(&pool, size);
        check_buddy_pool_full(&pool);
        buddy_destroy(&pool);
    }
}

void test_buddy_malloc_one_byte(void)
{
    fprintf(stderr, "->Test allocating and freeing 1 byte\n");
    struct buddy_pool pool;
    int kval = MIN_K;
    size_t size = UINT64_C(1) << kval;
    buddy_init(&pool, size);
    void *mem = buddy_malloc(&pool, 1);
    //Make sure correct kval was allocated
    buddy_free(&pool, mem);
    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

void test_buddy_malloc_one_large(void)
{
    fprintf(stderr, "->Testing size that will consume entire memory pool\n");
    struct buddy_pool pool;
    size_t bytes = UINT64_C(1) << MIN_K;
    buddy_init(&pool, bytes);

    //Ask for an exact K value to be allocated. This test makes assumptions on
    //the internal details of buddy_init.
    size_t ask = bytes - sizeof(struct avail);
    void *mem = buddy_malloc(&pool, ask);
    TEST_ASSERT_NOT_NULL(mem);

    //Move the pointer back and make sure get what is expected
    struct avail *tmp = (struct avail *)mem - 1;
    TEST_ASSERT_EQUAL(MIN_K, tmp->kval);
    TEST_ASSERT_EQUAL(BLOCK_RESERVED, tmp->tag);
    check_buddy_pool_empty(&pool);

    //Verify that a call on an empty tool fails as expected and errno is set to ENOMEM.
    void *fail = buddy_malloc(&pool, 5);
    TEST_ASSERT_NULL(fail);
    TEST_ASSERT_EQUAL(ENOMEM, errno);

    //Free the memory and then check to make sure everything is OK
    buddy_free(&pool, mem);
    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}

// New test functions
void test_btok_edge_cases(void) {
    fprintf(stderr, "->Testing btok edge cases\n");
    
    // Test minimum size
    TEST_ASSERT_EQUAL(SMALLEST_K, btok(1));
    
    // Test size that exactly matches a power of 2
    TEST_ASSERT_EQUAL(10, btok(1024));
    
    // Test size slightly above a power of 2
    TEST_ASSERT_EQUAL(11, btok(1025));
    
    // Test maximum allowed size
    size_t max_size = UINT64_C(1) << (MAX_K - 1);
    TEST_ASSERT_EQUAL(MAX_K - 1, btok(max_size));
}

void test_buddy_calc_edge_cases(void) {
    fprintf(stderr, "->Testing buddy_calc edge cases\n");
    struct buddy_pool pool;
    size_t size = UINT64_C(1) << MIN_K;  // Use minimum size to keep things simple
    buddy_init(&pool, size);
    
    // Test NULL pool
    TEST_ASSERT_NULL(buddy_calc(NULL, pool.base));
    
    // Test NULL block
    TEST_ASSERT_NULL(buddy_calc(&pool, NULL));
    
    // Test invalid block (before base)
    struct avail invalid_block;
    TEST_ASSERT_NULL(buddy_calc(&pool, &invalid_block));
    
    // Test valid buddy calculation
    // First, allocate a smaller block to force splitting
    void *mem = buddy_malloc(&pool, size/4);  // Request 1/4 of pool size
    TEST_ASSERT_NOT_NULL(mem);
    
    // Get the block header
    struct avail *block = ((struct avail *)mem) - 1;
    TEST_ASSERT_NOT_NULL(block);
    
    // Calculate buddy
    struct avail *buddy = buddy_calc(&pool, block);
    TEST_ASSERT_NOT_NULL(buddy);
    
    // Verify buddy properties
    TEST_ASSERT_EQUAL(block->kval, buddy->kval);
    
    // The buddy address should differ from the block address by exactly 2^kval
    uintptr_t block_addr = (uintptr_t)block;
    uintptr_t buddy_addr = (uintptr_t)buddy;
    uintptr_t expected_diff = UINT64_C(1) << block->kval;
    TEST_ASSERT_EQUAL(expected_diff, (block_addr > buddy_addr) ? 
                     (block_addr - buddy_addr) : (buddy_addr - block_addr));
    
    buddy_free(&pool, mem);
    buddy_destroy(&pool);
}

void test_buddy_malloc_edge_cases(void) {
    fprintf(stderr, "->Testing malloc edge cases\n");
    struct buddy_pool pool;
    buddy_init(&pool, 0);
    
    // Test NULL pool
    TEST_ASSERT_NULL(buddy_malloc(NULL, 10));
    
    // Test zero size
    TEST_ASSERT_NULL(buddy_malloc(&pool, 0));
    
    // Test malloc with size > pool size
    size_t huge_size = UINT64_C(1) << (DEFAULT_K + 1);
    TEST_ASSERT_NULL(buddy_malloc(&pool, huge_size));
    TEST_ASSERT_EQUAL(ENOMEM, errno);
    
    buddy_destroy(&pool);
}

void test_buddy_realloc(void) {
    fprintf(stderr, "->Testing realloc functionality\n");
    struct buddy_pool pool;
    buddy_init(&pool, 0);
    
    // Test realloc of NULL pointer (should act like malloc)
    void *ptr = buddy_realloc(&pool, NULL, 100);
    TEST_ASSERT_NOT_NULL(ptr);
    
    // Test realloc to larger size
    void *new_ptr = buddy_realloc(&pool, ptr, 200);
    TEST_ASSERT_NOT_NULL(new_ptr);
    
    // Test realloc to smaller size
    void *smaller_ptr = buddy_realloc(&pool, new_ptr, 50);
    TEST_ASSERT_NOT_NULL(smaller_ptr);
    
    // Test realloc to zero (should free)
    void *zero_ptr = buddy_realloc(&pool, smaller_ptr, 0);
    TEST_ASSERT_NULL(zero_ptr);
    
    buddy_destroy(&pool);
}

void test_multiple_allocations(void) {
    fprintf(stderr, "->Testing multiple allocations and frees\n");
    struct buddy_pool pool;
    buddy_init(&pool, 0);
    
    // Allocate multiple blocks of different sizes
    void *ptrs[5];
    size_t sizes[] = {100, 200, 300, 400, 500};
    
    for (int i = 0; i < 5; i++) {
        ptrs[i] = buddy_malloc(&pool, sizes[i]);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }
    
    // Free blocks in random order
    buddy_free(&pool, ptrs[2]);
    buddy_free(&pool, ptrs[0]);
    buddy_free(&pool, ptrs[4]);
    buddy_free(&pool, ptrs[1]);
    buddy_free(&pool, ptrs[3]);
    
    // Verify pool is back to original state
    check_buddy_pool_full(&pool);
    
    buddy_destroy(&pool);
}

void test_buddy_coalescing(void) {
    fprintf(stderr, "->Testing buddy block coalescing\n");
    struct buddy_pool pool;
    buddy_init(&pool, UINT64_C(1) << MIN_K);  // Use minimum size
    
    // Allocate a block that will require splitting
    void *ptr1 = buddy_malloc(&pool, 100);
    TEST_ASSERT_NOT_NULL(ptr1);
    
    // Free it and verify coalescing
    buddy_free(&pool, ptr1);
    check_buddy_pool_full(&pool);
    
    buddy_destroy(&pool);
}

void test_memory_content(void) {
    fprintf(stderr, "->Testing memory content\n");
    struct buddy_pool pool;
    buddy_init(&pool, 0);
    
    // Allocate and fill memory
    size_t test_size = 128;
    char *mem = (char *)buddy_malloc(&pool, test_size);
    TEST_ASSERT_NOT_NULL(mem);
    
    // Fill with pattern
    for (size_t i = 0; i < test_size; i++) {
        mem[i] = (char)(i & 0xFF);
    }
    
    // Verify pattern
    for (size_t i = 0; i < test_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), mem[i]);
    }
    
    // Reallocate to larger size
    size_t new_size = test_size * 2;
    char *new_mem = (char *)buddy_realloc(&pool, mem, new_size);
    TEST_ASSERT_NOT_NULL(new_mem);
    
    // Verify original content is preserved
    for (size_t i = 0; i < test_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), new_mem[i]);
    }
    
    // Fill remaining space
    for (size_t i = test_size; i < new_size; i++) {
        new_mem[i] = (char)(i & 0xFF);
    }
    
    // Verify entire buffer
    for (size_t i = 0; i < new_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), new_mem[i]);
    }
    
    buddy_free(&pool, new_mem);
    buddy_destroy(&pool);
}

void test_init_edge_cases(void) {
    fprintf(stderr, "->Testing init edge cases\n");
    
    // Test NULL pool
    buddy_init(NULL, 1024);
    
    // Test with very small size (should still work)
    struct buddy_pool pool;
    buddy_init(&pool, 1);
    buddy_destroy(&pool);
}

void test_destroy_edge_cases(void) {
    fprintf(stderr, "->Testing destroy edge cases\n");
    
    // Test NULL pool
    buddy_destroy(NULL);
    
    // Test NULL base
    struct buddy_pool pool = {0};  // Initialize all to 0
    buddy_destroy(&pool);
}

void test_free_edge_cases(void) {
    fprintf(stderr, "->Testing free edge cases\n");
    
    // Test NULL pool
    buddy_free(NULL, (void*)1);
    
    // Test NULL ptr
    struct buddy_pool pool;
    buddy_init(&pool, 0);
    buddy_free(&pool, NULL);
    buddy_destroy(&pool);
}

void test_realloc_edge_cases(void) {
    fprintf(stderr, "->Testing realloc edge cases\n");
    
    // Test NULL pool
    void* ptr = buddy_realloc(NULL, NULL, 100);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL(ENOMEM, errno);
    
    // Test malloc failure case (requesting too much memory)
    struct buddy_pool pool;
    buddy_init(&pool, UINT64_C(1) << MIN_K);  // Initialize with minimum size
    
    // First allocate a small block
    void* small = buddy_malloc(&pool, 100);
    TEST_ASSERT_NOT_NULL(small);
    
    // Try to realloc to a size larger than the pool
    ptr = buddy_realloc(&pool, small, (UINT64_C(1) << MIN_K) * 2);
    TEST_ASSERT_NULL(ptr);
    
    buddy_free(&pool, small);
    buddy_destroy(&pool);
}

void test_mmap_failure(void) {
    fprintf(stderr, "->Testing mmap failure handling\n");
    
    struct buddy_pool pool;
    // Try to allocate an extremely large size
    size_t huge_size = (size_t)-1;  // Maximum possible size_t value
    
    buddy_init(&pool, huge_size);
    // Verify that the pool's base is NULL after failed initialization
    TEST_ASSERT_NULL(pool.base);
}

void test_realloc_content(void) {
    fprintf(stderr, "->Testing detailed reallocation scenarios\n");
    struct buddy_pool pool;
    buddy_init(&pool, 0);  // Use default size
    
    // Test Case 1: Allocate initial block and fill with pattern
    size_t initial_size = 100;
    char *ptr = (char *)buddy_malloc(&pool, initial_size);
    TEST_ASSERT_NOT_NULL(ptr);
    for (size_t i = 0; i < initial_size; i++) {
        ptr[i] = (char)(i & 0xFF);
    }
    
    // Test Case 2: Reallocate to larger size
    size_t larger_size = 200;
    char *larger_ptr = (char *)buddy_realloc(&pool, ptr, larger_size);
    TEST_ASSERT_NOT_NULL(larger_ptr);
    
    // Verify original content is preserved
    for (size_t i = 0; i < initial_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), larger_ptr[i]);
    }
    
    // Fill new space
    for (size_t i = initial_size; i < larger_size; i++) {
        larger_ptr[i] = (char)(i & 0xFF);
    }
    
    // Test Case 3: Reallocate to smaller size
    size_t smaller_size = 50;
    char *smaller_ptr = (char *)buddy_realloc(&pool, larger_ptr, smaller_size);
    TEST_ASSERT_NOT_NULL(smaller_ptr);
    
    // Verify content is preserved up to smaller size
    for (size_t i = 0; i < smaller_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), smaller_ptr[i]);
    }
    
    // Test Case 4: Reallocate to same size (should return same pointer)
    char *same_ptr = (char *)buddy_realloc(&pool, smaller_ptr, smaller_size);
    TEST_ASSERT_EQUAL(same_ptr, smaller_ptr);
    
    // Verify content is still intact
    for (size_t i = 0; i < smaller_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), same_ptr[i]);
    }
    
    // Test Case 5: Reallocate to size that fits in current block
    size_t fit_size = smaller_size - 10;
    char *fit_ptr = (char *)buddy_realloc(&pool, same_ptr, fit_size);
    TEST_ASSERT_EQUAL(fit_ptr, same_ptr);
    
    // Verify content is still intact
    for (size_t i = 0; i < fit_size; i++) {
        TEST_ASSERT_EQUAL((char)(i & 0xFF), fit_ptr[i]);
    }
    
    // Cleanup
    buddy_free(&pool, fit_ptr);
    buddy_destroy(&pool);
}

int main(void) {
    time_t t;
    unsigned seed = (unsigned)time(&t);
    fprintf(stderr, "Random seed:%d\n", seed);
    srand(seed);
    printf("Running memory tests.\n");

    UNITY_BEGIN();
    
    // Original tests
    RUN_TEST(test_buddy_init);
    RUN_TEST(test_buddy_malloc_one_byte);
    RUN_TEST(test_buddy_malloc_one_large);
    
    // New tests
    RUN_TEST(test_btok_edge_cases);
    RUN_TEST(test_buddy_calc_edge_cases);
    RUN_TEST(test_buddy_malloc_edge_cases);
    RUN_TEST(test_buddy_realloc);
    RUN_TEST(test_multiple_allocations);
    RUN_TEST(test_buddy_coalescing);
    RUN_TEST(test_memory_content);
    RUN_TEST(test_init_edge_cases);
    RUN_TEST(test_destroy_edge_cases);
    RUN_TEST(test_free_edge_cases);
    RUN_TEST(test_realloc_edge_cases);
    RUN_TEST(test_mmap_failure);
    RUN_TEST(test_realloc_content);
    
    return UNITY_END();
}