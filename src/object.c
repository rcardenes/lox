#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
	Obj* object = (Obj*)reallocate(NULL, 0, size);
	object->type = type;
	object->isMarked = false;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void*)object, size, type);
#endif
	return object;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method) {
	ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	bound->receiver = receiver;
	bound->method = method;
	return bound;
}

ObjClass* newClass(ObjString* name) {
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->initializer = NULL;
	initTable(&klass->methods);
	return klass;
}

ObjClosure* newClosure(ObjFunction* function) {
	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int i = 0; i < function->upvalueCount; i++) {
		upvalues[i] = NULL;
	}

	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjFunction* newFunction() {
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjInstance* newInstance(ObjClass* klass) {
	ObjInstance* instance =  ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjNative* newNative(NativeFn function) {
	ObjNative* native =  ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	return native;
}

static ObjString* allocateString(int length, uint32_t hash, bool dynamic) {
	ObjString* string;
	if (dynamic) {
		string = (ObjString*)allocateObject(sizeof(ObjStringDynamic) + length + 1, OBJ_STRING_DYNAMIC);
	} else {
		string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
	}
	string->length = length;
	string->hash = hash;
	return string;
}

uint32_t hashString(const char* key, int length) {
	uint32_t hash = 2166136261u;
	const char* current = key;
	for (int i = 0; i < length; i++, current++) {
		hash ^= (uint8_t)*current;
		hash *= 16777619;
	}

	return hash;
}

ObjString* copyString(const char* chars, int length) {
	uint32_t hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);

	if (interned != NULL) return interned;

	ObjStringDynamic* string = (ObjStringDynamic*)allocateString(length, hash, true);
	memcpy(string->buffer, chars, length);
	string->buffer[length] = '\0';
	string->string.chars = string->buffer;
	push(OBJ_VAL(string)); // Temporarily push to the stack so that the GC won't reclaim us.
	tableSet(&vm.strings, (ObjString*)string, NIL_VAL);
	pop();

	return (ObjString*)string;
}

ObjString* copyStrings(StringList* list) {
	int length = list->totalLength;

	ObjStringDynamic* string = (ObjStringDynamic*)allocateString(length, 0, true);
	string->string.chars = &string->buffer[0];
	char* dest = string->buffer;
	StringListNode* current = list->first;
	while (current != NULL) {
		ObjString* origString = current->string;
		memcpy(dest, origString->chars, origString->length);
		dest += origString->length;
		current = current->next;
	}
	*dest = '\0';
	uint32_t hash = hashString(string->buffer, length);

	ObjString* interned = tableFindString(&vm.strings, string->buffer, length, hash);

	if (interned != NULL) {
		FREE_VARIABLE(ObjStringDynamic, length + 1, string);
		return interned;
	}

	((ObjString*)string)->hash = hash;
	push(OBJ_VAL(string)); // Temporarily push to the stack so that the GC won't reclaim us.
	tableSet(&vm.strings, (ObjString*)string, NIL_VAL);
	pop();

	return (ObjString*)string;
}

ObjUpvalue* newUpvalue(Value* slot) {
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	upvalue->closed = NIL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

static void printFunction(ObjFunction* function) {
	if (!function->name) {
		printf("<script>");
		return;
	}
	printf("<fn %s>", function->name->chars);
}

ObjString* takeString(char* chars, int length) {
	uint32_t hash = hashString(chars, length);
	ObjString* interned = tableFindString(&vm.strings, chars, length, hash);

	if (interned != NULL) {
		// NOTE: In the original implementation this function would take
		//       OWNERSHIP of the string, and so it would free the new
		//       string a this spot. It doesn't any longer.
		return interned;
	}

	ObjString* string = allocateString(length, hash, false);
	string->chars = chars;
	push(OBJ_VAL(string)); // Temporarily push to the stack so that the GC won't reclaim us.
	tableSet(&vm.strings, (ObjString*)string, NIL_VAL);
	pop();

	return string;
}

void printObject(Value value) {
	switch(OBJ_TYPE(value)) {
		case OBJ_BOUND_METHOD:
			printFunction(AS_BOUND_METHOD(value)->method->function);
			break;
		case OBJ_CLASS:
			printf("<%s class>", AS_CLASS(value)->name->chars);
			break;
		case OBJ_CLOSURE:
			printFunction(AS_CLOSURE(value)->function);
			break;
		case OBJ_FUNCTION:
			printFunction(AS_FUNCTION(value));
			break;
		case OBJ_INSTANCE:
			printf("<%s instance>", AS_INSTANCE(value)->klass->name->chars);
			break;
		case OBJ_NATIVE:
			printf("<native fn>");
			break;
		case OBJ_STRING:
		case OBJ_STRING_DYNAMIC:
			printf("%s", AS_CSTRING(value));
			break;
		case OBJ_UPVALUE:
			printf("upvalue");
			break;
	}
}

void initStringList(StringList* list) {
	list->totalLength = 0;
	list->first = NULL;
	list->last = NULL;
}

void addStringToList(StringList* list, ObjString* string) {
	StringListNode* node = ALLOCATE(StringListNode, 1);
	node->string = string;
	node->next = NULL;

	if (!list->first) {
		list->first = node;
	}
	else {
		list->last->next = node;
	}
	list->last = node;
	list->totalLength += string->length;
}

void prependStringToList(StringList* list, ObjString* string) {
	StringListNode* node = ALLOCATE(StringListNode, 1);
	node->string = string;
	node->next = list->first;
	list->first = node;

	if (!list->last) {
		list->last = node;
	}
	list->totalLength += string->length;
}

void resetStringList(StringList* list) {
	StringListNode* current = list->first;
	while (current != NULL) {
		StringListNode* next = current->next;
		FREE(StringListNode, current);
		current = next;
	}
	initStringList(list);
}
