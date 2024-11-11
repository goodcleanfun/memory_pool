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

    GREATEST_MAIN_END();        /* display results */
}