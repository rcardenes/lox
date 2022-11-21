#ifndef vlox_object_h
#define vlox_object_h

#include "common.h"
#include "chunk.h"
#include "value.h"

#define OBJ_TYPE(value)		(AS_OBJ(value)->type)

#define IS_CLOSURE(value)	isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)	isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)	isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)	(isObjType(value, OBJ_STRING_DYNAMIC) || \
				 isObjType(value, OBJ_STRING))

#define AS_CLOSURE(value)	((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)	((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)	(((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)	((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)	(((ObjString*)AS_OBJ(value))->chars)

typedef enum {
	OBJ_CLOSURE,
	OBJ_FUNCTION,
	OBJ_NATIVE,
	OBJ_STRING,
	OBJ_STRING_DYNAMIC,
	OBJ_UPVALUE
} ObjType;

struct Obj {
	ObjType type;
	bool isMarked;
	struct Obj* next;
};

typedef struct {
	Obj obj;
	int arity;
	Chunk chunk;
	int upvalueCount;
	ObjString* name;
} ObjFunction;

typedef Value (*NativeFn)(int, Value*);

typedef struct {
	Obj obj;
	NativeFn function;
} ObjNative;

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

typedef struct ObjUpvalue {
	Obj obj;
	Value* location;
	Value closed;
	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct StringListNode {
	ObjString* string;
	struct StringListNode* next;
} StringListNode;

typedef struct StringList {
	int totalLength;
	StringListNode* first;
	StringListNode* last;
} StringList;

typedef struct {
	Obj obj;
	ObjFunction* function;
	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;

ObjClosure* newClosure(ObjFunction*);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn);
ObjString* takeString(char*, int);
ObjString* copyStrings(StringList*);
ObjString* copyString(const char*, int);
ObjUpvalue* newUpvalue(Value*);
void printObject(Value);
void initStringList(StringList*);
void addStringToList(StringList*, ObjString*);
void prependStringToList(StringList*, ObjString*);
void resetStringList(StringList*);

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif // vlox_object_h
