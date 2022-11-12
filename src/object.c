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

	object->next = vm.objects;
	vm.objects = object;
	return object;
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

	return (ObjString*)string;
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

	return string;
}

void printObject(Value value) {
	switch(OBJ_TYPE(value)) {
		case OBJ_STRING:
		case OBJ_STRING_DYNAMIC:
			printf("%s", AS_CSTRING(value));
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

void resetStringList(StringList* list) {
	StringListNode* current = list->first;
	while (current != NULL) {
		StringListNode* next = current->next;
		FREE(StringListNode, current);
		current = next;
	}
	initStringList(list);
}
