#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

VM vm;

static void resetStack() {
	vm.stackTop = vm.stack;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	size_t instruction = vm.ip - vm.chunk->code - 1;
	int line = getLine(vm.chunk, instruction);
	fprintf(stderr, "[line %d] in script\n", line);
	resetStack();
}

void initVM() {
	vm.stack = (Value*)reallocate(NULL, 0, sizeof(Value) * STACK_SLICE_SIZE);
	vm.stackLimit = vm.stack + STACK_SLICE_SIZE;
	resetStack();
	vm.objects = NULL;
	initTable(&vm.globals);
	initTable(&vm.strings);
}

void freeVM() {
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	freeObjects();
	free(vm.stack);
}

void push(Value value) {
	if (vm.stackTop == vm.stackLimit) {
		int size = vm.stackLimit - vm.stack;
		int newSize = size + STACK_SLICE_SIZE;
		Value *newStack = GROW_ARRAY(Value, vm.stack, size, newSize);
		if (newStack != vm.stack) {
			vm.stack = newStack;
			vm.stackTop = vm.stack + size;
		}
		vm.stackLimit = vm.stack + newSize;
	}
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

/*
 * Inline is C99/C11 compliant
 */
static
inline int read_24bit_int() {
	int res = *vm.ip++;
	res |= (*vm.ip++) << 8;
	return res | ((*vm.ip++) << 16);
}

static
inline Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static
inline void replace(Value val) {
	*(vm.stackTop - 1) = val;
}

static bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
	StringList sl;

	initStringList(&sl);
	addStringToList(&sl, AS_STRING(pop()));
	prependStringToList(&sl, AS_STRING(peek(0)));
	ObjString* result= copyStrings(&sl);
	replace(OBJ_VAL(result));
	resetStringList(&sl);
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_24BIT_INT() (read_24bit_int())
#define READ_CONSTANT(isShort) (vm.chunk->constants.values[(isShort) ? READ_BYTE(): READ_24BIT_INT()])
#define READ_SHORT() \
	(vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_STRING(isShort) AS_STRING(READ_CONSTANT(isShort))
#define BINARY_OP(retType, op) \
	do { \
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
			runtimeError("Operands must be numbers."); \
			return INTERPRET_RUNTIME_ERROR; \
		} \
		double b = AS_NUMBER(pop()); \
		double a = AS_NUMBER(peek(0)); \
		replace(retType(a op b)); \
	} while (false)

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
			printf("[ ");
			printValue(*slot);
			printf(" ]");
		}
		printf("\n");
		disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif
		uint8_t instruction = READ_BYTE();

		switch (instruction) {
			case OP_CONSTANT_LONG:
			case OP_CONSTANT: {
					Value constant = READ_CONSTANT(instruction == OP_CONSTANT);
					push(constant);
					break;
				}
			case OP_NIL: push(NIL_VAL); break;
			case OP_TRUE: push(BOOL_VAL(true)); break;
			case OP_FALSE: push(BOOL_VAL(false)); break;
			case OP_POP: pop(); break;
			case OP_GET_LOCAL: {
				uint8_t slot = READ_BYTE();
				push(vm.stack[slot]);
				break;
			}
			case OP_GET_GLOBAL_LONG:
			case OP_GET_GLOBAL: {
				ObjString* name = READ_STRING(instruction == OP_GET_GLOBAL);
				Value value;
				if (!tableGet(&vm.globals, name, &value)) {
					runtimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				push(value);
				break;
			}
			case OP_DEFINE_GLOBAL_LONG:
			case OP_DEFINE_GLOBAL: {
				ObjString* name = READ_STRING(instruction == OP_DEFINE_GLOBAL);
				tableSet(&vm.globals, name, peek(0));
				pop();
				break;
			}
			case OP_SET_LOCAL: {
				uint8_t slot = READ_BYTE();
				vm.stack[slot] = peek(0);
				break;
			}
			case OP_SET_GLOBAL_LONG:
			case OP_SET_GLOBAL: {
				ObjString* name = READ_STRING(instruction == OP_SET_GLOBAL);
				if (tableSet(&vm.globals, name, peek(0))) {
					tableDelete(&vm.globals, name);
					runtimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_EQUAL: {
				       Value b = pop();
				       Value a = peek(0);
				       replace(BOOL_VAL(valuesEqual(a, b)));
				       break;
			       }
			case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
			case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
			case OP_ADD: {
			     if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
				     concatenate();
			     }
			     else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				     double b = AS_NUMBER(pop());
				     double a = AS_NUMBER(peek(0));
				     replace(NUMBER_VAL(a + b));
			     }
			     else {
				     runtimeError("Operands must be two numbers or two strings.");
				     return INTERPRET_RUNTIME_ERROR;
			     }
			     break;
			}
			case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
			case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
			case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
			case OP_NOT:
				replace(BOOL_VAL(isFalsey(peek(0))));
				break;
			case OP_NEGATE: {
				Value constant = peek(0);
				if (!IS_NUMBER(constant)) {
					runtimeError("Operand must be a number.");
					return INTERPRET_RUNTIME_ERROR;
				}
				replace(NUMBER_VAL(-AS_NUMBER(constant)));
				break;
			}
			case OP_PRINT: {
				printValue(pop());
				printf("\n");
				break;
			}
			case OP_JUMP: {
				uint16_t offset = READ_SHORT();
				vm.ip += offset;
				break;
			}
			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(0))) vm.ip += offset;
				break;
			}
			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
				vm.ip -= offset;
				break;
			}
			case OP_RETURN: {
				// Exit the interpreter.
				return INTERPRET_OK;
			}
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_24BIT_INT
#undef READ_CONSTANT
#undef READ_STRING
#undef READ_LONG_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(const char* source) {
	Chunk chunk;
	initChunk(&chunk);

	if (!compile(source, &chunk)) {
		freeChunk(&chunk);
		return INTERPRET_COMPILE_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;

	InterpretResult result = run();

	freeChunk(&chunk);
	return result;
}
