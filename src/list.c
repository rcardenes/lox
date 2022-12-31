#include "list.h"
#include "object.h"
#include "vm.h"

#define RET_ERROR(...) \
	{\
		vmRuntimeError(__VA_ARGS__);\
		NativeReturn result = { INTERPRET_RUNTIME_ERROR, NIL_VAL };\
		return result;\
	}

#define RET_OK(val) \
	{\
		NativeReturn result = { INTERPRET_OK, val }; \
		return result; \
	}

static bool isInteger(Value* val) {
	if (!IS_NUMBER(*val))
		return false;

	double d = AS_NUMBER(*val);
	return d == ((int)d);
}

static NativeReturn createList(int, Value*);
static NativeReturn append(int, Value*);
static NativeReturn get(int, Value*);
static NativeReturn delete(int, Value*);
static NativeReturn length(int, Value*);
static NativeReturn slice(int, Value*);

static NativeDef nativeFunctions[] = {
	{ "list", 0, createList },
	{ "append", 2, append },
	{ "get", 2, get },
	{ "delete", 2, delete },
	{ "len", 1, length },
	{ "slice", 4, slice },
	{ NULL, -1, NULL }
};

void listNativeFunctions(RegisterNative addToRegistry) {
	NativeDef* current = &nativeFunctions[0];

	while (current->name != NULL) {
		addToRegistry(current++);
	}
}

NativeReturn createList(int argCount, Value* args) {
	ObjList* list = newList();

	RET_OK(OBJ_VAL(list));
}

NativeReturn append(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}

	appendToList(AS_LIST(args[0]), args[1]);

	RET_OK(NIL_VAL);
}

NativeReturn get(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}
	else if (!isInteger(&args[1]) || AS_NUMBER(args[1]) < 0) {
		RET_ERROR("Expected a non-negative integer as second argument.");
	}

	ObjList* list = AS_LIST(args[0]);
	int i = (int)AS_NUMBER(args[1]);

	if (!isValidListIndex(list, i)) {
		RET_ERROR("Invalid index %d", i);
	}

	RET_OK(indexFromList(list, i));
}

NativeReturn delete(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}
	else if (!isInteger(&args[1]) || AS_NUMBER(args[1]) < 0) {
		RET_ERROR("Expected a non-negative integer as second argument.");
	}

	ObjList* list = AS_LIST(args[0]);
	int i = (int)AS_NUMBER(args[1]);

	if (!isValidListIndex(list, i)) {
		RET_ERROR("Invalid index %d", i);
	}

	Value v = list->items.values[i];
	deleteFromList(list, i);

	RET_OK(v);
}

NativeReturn length(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}

	ObjList* list = AS_LIST(args[0]);

	RET_OK(NUMBER_VAL(list->items.count));
}

NativeReturn slice(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}
	else if (!isInteger(&args[1]) || AS_NUMBER(args[1]) < 0) {
		RET_ERROR("Expected a non-negative integer as second argument.");
	}
	else if (!isInteger(&args[2]) || AS_NUMBER(args[2]) < 0) {
		RET_ERROR("Expected a non-negative integer as third argument.");
	}
	else if (!isInteger(&args[3]) || AS_NUMBER(args[3]) < 1) {
		RET_ERROR("Expected a positive integer as fourth argument.");
	}

	ObjList* list = AS_LIST(args[0]);
	int start = (int)AS_NUMBER(args[1]);
	int stop = (int)AS_NUMBER(args[2]);
	int step = (int)AS_NUMBER(args[3]);
	int len = list->items.count;

	if (stop >= len) {
		stop = len;
	}

	RET_OK(OBJ_VAL(sliceFromList(list, start, stop, step)));
}
