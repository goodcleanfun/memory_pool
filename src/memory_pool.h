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

#ifdef MEMORY_POOL_THREAD_SAFE
#include <stdatomic.h>
#include "threading/threading.h"
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

#define MEMORY_POOL_ITEM MEMORY_POOL_TYPED(item_t)

#ifdef MEMORY_POOL_THREAD_SAFE
typedef struct MEMORY_POOL_TYPED(free_list) {
    size_t version;  // Version counter to avoid the ABA problem
    MEMORY_POOL_ITEM *node;
} MEMORY_POOL_TYPED(free_list_t);
#endif

typedef struct MEMORY_POOL_TYPED(block) {
    struct MEMORY_POOL_TYPED(block) *next;
    MEMORY_POOL_TYPE data[];
} MEMORY_POOL_TYPED(block_t);

typedef struct {
    size_t num_blocks;
    size_t block_size;
    size_t type_size;
    MEMORY_POOL_TYPED(block_t) *block;
    #ifndef MEMORY_POOL_THREAD_SAFE
    size_t block_remaining;
    MEMORY_POOL_TYPED(item_t) *free_list;
    #else
    atomic_size_t block_index;
    // Uses double compare-and-swap with a version counter to avoid the ABA problem
    _Atomic MEMORY_POOL_TYPED(free_list_t) free_list;
    // Use cheap non-exclusive read locks to get new nodes from the current block
    // Take exclusive write-lock only when adding a new block and resetting counter
    rwlock_t block_change_lock;
    #endif
} MEMORY_POOL_NAME;

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new_size)(size_t block_size, size_t type_size) {
    if (!IS_POWER_OF_TWO(block_size)) {
        return NULL;
    }
    MEMORY_POOL_NAME *pool = calloc(1, sizeof(MEMORY_POOL_NAME));
    if (pool == NULL) return NULL;
    MEMORY_POOL_TYPED(block_t) *block = aligned_malloc(sizeof(MEMORY_POOL_TYPED(block_t)) + block_size * type_size, block_size);
    if (block == NULL) {
        free(pool);
        return NULL;
    }
    block->next = NULL;

    pool->block = block;
    pool->type_size = type_size;
    pool->block_size = block_size;
    pool->num_blocks = 1;

    #ifndef MEMORY_POOL_THREAD_SAFE
    pool->free_list = NULL;
    pool->block_remaining = block_size;
    #else
    pool->free_list = (MEMORY_POOL_TYPED(free_list_t)){.version = 0, .node = NULL};
    atomic_init(&pool->block_index, 0);
    rwlock_init(&pool->block_change_lock, NULL);
    #endif
    return pool;
}

MEMORY_POOL_NAME *MEMORY_POOL_FUNC(new)(void) {
    return MEMORY_POOL_FUNC(new_size)(DEFAULT_MEMORY_POOL_BLOCK_SIZE, sizeof(MEMORY_POOL_TYPE));
}

void MEMORY_POOL_FUNC(destroy)(MEMORY_POOL_NAME *pool) {
    if (pool == NULL) return;
    #ifdef MEMORY_POOL_THREAD_SAFE
    rwlock_destroy(&pool->block_change_lock);
    #endif
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
    #ifndef MEMORY_POOL_THREAD_SAFE
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
    #else
    MEMORY_POOL_TYPED(free_list_t) head, new_head;
    MEMORY_POOL_TYPE *value = NULL;
    // Compare-and-swap loop on the free-list (double-wide with a version counter)
    do {
        head = atomic_load(&pool->free_list);
        if (head.node == NULL) {
            break;
        }
        // We increment the version in the more common release case. Only needed on one side
        new_head.version = head.version;
        new_head.node = head.node->next;
    } while (!atomic_compare_exchange_weak(&pool->free_list, &head, new_head));

    if (head.node != NULL) {
        value =  (MEMORY_POOL_TYPE *)head.node;
        return value;
    }

    bool in_block = false;

    size_t index;
    while (!in_block) {
        // Take a non-exclusive read lock to make sure we're not in the middle of adding a block
        rwlock_rdlock(&pool->block_change_lock);
        // This gets the current thread a unique index in the current block
        index = atomic_fetch_add(&pool->block_index, 1);
        rwlock_unlock(&pool->block_change_lock);

        in_block = index < pool->block_size;
        if (in_block) {
            value = pool->block->data + index;
        } else {
            /*  If the counter has gone beyond the block size, we need a new block.
             ** Take a write lock to make sure only one thread grows the pool.
             ** Whoever gets the exclusive lock first is responsible for allocating
             ** the new block and connecting it to the block list.
             */
            if (rwlock_trywrlock(&pool->block_change_lock) != thrd_busy) {
                /* Check if another thread that was waiting with a high index has already added a new block
                ** if this is the case, the block index is already reset and we can proceed
                */
                if (atomic_load(&pool->block_index) < pool->block_size) {
                    rwlock_unlock(&pool->block_change_lock);
                    thrd_yield();
                    continue;
                }
                MEMORY_POOL_TYPED(block_t) *block = aligned_malloc(sizeof(MEMORY_POOL_TYPED(block_t)) + pool->block_size * sizeof(MEMORY_POOL_TYPE), pool->block_size);
                if (block == NULL) return NULL;
                block->next = pool->block;
                pool->block = block;
                pool->num_blocks++;
                // Claim the zeroth index for this thread and store 1 in the block index
                index = 0;
                value = pool->block->data;
                atomic_store(&pool->block_index, 1);
                rwlock_unlock(&pool->block_change_lock);
                break;
            }
        }
    }
    return value;
    #endif
}

bool MEMORY_POOL_FUNC(release)(MEMORY_POOL_NAME *pool, MEMORY_POOL_TYPE *value) {
    if (pool == NULL || value == NULL) return false;
    #ifndef MEMORY_POOL_THREAD_SAFE
    // Set the next pointer to the current head (which is a data pointer)
    MEMORY_POOL_ITEM *head = pool->free_list;
    pool->free_list = (MEMORY_POOL_ITEM *)value;
    pool->free_list->next = head;
    #else
    MEMORY_POOL_TYPED(free_list_t) head, new_head;
    do {
        head = atomic_load(&pool->free_list);
        /* Version counter is an optimistic way to prevent the ABA problem
        ** Increment the counter to make sure when this node becomes head,
        ** another thread didn't already pull the previous head.
        */
        new_head.version = head.version + 1;
        new_head.node = (MEMORY_POOL_ITEM *)value;
        new_head.node->next = head.node;
    } while (!atomic_compare_exchange_weak(&pool->free_list, &head, new_head));
    #endif

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