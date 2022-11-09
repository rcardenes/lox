#ifndef vlox_value_h
#define vlox_value_h

#include "common.h"

typedef double Value;

typedef struct {
	int capacity;
	int count;
	Value* values;
} ValueArray;

void initValueArray(ValueArray*);
void writeValueArray(ValueArray*, Value);
void freeValueArray(ValueArray*);
void printValue(Value);

#endif // vlox_value_h
