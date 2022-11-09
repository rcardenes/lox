#include <stdio.h>
#include "debug.h"
#include "value.h"

void disassembleChunk(Chunk* chunk, const char* name) {
	printf("== %s ==\n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = disassembleInstruction(chunk, offset);
	}
}

static int simpleInstruction(const char* name, int offset) {
	printf("%s\n", name);
	return offset + 1;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset, bool isLong) {
	uint32_t constant = chunk->code[offset+1];

	if (isLong) {
		constant |= ((uint32_t)chunk->code[offset+2]) << 8;
		constant |= ((uint32_t)chunk->code[offset+3]) << 16;
	}

	printf("%-16s %9d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + (!isLong ? 2 : 4);
}

int disassembleInstruction(Chunk* chunk, int offset) {
	printf("%04d ", offset);
	int line = getLine(chunk, offset);
	if (line < 0) {
		printf("   | ");
	}
	else {
		printf("%4d ", line);
	}

	uint8_t instruction = chunk->code[offset];

	switch (instruction) {
		case OP_RETURN:
			return simpleInstruction("OP_RETURN", offset);
		case OP_CONSTANT:
			return constantInstruction("OP_CONSTANT", chunk, offset, false);
		case OP_CONSTANT_LONG:
			return constantInstruction("OP_CONSTANT_LONG", chunk, offset, true);
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}
}
