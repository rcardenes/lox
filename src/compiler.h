#ifndef vlox_compiler_h
#define vlox_compiler_h

#include "object.h"
#include "vm.h"

ObjFunction* compile(const char*);
void markCompilerRoots();

#endif // vlox_compiler_h
