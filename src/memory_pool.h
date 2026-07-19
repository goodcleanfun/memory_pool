#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "aligned/aligned.h"
#include "bit_utils/bit_utils.h"

// Set default block size to roughly 4kb (OS page size) depending on the pointer size
#if ((UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu))
#define DEFAULT_MEMORY_POOL_BLOCK_SIZE 512
#else
#define DEFAULT_MEMORY_POOL_BLOCK_SIZE 256
#endif

#endif // MEMORY_POOL_H

#ifndef MEMORY_POOL_NAME
#error "Must define MEMORY_POOL_NAME"
#endif

#ifndef MEMORY_POOL_TYPE
#error "Must define MEMORY_POOL_TYPE" 
#endif

#ifndef MEMORY_POOL_MALLOC
#define MEMORY_POOL_MALLOC(size) malloc(size)
#define MEMORY_POOL_MALLOC_DEFINED
#endif
#ifndef MEMORY_POOL_CALLOC
#define MEMORY_POOL_CALLOC(num, size) calloc(num, size)
#define MEMORY_POOL_CALLOC_DEFINED
#endif
#ifndef MEMORY_POOL_FREE
#define MEMORY_POOL_FREE(ptr) free(ptr)
#define MEMORY_POOL_FREE_DEFINED
#endif
#ifndef MEMORY_POOL_ALIGNED_MALLOC
#define MEMORY_POOL_ALIGNED_MALLOC(size, alignment) aligned_malloc(size, alignment)
#define MEMORY_POOL_ALIGNED_MALLOC_DEFINED
#endif
#ifndef MEMORY_POOL_ALIGNED_FREE
#define MEMORY_POOL_ALIGNED_FREE(ptr) aligned_free(ptr)
#define MEMORY_POOL_ALIGNED_FREE_DEFINED
#endif

#ifndef MEMORY_POOL_ALIGNMENT
#define MEMORY_POOL_ALIGNMENT CACHE_LINE_SIZE
#define MEMORY_POOL_ALIGNMENT_DEFINED
#endif

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

#define MEMORY_POOL_ITEM MEMORY_POOL_TYPED(item_t)

typedef struct MEMORY_POOL_TYPED(block) {
    struct MEMORY_POOL_TYPED(block) *next;
    size_t block_remaining;
    MEMORY_POOL_TYPE *data;
} MEMORY_POOL_TYPED(block_t);

typedef struct {
    size_t num_blocks;
    size_t block_size;
    size_t type_size;
    MEMORY_POOL_TYPED(block_t) *block;
    MEMORY_POOL_TYPED(item_t) *free_list;
} MEMORY_POOL_NAME;

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new_size)(size_t block_size, size_t type_size) {
    if (!is_power_of_two(block_size)) {
        return NULL;
    }
    MEMORY_POOL_NAME *pool = MEMORY_POOL_CALLOC(1, sizeof(MEMORY_POOL_NAME));
    if (pool == NULL) return NULL;

    MEMORY_POOL_TYPED(block_t) *block = MEMORY_POOL_MALLOC(sizeof(MEMORY_POOL_TYPED(block_t)));
    if (block == NULL) {
        MEMORY_POOL_FREE(pool);
        return NULL;
    }

    block->data = (MEMORY_POOL_TYPE *) MEMORY_POOL_ALIGNED_MALLOC(block_size * type_size, MEMORY_POOL_ALIGNMENT);
    if (block->data == NULL) {
        MEMORY_POOL_FREE(block);
        MEMORY_POOL_FREE(pool);
        return NULL;
    }

    block->next = NULL;
    block->block_remaining = block_size;
    pool->block = block;

    pool->type_size = type_size;
    pool->block_size = block_size;
    pool->num_blocks = 1;

    pool->free_list = NULL;

    return pool;
}

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new)(void) {
    return MEMORY_POOL_FUNC(new_size)(DEFAULT_MEMORY_POOL_BLOCK_SIZE, sizeof(MEMORY_POOL_TYPE));
}

void MEMORY_POOL_FUNC(destroy)(MEMORY_POOL_NAME *pool) {
    if (pool == NULL) return;
    MEMORY_POOL_TYPED(block_t) *block = pool->block;
    while(block != NULL) {
        MEMORY_POOL_TYPED(block_t) *next = block->next;
        MEMORY_POOL_ALIGNED_FREE(block->data);
        MEMORY_POOL_FREE(block);
        block = next;
    }
    MEMORY_POOL_FREE(pool);
}

MEMORY_POOL_TYPE *MEMORY_POOL_FUNC(get)(MEMORY_POOL_NAME *pool) {
    if (pool == NULL) return NULL;
    if (pool->free_list != NULL) {
        MEMORY_POOL_TYPED(item_t) *head = pool->free_list;
        MEMORY_POOL_TYPE *value = (MEMORY_POOL_TYPE *)head;
        pool->free_list = head->next;
        return value;
    }
    if (pool->block->block_remaining == 0) {
        MEMORY_POOL_TYPED(block_t) *block = MEMORY_POOL_MALLOC(sizeof(MEMORY_POOL_TYPED(block_t)));
        if (block == NULL) return NULL;
        block->data = (MEMORY_POOL_TYPE *) MEMORY_POOL_ALIGNED_MALLOC(pool->block_size * sizeof(MEMORY_POOL_TYPE), MEMORY_POOL_ALIGNMENT);
        if (block->data == NULL) {
            MEMORY_POOL_FREE(block);
            return NULL;
        }
        block->next = pool->block;
        block->block_remaining = pool->block_size;
        pool->block = block;
        pool->num_blocks++;
    }

    MEMORY_POOL_TYPE *value = pool->block->data + (pool->block_size - pool->block->block_remaining);
    pool->block->block_remaining--;
    return value;
}

bool MEMORY_POOL_FUNC(release)(MEMORY_POOL_NAME *pool, MEMORY_POOL_TYPE *value) {
    if (pool == NULL || value == NULL) return false;
    // Set the next pointer to the current head (which is a data pointer)
    MEMORY_POOL_ITEM *head = pool->free_list;
    pool->free_list = (MEMORY_POOL_ITEM *)value;
    pool->free_list->next = head;

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
#ifdef MEMORY_POOL_MALLOC_DEFINED
#undef MEMORY_POOL_MALLOC
#undef MEMORY_POOL_MALLOC_DEFINED
#endif
#ifdef MEMORY_POOL_CALLOC_DEFINED
#undef MEMORY_POOL_CALLOC
#undef MEMORY_POOL_CALLOC_DEFINED
#endif
#ifdef MEMORY_POOL_FREE_DEFINED
#undef MEMORY_POOL_FREE
#undef MEMORY_POOL_FREE_DEFINED
#endif
#ifdef MEMORY_POOL_ALIGNED_MALLOC_DEFINED
#undef MEMORY_POOL_ALIGNED_MALLOC
#undef MEMORY_POOL_ALIGNED_MALLOC_DEFINED
#endif