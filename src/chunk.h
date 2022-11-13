#ifndef vlox_chunk_h
#define vlox_chunk_h

#include "common.h"
#include "memory.h"
#include "value.h"

typedef enum {
	OP_CONSTANT,
	OP_CONSTANT_LONG,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_POP,
	OP_GET_LOCAL,
	OP_GET_GLOBAL,
	OP_GET_GLOBAL_LONG,
	OP_DEFINE_GLOBAL,
	OP_DEFINE_GLOBAL_LONG,
	OP_SET_LOCAL,
	OP_SET_GLOBAL,
	OP_SET_GLOBAL_LONG,
	OP_EQUAL,
	OP_GREATER,
	OP_LESS,
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_NOT,
	OP_NEGATE,
	OP_PRINT,
	OP_RETURN
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

typedef struct {
	bool shortIndex;
	uint32_t index;
} ConstIndex;

void initChunk(Chunk*);
void freeChunk(Chunk*);
void writeChunk(Chunk*, uint8_t, int);
void writeConstant(Chunk*, Value, int);
int addConstant(Chunk*, Value);
void addLine(Chunk*, int, int);
int getLine(Chunk*, int);

#endif // vlox_chunk_h
