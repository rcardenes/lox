#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

VM vm;

static Value clockNative(int argCount, Value* args) {
	return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->function;
		size_t instruction = frame->ip - function->chunk.code - 1;
		int line = getLine(&function->chunk, instruction);
		fprintf(stderr, "[line %d] in ", line);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack();
}

static void defineNative(const char* name, NativeFn function) {
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void initVM() {
	vm.stack = (Value*)reallocate(NULL, 0, sizeof(Value) * STACK_SLICE_SIZE);
	vm.stackLimit = vm.stack + STACK_SLICE_SIZE;
	resetStack();
	vm.objects = NULL;
	initTable(&vm.globals);
	initTable(&vm.strings);

	defineNative("clock", clockNative);
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
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	int res = *frame->ip++;
	res |= (*frame->ip++) << 8;
	return res | ((*frame->ip++) << 16);
}

static
inline Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static bool call(ObjFunction* function, int argCount) {
	if (argCount != function->arity) {
		runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		runtimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->function = function;
	frame->ip = function->chunk.code;
	frame->slots = vm.stackTop - argCount - 1;
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
			case OBJ_FUNCTION:
				return call(AS_FUNCTION(callee), argCount);
			case OBJ_NATIVE: {
				NativeFn native = AS_NATIVE(callee);
				Value result = native(argCount, vm.stackTop - argCount);
				vm.stackTop -= argCount + 1;
				push(result);
				return true;
			}
			default:
				break; // Non-callable object type.
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
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
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	printObject(OBJ_VAL(frame->function)); printf("\n");


#define READ_BYTE() (*frame->ip++)
#define READ_24BIT_INT() (read_24bit_int())
#define READ_CONSTANT(isShort) (frame->function->chunk.constants.values[(isShort) ? READ_BYTE(): READ_24BIT_INT()])
#define READ_SHORT() \
	(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
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
		if (vm.stackTop == vm.stack) {
			printf("empty_stack");
		}
		else {
			for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
				printf("[ ");
				printValue(*slot);
				printf(" ]");
			}
		}
		printf("\n");
		disassembleInstruction(&frame->function->chunk,
				(int)(frame->ip - frame->function->chunk.code));
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
				push(frame->slots[slot]);
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
				frame->slots[slot] = peek(0);
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
			case OP_EQUAL_NO_POP: {
				       Value b = peek(0);
				       Value a = peek(-1);
				       replace(BOOL_VAL(valuesEqual(a, b)));
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
				frame->ip += offset;
				break;
			}
			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(0))) frame->ip += offset;
				break;
			}
			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
				frame->ip -= offset;
				break;
			}
			case OP_CALL: {
				int argCount = READ_BYTE();
				if (!callValue(peek(argCount), argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}
			case OP_RETURN: {
				Value result = pop();
				vm.frameCount--;
				if (vm.frameCount == 0) {
					pop();
					return INTERPRET_OK;
				}

				vm.stackTop = frame->slots;
				push(result);
				frame = &vm.frames[vm.frameCount - 1];
				break;
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
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	push(OBJ_VAL(function));
	call(function, 0);

	return run();
}
