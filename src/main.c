#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int arch, const char* argv[]) {
	initVM();

	Chunk chunk;

	initChunk(&chunk);

	int i;

	for (i = 0; i < 258; i++) {
		writeConstant(&chunk, i * 1.2, i+1);
	}
	writeChunk(&chunk, OP_RETURN, i);

	// disassembleChunk(&chunk, "test chunk");
	interpret(&chunk);
	freeVM();
	freeChunk(&chunk);

	return 0;
}
