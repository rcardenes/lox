#ifndef vlox_chunk_h
#define vlox_chunk_h

#include "common.h"
#include "value.h"

#define MAX_SHORT_CONST 128

typedef enum {
	OP_CONSTANT,
	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_POP,
	OP_GET_LOCAL,
	OP_GET_GLOBAL,
	OP_DEFINE_GLOBAL,
	OP_SET_LOCAL,
	OP_SET_GLOBAL,
	OP_EQUAL_NO_POP,
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
	OP_JUMP,
	OP_JUMP_IF_FALSE,
	OP_LOOP,
	OP_CALL,
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

void initChunk(Chunk*);
void freeChunk(Chunk*);
void writeChunk(Chunk*, uint8_t, int);
void writeConstant(Chunk*, OpCode, int, int);
int addConstant(Chunk*, Value);
void addLine(Chunk*, int, int);
int getLine(Chunk*, int);

#endif // vlox_chunk_h
