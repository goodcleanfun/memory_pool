#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "greatest/greatest.h"

typedef struct bintree_node {
    uint32_t value;
    struct bintree_node *left;
    struct bintree_node *right;
} bintree_node_t;

#define MEMORY_POOL_NAME bintree_node_memory_pool
#define MEMORY_POOL_TYPE bintree_node_t
#include "memory_pool.h"
#undef MEMORY_POOL_NAME
#undef MEMORY_POOL_TYPE

#define MEMORY_POOL_NAME bintree_node_concurrent_memory_pool
#define MEMORY_POOL_TYPE bintree_node_t
#define MEMORY_POOL_THREAD_SAFE
#include "memory_pool.h"
#undef MEMORY_POOL_NAME
#undef MEMORY_POOL_TYPE
#undef MEMORY_POOL_THREAD_SAFE

typedef struct {
    bintree_node_concurrent_memory_pool *pool;
    bintree_node_t **nodes;
} bintree_concurrent_memory_pool_test_arg_t;

int bintree_memory_pool_create_thread(void *arg) {
    bintree_concurrent_memory_pool_test_arg_t *input = (bintree_concurrent_memory_pool_test_arg_t *)arg;
    bintree_node_concurrent_memory_pool *pool = input->pool;
    bintree_node_t **nodes = input->nodes;

    bintree_node_t *node;

    for (size_t i = 0; i < pool->block_size; i++) {
        node = bintree_node_concurrent_memory_pool_get(pool);
        if (node == NULL) {
            return 1;
        }
        nodes[i] = node;
    }

    for (size_t i = 0; i < pool->block_size; i++) {
        node = nodes[i];
        if (!bintree_node_concurrent_memory_pool_release(pool, nodes[i])) {
            return 1;
        }
        nodes[i] = NULL;
    }
    return 0;
}

#define NUM_THREADS 8

TEST test_concurrent_memory_pool(void)  {
    bintree_node_concurrent_memory_pool *pool = bintree_node_concurrent_memory_pool_new();
    ASSERT(pool != NULL);
    ASSERT_EQ(pool->num_blocks, 1);

    size_t max_nodes = pool->block_size * NUM_THREADS;
    bintree_node_t **nodes = malloc(max_nodes * sizeof(bintree_node_t *));

    thrd_t create_threads[NUM_THREADS];
    bintree_concurrent_memory_pool_test_arg_t args[NUM_THREADS];

    for (size_t i = 0; i < NUM_THREADS; i++) {
        args[i] = (bintree_concurrent_memory_pool_test_arg_t){
            .pool = pool,
            .nodes = nodes + (i * pool->block_size)
        };
        thrd_create(&create_threads[i], bintree_memory_pool_create_thread, &args[i]);
    }
    for (size_t i = 0; i < NUM_THREADS; i++) {
        thrd_join(create_threads[i], NULL);
    }

    size_t free_list_size = 0;
    bintree_node_concurrent_memory_pool_free_list_t free_list = atomic_load(&pool->free_list);
    bintree_node_concurrent_memory_pool_item_t *item = free_list.node;

    bintree_node_concurrent_memory_pool_item_t *prev_item = NULL;

    while (item != NULL) {
        for (size_t i = 0; i < free_list_size; i++) {
            if (nodes[i] == (bintree_node_t *)item) {
                fprintf(stderr, "duplicate node: %p, prev_item=%p, prev_i=%zu, free_list_size=%zu\n", item, prev_item, i, free_list_size);
                FAIL();
            }
        }
        nodes[free_list_size++] = (bintree_node_t *)item;
        ASSERT(free_list_size <= NUM_THREADS * pool->block_size);
        prev_item = item;
        item = item->next;
    }
    free(nodes);

    bintree_node_concurrent_memory_pool_destroy(pool);
    PASS();
}

int bintree_memory_pool_thread(void *arg) {
    bintree_node_concurrent_memory_pool *pool = (bintree_node_concurrent_memory_pool *)arg;

    bintree_node_t **nodes = malloc(pool->block_size * sizeof(bintree_node_t *));
    bintree_node_t *node;

    for (size_t i = 0; i < pool->block_size; i++) {
        node = bintree_node_concurrent_memory_pool_get(pool);
        if (node == NULL) {
            return 1;
        }
        nodes[i] = node;
    }
    for (size_t i = 0; i < pool->block_size; i++) {
        node = nodes[i];
        if (!bintree_node_concurrent_memory_pool_release(pool, node)) {
            return 1;
        }
    }
    free(nodes);
    return 0;
}


TEST test_memory_pool(void) {
    bintree_node_memory_pool *pool = bintree_node_memory_pool_new();
    ASSERT(pool != NULL);
    ASSERT_EQ(pool->num_blocks, 1);

    for (size_t i = 0; i < pool->block_size; i++) {
        bintree_node_t *node = bintree_node_memory_pool_get(pool);
        if (node == NULL) {
            FAIL();
        }
    }

    bintree_node_t *node1 = bintree_node_memory_pool_get(pool);
    ASSERT(node1 != NULL);
    ASSERT_EQ(pool->num_blocks, 2);

    bintree_node_t *node2 = bintree_node_memory_pool_get(pool);
    ASSERT(node2 != NULL);
    bintree_node_t *node3 = bintree_node_memory_pool_get(pool);
    ASSERT(node3 != NULL);

    bintree_node_memory_pool_release(pool, node2);
    bintree_node_memory_pool_release(pool, node1);

    bintree_node_t *node4 = bintree_node_memory_pool_get(pool);
    ASSERT_EQ(node4, node1);
    bintree_node_t *node5 = bintree_node_memory_pool_get(pool);
    ASSERT_EQ(node5, node2);

    bintree_node_t *node6 = bintree_node_memory_pool_get(pool);
    ASSERT(node6 != NULL);
    ASSERT_EQ(pool->block_remaining, pool->block_size - 4);

    bintree_node_memory_pool_destroy(pool);
    PASS();
}


/* Add definitions that need to be in the test runner's main file. */
GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* command-line options, initialization. */

    RUN_TEST(test_memory_pool);
    RUN_TEST(test_concurrent_memory_pool);

    GREATEST_MAIN_END();        /* display results */
}