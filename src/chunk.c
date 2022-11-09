#include <stdlib.h>
#include "chunk.h"

#include <stdio.h>

static void initLineArray(LineArray* array) {
	array->count = 0;
	array->capacity = 0;
	array->lines = NULL;
}

static void freeLineArray(LineArray* array) {
	FREE_ARRAY(LineInfo, array->lines, array->capacity);
	initLineArray(array);
}

static void writeLineArray(LineArray* array, int line, int opCount) {
	int index = array->count - 1;

	if ((index >= 0) && (array->lines[index].lineNo == line)) {
		array->lines[index].opCount += opCount;
	}
	else {
		LineInfo info = { opCount, line };
		if (array->capacity < (array->count + 1)) {
			int oldCapacity = array->capacity;
			array->capacity = GROW_CAPACITY(oldCapacity);
			array->lines = GROW_ARRAY(LineInfo, array->lines,
					oldCapacity, array->capacity);
		}
		array->lines[array->count] = info;
		array->count++;
	}
}

void initChunk(Chunk* chunk) {
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	initLineArray(&chunk->lines);
	initValueArray(&chunk->constants);
}

void freeChunk(Chunk* chunk) {
	FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
	freeLineArray(&chunk->lines);
	freeValueArray(&chunk->constants);
	initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) {
	if (chunk->capacity < (chunk->count + 1)) {
		int oldCapacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(oldCapacity);
		chunk->code = GROW_ARRAY(uint8_t, chunk->code,
				oldCapacity, chunk->capacity);
	}

	chunk->code[chunk->count] = byte;
	chunk->count++;

	if (line >= 0) {
		addLine(chunk, line, 1);
	}
}

void writeConstant(Chunk* chunk, Value value, int line) {
	int index = addConstant(chunk, value);

	if (index < 256) {
		writeChunk(chunk, OP_CONSTANT, -1);
		writeChunk(chunk, index, -1);
		addLine(chunk, line, 2);
	}
	else {
		writeChunk(chunk, OP_CONSTANT_LONG, -1);
		writeChunk(chunk, (index & 0xFF), -1);
		writeChunk(chunk, (index & 0xFF00) >> 8, -1);
		writeChunk(chunk, (index & 0xFF0000) >> 16, -1);
		addLine(chunk, line, 4);
	}
}

int addConstant(Chunk* chunk, Value value) {
	writeValueArray(&chunk->constants, value);
	return chunk->constants.count - 1;
}

void addLine(Chunk* chunk, int line, int opCount) {
	writeLineArray(&chunk->lines, line, opCount);
}

int getLine(Chunk* chunk, int offset) {
	LineArray* lines = &chunk->lines;
	const int totalLines = lines->count;

	if (totalLines > 0) {
		int lineOffset = 0;
		int index = 0;

		while ((index < totalLines) && (lineOffset < offset)) {
			lineOffset += lines->lines[index].opCount;
			if (lineOffset <= offset)
				index++;
		}

		if (lineOffset == offset) {
			return lines->lines[index].lineNo;
		}
	}

	return -1;
}
