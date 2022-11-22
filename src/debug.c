#include <stdio.h>
#include "debug.h"
#include "object.h"
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

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
	uint8_t slot = chunk->code[offset + 1];
	printf("%-16s %18d\n", name, slot);
	return offset + 2;
}

static int jumpInstruction(const char* name, int sign, Chunk* chunk, int offset) {
	uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
	jump |= chunk->code[offset + 2];
	printf("%-16s %18d -> %d\n", name, offset, offset + 3 + sign * jump);
	return offset + 3;
}

static int decodeConstantIndex(Chunk* chunk, int offset, uint32_t* cnst) {
	uint32_t constant = chunk->code[offset + 1];

	if (constant > 127 ) {
		constant = (constant & 0x7F) << 16
			 | ((uint32_t)chunk->code[offset+2]) << 8
			 | ((uint32_t)chunk->code[offset+3]);
	}

	offset += (constant < 128 ? 2 : 4);
	*cnst = constant;

	return offset;
}

static int constantInstruction(const char* name, Chunk* chunk, int offset) {
	uint32_t constant;

	offset = decodeConstantIndex(chunk, offset, &constant);

	printf("%-16s %18d '", name, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");

	return offset;
}

static int invokeInstruction(const char* name, Chunk* chunk, int offset) {
	uint32_t constant;

	offset = decodeConstantIndex(chunk, offset, &constant);
	uint8_t argCount = chunk->code[offset];
	printf("%-16s (%d args) %9d '", name, argCount, constant);
	printValue(chunk->constants.values[constant]);
	printf("'\n");
	return offset + 1;
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
		case OP_CONSTANT:
			return constantInstruction("OP_CONSTANT", chunk, offset);
		case OP_NIL:
			return simpleInstruction("OP_NIL", offset);
		case OP_TRUE:
			return simpleInstruction("OP_TRUE", offset);
		case OP_FALSE:
			return simpleInstruction("OP_FALSE", offset);
		case OP_POP:
			return simpleInstruction("OP_POP", offset);
		case OP_GET_LOCAL:
			return byteInstruction("OP_GET_LOCAL", chunk, offset);
		case OP_SET_LOCAL:
			return byteInstruction("OP_SET_LOCAL", chunk, offset);
		case OP_GET_GLOBAL:
			return constantInstruction("OP_GET_GLOBAL", chunk, offset);
		case OP_DEFINE_GLOBAL:
			return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
		case OP_SET_GLOBAL:
			return constantInstruction("OP_SET_GLOBAL", chunk, offset);
		case OP_GET_UPVALUE:
			return byteInstruction("OP_GET_UPVALUE", chunk, offset);
		case OP_SET_UPVALUE:
			return byteInstruction("OP_SET_UPVALUE", chunk, offset);
		case OP_GET_PROPERTY:
			return constantInstruction("OP_GET_PROPERTY", chunk, offset);
		case OP_SET_PROPERTY:
			return constantInstruction("OP_SET_PROPERTY", chunk, offset);
		case OP_GET_SUPER:
			return constantInstruction("OP_GET_SUPER", chunk, offset);
		case OP_EQUAL_NO_POP:
			return simpleInstruction("OP_EQUAL_NO_POP", offset);
		case OP_EQUAL:
			return simpleInstruction("OP_EQUAL", offset);
		case OP_GREATER:
			return simpleInstruction("OP_GREATER", offset);
		case OP_LESS:
			return simpleInstruction("OP_LESS", offset);
		case OP_ADD:
			return simpleInstruction("OP_ADD", offset);
		case OP_SUBTRACT:
			return simpleInstruction("OP_SUBTRACT", offset);
		case OP_MULTIPLY:
			return simpleInstruction("OP_MULTIPLY", offset);
		case OP_DIVIDE:
			return simpleInstruction("OP_DIVIDE", offset);
		case OP_NOT:
			return simpleInstruction("OP_NOT", offset);
		case OP_NEGATE:
			return simpleInstruction("OP_NEGATE", offset);
		case OP_JUMP:
			return jumpInstruction("OP_JUMP", 1, chunk, offset);
		case OP_JUMP_IF_FALSE:
			return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
		case OP_LOOP:
			return jumpInstruction("OP_LOOP", -1, chunk, offset);
		case OP_CALL:
			return byteInstruction("OP_CALL", chunk, offset);
		case OP_INVOKE:
			return invokeInstruction("OP_INVOKE", chunk, offset);
		case OP_SUPER_INVOKE:
			return invokeInstruction("OP_SUPER_INVOKE", chunk, offset);
		case OP_CLOSURE: {
			uint32_t constant;

			offset = decodeConstantIndex(chunk, offset, &constant);

			printf("OP_CLOSURE       %18d '", constant);
			printValue(chunk->constants.values[constant]);
			printf("'\n");

			ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
			for (int j = 0; j < function->upvalueCount; j++) {
				int isLocal = chunk->code[offset++];
				int index = chunk->code[offset++];
				printf("%04d    | %39s %d\n",
						offset - 2, isLocal ? "local" : "upvalue", index);
			}
			return offset;
		}
		case OP_CLOSE_UPVALUE:
			return simpleInstruction("OP_CLOSE_UPVALUE", offset);
		case OP_RETURN:
			return simpleInstruction("OP_RETURN", offset);
		case OP_PRINT:
			return simpleInstruction("OP_PRINT", offset);
		case OP_CLASS: {
			uint32_t constant;

			offset = decodeConstantIndex(chunk, offset, &constant);

			printf("OP_CLASS         %18d '", constant);
			printValue(chunk->constants.values[constant]);
			printf("'\n");
			return offset;
		}
		case OP_INHERIT:
			return simpleInstruction("OP_INHERIT", offset);
		case OP_METHOD: {
			uint32_t constant;

			offset = decodeConstantIndex(chunk, offset, &constant);

			printf("OP_METHOD        %18d '", constant);
			printValue(chunk->constants.values[constant]);
			printf("'\n");
			return offset;
		}
		default:
			printf("Unknown opcode %d\n", instruction);
			return offset + 1;
	}
}
