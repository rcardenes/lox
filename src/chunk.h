#ifndef vlox_chunk_h
#define vlox_chunk_h

#include "common.h"
#include "memory.h"

typedef enum {
	OP_RETURN
} OpCode;

typedef struct {
	int count;
	int capacity;
	uint8_t* code;
} Chunk;

void initChunk(Chunk*);
void freeChunk(Chunk*);
void writeChunk(Chunk*, uint8_t);

#endif // vlox_chunk_h
