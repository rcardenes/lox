#ifndef vlox_table_h
#define vlox_table_h

#include "common.h"
#include "value.h"

typedef struct {
	ObjString* key;
	Value value;
	uint8_t properties;
} Entry;

typedef struct {
	int count;
	int capacity;
	Entry* entries;
} Table;

typedef enum {
	TABLE_NOPROP    = 0x00,
	TABLE_IMMUTABLE = 0x01
} TableProperty;

void initTable(Table*);
void freeTable(Table*);
bool tableGet(Table*, ObjString*, Value*);
bool tableGetProperties(Table*, ObjString*, uint8_t*);
bool tableSet(Table*, ObjString*, Value);
bool tableSetProperties(Table*, ObjString*, uint8_t);
bool tableUnsetProperties(Table*, ObjString*, uint8_t);
bool tableDelete(Table*, ObjString*);
void tableAddAll(Table*, Table*);
ObjString* tableFindString(Table*, const char*, int, uint32_t);
void tableRemoveWhite(Table*);
void markTable(Table*);

#endif // vlox_table_h
