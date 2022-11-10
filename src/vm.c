#include <stdio.h>
#include "vm.h"

VM vm;

void initVM() {
}

void freeVM() {
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

	for (;;) {
		uint8_t instruction;

		switch (instruction = READ_BYTE()) {
			case OP_CONSTANT:
				{
					Value constant = READ_CONSTANT();
					printValue(constant);
					printf("\n");
				}
				break;
			case OP_CONSTANT_LONG:
				{
					Value constant = READ_LONG_CONSTANT();
					printValue(constant);
					printf("\n");
				}
				break;
			case OP_RETURN:
				{
					return INTERPRET_OK;
				}
				break;
		}
	}

#undef READ_BYTE
#undef READ_24BIT_INT
#undef READ_CONSTANT
#undef READ_LONG_CONSTANT
}

InterpretResult interpret(Chunk* chunk) {
	vm.chunk = chunk;
	vm.ip = vm.chunk->code;

	return run();
}
