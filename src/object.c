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

static ObjString* allocateString(int length, bool dynamic) {
	ObjString* string;
	if (dynamic) {
		string = (ObjString*)allocateObject(sizeof(ObjStringDynamic) + length + 1, OBJ_STRING_DYNAMIC);
	} else {
		string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
	}
	string->length = length;
	return string;
}

ObjString* copyString(const char* chars, int length) {
	ObjStringDynamic* string = (ObjStringDynamic*)allocateString(length, true);
	memcpy(string->buffer, chars, length);
	string->buffer[length] = '\0';
	string->string.chars = string->buffer;

	return (ObjString*)string;
}

ObjString* copyStrings(StringList* list) {
	ObjStringDynamic* string = (ObjStringDynamic*)allocateString(list->totalLength, true);
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

	return (ObjString*)string;
}

ObjString* takeString(char* chars, int length) {
	ObjString* string = allocateString(length, false);
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
