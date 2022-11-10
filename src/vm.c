#include <stdio.h>
#include "debug.h"
#include "vm.h"

VM vm;

static void resetStack() {
	vm.stackTop = vm.stack;
}

void initVM() {
	resetStack();
}

void freeVM() {
}

void push(Value value) {
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

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_24BIT_INT() (read_24bit_int())
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_LONG_CONSTANT() (vm.chunk->constants.values[READ_24BIT_INT()])
#define PEEK() (*(vm.stackTop - 1))
#define REPLACE(val) do { *(vm.stackTop - 1) = (val); } while (false)
#define BINARY_OP(op) \
	do { \
		double b = pop(); \
		double a = PEEK(); \
		REPLACE(a op b); \
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
		uint8_t instruction;

		switch (instruction = READ_BYTE()) {
			case OP_CONSTANT:
				{
					Value constant = READ_CONSTANT();
					push(constant);
				}
				break;
			case OP_CONSTANT_LONG:
				{
					Value constant = READ_LONG_CONSTANT();
					push(constant);
				}
				break;
			case OP_ADD: BINARY_OP(+); break;
			case OP_SUBTRACT: BINARY_OP(-); break;
			case OP_MULTIPLY: BINARY_OP(*); break;
			case OP_DIVIDE: BINARY_OP(/); break;
			case OP_NEGATE: REPLACE(-PEEK()); break;
			case OP_RETURN:
				{
					printValue(pop());
					printf("\n");
					return INTERPRET_OK;
				}
				break;
		}
	}

#undef READ_BYTE
#undef READ_24BIT_INT
#undef READ_CONSTANT
#undef READ_LONG_CONSTANT
#undef BINARY_OP
}

InterpretResult interpret(Chunk* chunk) {
	vm.chunk = chunk;
	vm.ip = vm.chunk->code;

	return run();
}
