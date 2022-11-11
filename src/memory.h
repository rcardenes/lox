#ifndef vlox_memory_h
#define vlox_memory_h

#include "common.h"
#include "object.h"

#define ALLOCATE(type, count) \
	(type*)reallocate(NULL, 0, sizeof(type) * count);

#define GROW_CAPACITY(capacity) \
	((capacity) < 8 ? 8 : ((capacity) * 2))

#define GROW_ARRAY(type, pointer, oldCount, newCount) \
	(type*)reallocate(pointer, sizeof(type) * (oldCount), \
			sizeof(type) * (newCount))

#define FREE_ARRAY(type, pointer, oldCount) \
	reallocate(pointer, sizeof(type) * (oldCount), 0)

#define FREE_VARIABLE(type, additional, pointer) reallocate(pointer, sizeof(type) + additional, 0);
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0);

void* reallocate(void*, size_t, size_t);
void freeObjects();

#endif // vlox_memory_h
