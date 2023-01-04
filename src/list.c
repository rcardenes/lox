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

static NativeReturn createList(int, Value*);
static NativeReturn get(int, Value*);
static NativeReturn length(int, Value*);

static NativeDef nativeFunctions[] = {
	{ "list", 0, createList },
	{ "get", 2, get },
	{ "len", 1, length },
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

NativeReturn get(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}
	else if (!IS_INT(args[1])) {
		RET_ERROR("Expected an integer as second argument.");
	}

	ObjList* list = AS_LIST(args[0]);
	int64_t i = AS_INT(args[1]);

	if (!isValidListIndex(list, i)) {
		RET_ERROR("Invalid index %d", i);
	}

	RET_OK(indexFromList(list, i));
}

NativeReturn length(int argCount, Value* args) {
	if (!IS_OBJ(args[0]) || !IS_LIST(args[0])) {
		RET_ERROR("Expected a list as first argument.");
	}

	ObjList* list = AS_LIST(args[0]);

	RET_OK(NUMBER_VAL(list->items.count));
}
