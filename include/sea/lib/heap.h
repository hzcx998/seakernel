#ifndef __SEA_LIB_HEAP_H
#define __SEA_LIB_HEAP_H

#include <sea/types.h>
#include <sea/rwlock.h>

#define HEAP_KMALLOC  1
#define HEAP_LOCKLESS 2
#define HEAPMODE_MAX 0
#define HEAPMODE_MIN 1

struct heapnode {
	void *data;
	uint64_t key;
};

struct heap {
	int flags;
	int mode;
	size_t capacity;
	size_t count;
	rwlock_t rwl;
	struct heapnode *array;
};

struct heap *heap_create(struct heap *heap, int flags, int heapmode);
void heap_insert(struct heap *heap, uint64_t key, void *data);
int heap_peek(struct heap *heap, uint64_t *key, void **data);
int heap_pop(struct heap *heap, uint64_t *key, void **data);
void heap_destroy(struct heap *heap);

#endif
