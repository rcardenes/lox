#ifndef vbox_debug_h
#define vbox_debug_h

#include "chunk.h"

void disassembleChunk(Chunk*, const char*);
int disassembleInstruction(Chunk*, int);

#endif // vbox_debug_h
