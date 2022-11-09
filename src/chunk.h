#ifndef vlox_chunk_h
#define vlox_chunk_h

#include "common.h"
#include "memory.h"
#include "value.h"

typedef enum {
	OP_RETURN,
	OP_CONSTANT
} OpCode;

typedef struct {
	int count;
	int capacity;
	uint8_t* code;
	ValueArray constants;
} Chunk;

void initChunk(Chunk*);
void freeChunk(Chunk*);
void writeChunk(Chunk*, uint8_t);
int addConstant(Chunk*, Value);

#endif // vlox_chunk_h
