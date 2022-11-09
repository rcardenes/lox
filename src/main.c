#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int arch, const char* argv[]) {
	Chunk chunk;

	initChunk(&chunk);

	int constant1 = addConstant(&chunk, 0.1);
	writeChunk(&chunk, OP_CONSTANT);
	writeChunk(&chunk, constant1);
	addLine(&chunk, 122, 2);

	int constant = addConstant(&chunk, 1.2);
	writeChunk(&chunk, OP_CONSTANT);
	writeChunk(&chunk, constant);
	addLine(&chunk, 123, 2);

	writeChunk(&chunk, OP_RETURN);
	addLine(&chunk, 123, 1);


	disassembleChunk(&chunk, "test chunk");
	freeChunk(&chunk);

	return 0;
}
