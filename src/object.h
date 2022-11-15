#ifndef vlox_object_h
#define vlox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)		(AS_OBJ(value)->type)

#define IS_FUNCTION(value)	isObjType(value, OBJ_FUNCTION)
#define IS_STRING(value)	(isObjType(value, OBJ_STRING_DYNAMIC) || \
				 isObjType(value, OBJ_STRING))

#define AS_FUNCTION(value)	((ObjFunction*)AS_OBJ(value))
#define AS_STRING(value)	((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)	(((ObjString*)AS_OBJ(value))->chars)

typedef enum {
	OBJ_FUNCTION,
	OBJ_STRING,
	OBJ_STRING_DYNAMIC
} ObjType;

struct Obj {
	ObjType type;
	struct Obj* next;
};

typedef struct {
	Obj obj;
	int arity;
	Chunk chunk;
	ObjString* name;
} ObjFunction;

struct ObjString {
	Obj obj;
	int length;
	char* chars;
	uint32_t hash;
};

typedef struct ObjStringDynamic {
	struct ObjString string;
	char buffer[];
} ObjStringDynamic;

typedef struct StringListNode {
	ObjString* string;
	struct StringListNode* next;
} StringListNode;

typedef struct StringList {
	int totalLength;
	StringListNode* first;
	StringListNode* last;
} StringList;

ObjFunction* newFunction();
ObjString* takeString(char*, int);
ObjString* copyStrings(StringList*);
ObjString* copyString(const char*, int);
void printObject(Value);
void initStringList(StringList*);
void addStringToList(StringList*, ObjString*);
void prependStringToList(StringList*, ObjString*);
void resetStringList(StringList*);

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // vlox_object_h
