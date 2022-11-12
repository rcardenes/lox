#ifndef vlox_table_h
#define vlox_table_h

#include "common.h"
#include "value.h"

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	int count;
	int capacity;
	Entry* entries;
} Table;

void initTable(Table*);
void freeTable(Table*);
bool tableGet(Table*, ObjString*, Value*);
bool tableSet(Table*, ObjString*, Value);
bool tableDelete(Table*, ObjString*);
void tableAddAll(Table*, Table*);
ObjString* tableFindString(Table*, const char*, int, uint32_t);

#endif // vlox_table_h
