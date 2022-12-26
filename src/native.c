#include "native.h"
#include "vm.h"
#include <stdio.h>
#include <time.h>

static NativeReturn clockNative(int, Value*);
static NativeReturn toStringNative(int, Value*);

NativeDef nativeFunctions[] = {
	{ "clock", 0, clockNative },
	{ "toString", 1, toStringNative },
	{ NULL, -1, NULL }
};

void miscNativeFunctions(RegisterNative addToRegistry) {
	NativeDef* current = &nativeFunctions[0];
	while (current->name != NULL) {
		addToRegistry(current++);
	}
}

NativeReturn clockNative(int argCount, Value* args) {
	NativeReturn result = {INTERPRET_OK, NUMBER_VAL((double)clock() / CLOCKS_PER_SEC)};
	return result;
}

NativeReturn toStringNative(int argCount, Value* args) {
	NativeReturn result = {INTERPRET_OK, NIL_VAL};

	if (IS_BOOL(args[0])) {
		if (AS_BOOL(args[0]))
			result.value = OBJ_VAL(takeString("true", 4));
		else
			result.value = OBJ_VAL(takeString("false", 5));
	}
	else if (IS_NUMBER(args[0])) {
		char str[128] = "foobar";
		double d = AS_NUMBER(args[0]);
		int i = (int)d;

		if (d == i) {
			snprintf(str, 128, "%d", i);
		}
		else {
			snprintf(str, 128, "%g", d);
		}
		result.value = OBJ_VAL(copyString(str, strlen(str)));
	}
	else if (IS_NIL(args[0])) {
		result.value = OBJ_VAL(takeString("nil", 3));
	}
	else {
		vmRuntimeError("toString accepts only numbers or booleans.");
		result.status = NATIVE_RUNTIME_ERROR;
	}

	return result;
}
