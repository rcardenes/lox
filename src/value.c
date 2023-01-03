#include <stdio.h>
#include <string.h>

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
	array->values = NULL;
	array->count = 0;
	array->capacity = 0;
}

void writeValueArray(ValueArray* array, Value value) {
	if (array->capacity < (array->count + 1)) {
		int oldCapacity = array->capacity;
		array->capacity = GROW_CAPACITY(oldCapacity);
		array->values = GROW_ARRAY(Value, array->values,
				oldCapacity, array->capacity);
	}

	array->values[array->count] = value;
	array->count++;
}

void freeValueArray(ValueArray* array) {
	FREE_ARRAY(Value, array->values, array->capacity);
	initValueArray(array);
}

void printValue(Value value) {
#ifdef NAN_BOXING
	if (IS_BOOL(value)) {
		printf(AS_BOOL(value) ? "true" : "false");
	}
	else if (IS_NIL(value)) {
		printf("nil");
	}
	else if (IS_INT(value)) {
		printf("%lld", AS_INT(value));
	}
	else if (IS_NUMBER(value)) {
		double d = AS_NUMBER(value);
		int i = (int)d;
		if (d == i) {
			printf("%d", i);
		}
		else {
			printf("%g", d);
		}
	}
	else if (IS_OBJ(value)) {
		printObject(value);
	}
#else
	switch (value.type) {
		case VAL_BOOL: printf(AS_BOOL(value) ? "true" : "false"); break;
		case VAL_NIL: printf("nil"); break;
		case VAL_INT: printf("%lld", AS_INT(value));
		case VAL_NUMBER: {
			double d = AS_NUMBER(value);
			int i = (int)d;
			if (d == i) {
				printf("%d", i);
			}
			else {
				printf("%g", d);
			}
			break;
		}
		case VAL_OBJ: printObject(value); break;
	}
#endif
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
	if (IS_INT(a) && IS_INT(b)) {
		return AS_INT(a) == AS_INT(b);
	}
	if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
		return (IS_INT(a) ? AS_INT(a) : AS_NUMBER(a)) == (IS_INT(b) ? AS_INT(b) : AS_NUMBER(b));
	}
	else
		return a == b;
#else
	if (IS_NUMERIC(a) && IS_NUMERIC(b)) {
		if (a.type == VAL_INT && b.type == VAL_INT) {
			return AS_INT(a) == AS_INT(b);
		}
		else {
			return (IS_INT(a) ? AS_INT(a) : AS_NUMBER(a)) == (IS_INT(b) ? AS_INT(b) : AS_NUMBER(b));
		}
	}

	if (a.type != b.type) return false;
	switch (a.type) {
		case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
		case VAL_NIL: return true;
		case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(a);
		default: return false; // Unreachable
	}
#endif
}
