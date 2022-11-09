#ifndef vlox_chunk_h
#define vlox_chunk_h

#include "common.h"
#include "memory.h"
#include "value.h"

typedef enum {
	OP_RETURN,
	OP_CONSTANT,
	OP_CONSTANT_LONG
} OpCode;

typedef struct {
	int opCount;
	int lineNo;
} LineInfo;

typedef struct {
	int count;
	int capacity;
	LineInfo* lines;
} LineArray;


typedef struct {
	int count;
	int capacity;
	uint8_t* code;
	LineArray lines;
	ValueArray constants;
} Chunk;

void initChunk(Chunk*);
void freeChunk(Chunk*);
void writeChunk(Chunk*, uint8_t, int);
void writeConstant(Chunk*, Value, int);
int addConstant(Chunk*, Value);
void addLine(Chunk*, int, int);
int getLine(Chunk*, int);

#endif // vlox_chunk_h
