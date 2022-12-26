#ifndef vlox_native_h
#define vlox_native_h

#include "object.h"

typedef struct {
	const char* name;
	int arity;
	NativeFn func;
} NativeDef;

typedef void (*RegisterNative)(NativeDef*);

void miscNativeFunctions(RegisterNative);

#endif // vlox_native_h

