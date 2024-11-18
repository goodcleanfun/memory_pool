#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "aligned/aligned.h"

#define DEFAULT_MEMORY_POOL_BLOCK_SIZE 256

#endif // MEMORY_POOL_H

#ifndef MEMORY_POOL_NAME
#error "Must define MEMORY_POOL_NAME"
#endif

#ifndef MEMORY_POOL_TYPE
#error "Must define MEMORY_POOL_TYPE" 
#endif

#define IS_POWER_OF_TWO(x) (((x) & ((x) - 1)) == 0)

#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT3_(a, b, c) a ## b ## c
#define CONCAT3(a, b, c) CONCAT3_(a, b, c)
#define MEMORY_POOL_FUNC(func) CONCAT(MEMORY_POOL_NAME, _##func)
#define MEMORY_POOL_TYPED(val) CONCAT(MEMORY_POOL_NAME, _##val)

typedef union MEMORY_POOL_TYPED(item) {
    union MEMORY_POOL_TYPED(item) *next;
    MEMORY_POOL_TYPE value;
} MEMORY_POOL_TYPED(item_t);


typedef struct MEMORY_POOL_TYPED(block) {
    struct MEMORY_POOL_TYPED(block) *next;
    MEMORY_POOL_TYPE data[];
} MEMORY_POOL_TYPED(block_t);

typedef struct {
    size_t block_size;
    MEMORY_POOL_TYPED(block_t) *block;
    size_t block_remaining;
    size_t num_blocks;
    MEMORY_POOL_TYPED(item_t) *free_list;
} MEMORY_POOL_NAME;

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new_size)(size_t block_size) {
    if (!IS_POWER_OF_TWO(block_size)) {
        return NULL;
    }
    MEMORY_POOL_NAME *pool = calloc(1, sizeof(MEMORY_POOL_NAME));
    if (pool == NULL) return NULL;
    MEMORY_POOL_TYPED(block_t) *block = aligned_malloc(sizeof(MEMORY_POOL_TYPED(block_t)) + block_size * sizeof(MEMORY_POOL_TYPE), block_size);
    if (block == NULL) {
        free(pool);
        return NULL;
    }
    block->next = NULL;
    pool->free_list = NULL;

    pool->block = block;
    pool->block_remaining = block_size;
    pool->block_size = block_size;
    pool->num_blocks = 1;

    return pool;
}

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new)(void) {
    return MEMORY_POOL_FUNC(new_size)(DEFAULT_MEMORY_POOL_BLOCK_SIZE);
}

void MEMORY_POOL_FUNC(destroy)(MEMORY_POOL_NAME *pool) {
    if (pool == NULL) return;
    MEMORY_POOL_TYPED(block_t) *block = pool->block;
    while(block != NULL) {
        MEMORY_POOL_TYPED(block_t) *next = block->next;
        free(block);
        block = next;        
    }
    free(pool);
}

MEMORY_POOL_TYPE *MEMORY_POOL_FUNC(get)(MEMORY_POOL_NAME *pool) {
    if (pool == NULL) return NULL;
    if (pool->free_list != NULL) {
        MEMORY_POOL_TYPED(item_t) *head = pool->free_list;
        MEMORY_POOL_TYPE *value = (MEMORY_POOL_TYPE *)head;
        pool->free_list = head->next;
        return value;
    }
    if (pool->block_remaining == 0) {
        MEMORY_POOL_TYPED(block_t) *block = aligned_malloc(sizeof(MEMORY_POOL_TYPED(block_t)) + pool->block_size * sizeof(MEMORY_POOL_TYPE), pool->block_size);
        if (block == NULL) return NULL;
        block->next = pool->block;
        pool->block = block;
        pool->num_blocks++;
        pool->block_remaining = pool->block_size;
    }

    MEMORY_POOL_TYPE *value = pool->block->data + (pool->block_size - pool->block_remaining);
    pool->block_remaining--;
    return value;
}

bool MEMORY_POOL_FUNC(release)(MEMORY_POOL_NAME *pool, MEMORY_POOL_TYPE *value) {
    if (pool == NULL || value == NULL) return false;
    // Set the next pointer to the current head (which is a data pointer)
    if (pool->free_list == NULL) {
        // Keep the pointer to 
        pool->free_list = (MEMORY_POOL_TYPED(item_t) *)value;
        pool->free_list->next = NULL;
    } else {
        MEMORY_POOL_TYPED(item_t) *head = pool->free_list;
        pool->free_list = (MEMORY_POOL_TYPED(item_t) *)value;
        pool->free_list->next = head;
    }
    return true;
}


#undef CONCAT_
#undef CONCAT
#undef CONCAT3_
#undef CONCAT3
#undef MEMORY_POOL_FUNC
#undef MEMORY_POOL_TYPED
#undef MEMORY_POOL_ARRAY_NAME
#undef MEMORY_POOL_ARRAY_FUNC
#undef IS_POWER_OF_TWO