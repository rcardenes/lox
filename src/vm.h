#ifndef vlox_vm_h
#define vlox_vm_h

#include "object.h"
#include "table.h"
#include "value.h"

#define FRAMES_MAX 64
#define STACK_SLICE_SIZE 256

typedef struct {
	ObjClosure* closure;
	uint8_t* ip;
	Value* slots;
} CallFrame;

typedef struct {
	CallFrame frames[FRAMES_MAX];
	int frameCount;

	Value* stack;
	Value* stackLimit;
	Value* stackTop;
	Table globals;
	Table strings;
	ObjUpvalue* openUpvalues;
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
