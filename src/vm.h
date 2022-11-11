#ifndef vlox_vm_h
#define vlox_vm_h

#include "chunk.h"
#include "value.h"

#define STACK_SLICE_SIZE 256

typedef struct {
	Chunk* chunk;
	uint8_t* ip;
	Value* stack;
	Value* stackLimit;
	Value* stackTop;
	Obj* objects;
} VM;

extern VM vm;

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

void initVM();
void freeVM();
InterpretResult interpret(const char*);
void push(Value value);
Value pop();

#endif // vlox_vm_h
