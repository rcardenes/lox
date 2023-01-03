#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "native.h"
#include "list.h"
#include "vm.h"

VM vm;

static void resetStack() {
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

void vmRuntimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;
		int line = getLine(&function->chunk, instruction);
		fprintf(stderr, "[line %d] in ", line);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack();
}

static void defineNative(NativeDef* definition) {
	push(OBJ_VAL(copyString(definition->name, (int)strlen(definition->name))));
	push(OBJ_VAL(newNative(definition->func, definition->arity)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

void initVM() {
	vm.stack = (Value*)reallocate(NULL, 0, sizeof(Value) * STACK_SLICE_SIZE);
	vm.stackLimit = vm.stack + STACK_SLICE_SIZE;
	resetStack();
	vm.objects = NULL;
	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;

	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;

	initTable(&vm.globals);
	initTable(&vm.strings);

	vm.initString = NULL;
	vm.initString = copyString("init", 4);

	miscNativeFunctions(defineNative);
	listNativeFunctions(defineNative);
}

void freeVM() {
	freeTable(&vm.globals);
	freeTable(&vm.strings);
	vm.initString = NULL;
	freeObjects();
	free(vm.stack);
}

void push(Value value) {
	if (vm.stackTop == vm.stackLimit) {
		int size = vm.stackLimit - vm.stack;
		int newSize = size + STACK_SLICE_SIZE;
		Value *newStack = GROW_ARRAY(Value, vm.stack, size, newSize);
		if (newStack != vm.stack) {
			vm.stack = newStack;
			vm.stackTop = vm.stack + size;
		}
		vm.stackLimit = vm.stack + newSize;
	}
	*vm.stackTop = value;
	vm.stackTop++;
}

Value pop() {
	vm.stackTop--;
	return *vm.stackTop;
}

static void popMany(int n) {
	vm.stackTop -= n;
}

/*
 * Inline is C99/C11 compliant
 */

static inline int readConstantIndex(CallFrame* frame) {
	// Integers used for indices, etc., can be 23 bits long, but it would
	// be a waste to use 3 bytes all the time for indices that are mostly
	// going to be fine with just 7 bits!
	//
	// Thus, we use some compression.
	if (frame == NULL)
		frame = &vm.frames[vm.frameCount - 1];

	int index = *(frame->ip++);

	if (index > 127) {
		index = (index & 0x7F) << 16;
		index |= (*frame->ip++) << 8;
		index |= (*frame->ip++);
	}

	return index;
}

static Value readConstant() {
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
	int index = readConstantIndex(frame);
	return frame->closure->function->chunk.constants.values[index];
}

static
inline Value peek(int distance) {
	return vm.stackTop[-1 - distance];
}

static bool call(ObjClosure* closure, int argCount) {
	ObjFunction* function = closure->function;

	if (argCount != function->arity) {
		vmRuntimeError("Expected %d arguments but got %d.", function->arity, argCount);
		return false;
	}

	if (vm.frameCount == FRAMES_MAX) {
		vmRuntimeError("Stack overflow.");
		return false;
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->slots = vm.stackTop - argCount - 1;
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch(OBJ_TYPE(callee)) {
			case OBJ_BOUND_METHOD: {
				ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
				vm.stackTop[-argCount - 1] =  bound->receiver;
				return call(bound->method, argCount);
			}
			case OBJ_CLASS: {
				ObjClass* klass = AS_CLASS(callee);
				vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
				if (klass->initializer != NULL) {
					return call(klass->initializer, argCount);
				}
				else if (argCount != 0) {
					vmRuntimeError("Expected 0 arguments but got %d.", argCount);
					return false;
				}
				return true;
			}
			case OBJ_CLOSURE:
				return call(AS_CLOSURE(callee), argCount);
			case OBJ_NATIVE: {
				ObjNative* native = AS_NATIVE(callee);
				if (argCount != native->arity) {
					vmRuntimeError("Expected %d arguments but got %d.", native->arity, argCount);
					return false;
				}
				NativeReturn result = native->function(argCount, vm.stackTop - argCount);
				if (result.status != NATIVE_OK) {
					return false;
				}
				popMany(argCount + 1);
				push(result.value);
				return true;
			}
			default:
				break; // Non-callable object type.
		}
	}
	vmRuntimeError("Can only call functions and classes.");
	return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		vmRuntimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
	Value receiver = peek(argCount);

	if (!IS_INSTANCE(receiver)) {
		vmRuntimeError("Only instances have methods.");
		return false;
	}

	ObjInstance* instance = AS_INSTANCE(receiver);

	Value value;
	if (tableGet(&instance->fields, name, &value)) {
		vm.stackTop[-argCount - 1] = value;
		return callValue(value, argCount);
	}

	return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		vmRuntimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop();
	push(OBJ_VAL(bound));
	return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
	ObjUpvalue* prevUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;
	while (upvalue != NULL && upvalue->location > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvalues(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

static void defineMethod(ObjString* name) {
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	{
		ObjClosure* closure = AS_CLOSURE(method);
		if (memcmp(closure->function->name->chars, "init", 4) == 0) {
			klass->initializer = closure;
		}
	}
	tableSet(&klass->methods, name, method);
	pop();
}

static
inline void replace(Value val) {
	*(vm.stackTop - 1) = val;
}

static bool isFalsey(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
	StringList sl;

	initStringList(&sl);
	addStringToList(&sl, AS_STRING(peek(0)));
	prependStringToList(&sl, AS_STRING(peek(1)));
	ObjString* result= copyStrings(&sl);
	pop();
	replace(OBJ_VAL(result));
	resetStringList(&sl);
}

typedef enum {
	ArithAdd,
	ArithSub,
	ArithMul,
	ArithDiv
} ArithOp;

static bool doArith(ArithOp op) {
	Value vb = peek(0);
	Value va = peek(1);
	Value res;

	if (!IS_NUMERIC(va) || !IS_NUMERIC(vb)) {
		vmRuntimeError("Operands must be numeric.");
		return false;
	}

	if (IS_INT(va) && IS_INT(vb)) {
		int64_t b = AS_INT(pop());
		int64_t a = AS_INT(va);
		switch (op) {
			case ArithAdd:
				res = INT_VAL(a + b);
				break;
			case ArithSub:
				res = INT_VAL(a - b);
				break;
			case ArithMul:
				res = INT_VAL(a * b);
				break;
			case ArithDiv:
				res = INT_VAL(a / b);
				break;
		}
	}
	else {
		double b = IS_INT(vb) ? AS_INT(vb) : AS_NUMBER(vb);
		double a = IS_INT(va) ? AS_INT(va) : AS_NUMBER(va);
		pop();
		switch (op) {
			case ArithAdd:
				res = NUMBER_VAL(a + b);
				break;
			case ArithSub:
				res = NUMBER_VAL(a - b);
				break;
			case ArithMul:
				res = NUMBER_VAL(a * b);
				break;
			case ArithDiv:
				res = NUMBER_VAL(a / b);
				break;
		}
	}

	replace(res);
	return true;
}

typedef enum {
	BoolGreaterThan,
	BoolLessThan
} BoolOp;

static bool doBool(BoolOp op) {
	Value vb = peek(0);
	Value va = peek(1);
	Value res;

	if (!IS_NUMERIC(va) || !IS_NUMERIC(vb)) {
		vmRuntimeError("Operands must be numeric.");
		return false;
	}

	pop();

	switch (op) {
		case BoolGreaterThan:
			res = BOOL_VAL((IS_INT(va) ? AS_INT(va) : AS_NUMBER(va)) > (IS_INT(vb) ? AS_INT(vb) : AS_NUMBER(vb)));
			break;
		case BoolLessThan:
			res = BOOL_VAL((IS_INT(va) ? AS_INT(va) : AS_NUMBER(va)) < (IS_INT(vb) ? AS_INT(vb) : AS_NUMBER(vb)));
			break;
	}

	replace(res);
	return true;
}

static InterpretResult run() {
	CallFrame* frame = &vm.frames[vm.frameCount - 1];
//	printObject(OBJ_VAL(frame->closure->function)); printf("\n");


#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
	(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_STRING() AS_STRING(readConstant())
#define BIN_BOOL(op) \
	do { \
		if (!doBool(op)) { \
			return INTERPRET_RUNTIME_ERROR; \
		} \
	} while (false)
#define BIN_ARITH(op) \
	do { \
		if (!doArith(op)) { \
			return INTERPRET_RUNTIME_ERROR; \
		} \
	} while (false)

	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		printf("          ");
		if (vm.stackTop == vm.stack) {
			printf("empty_stack");
		}
		else {
			for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
				printf("[ ");
				printValue(*slot);
				printf(" ]");
			}
		}
		printf("\n");
		disassembleInstruction(&frame->closure->function->chunk,
				(int)(frame->ip - frame->closure->function->chunk.code));
#endif
		uint8_t instruction = READ_BYTE();

		switch (instruction) {
			case OP_CONSTANT: {
					Value constant = readConstant();
					push(constant);
					break;
				}
			case OP_NIL: push(NIL_VAL); break;
			case OP_TRUE: push(BOOL_VAL(true)); break;
			case OP_FALSE: push(BOOL_VAL(false)); break;
			case OP_POP: pop(); break;
			case OP_GET_LOCAL: {
				uint8_t slot = READ_BYTE();
				push(frame->slots[slot]);
				break;
			}
			case OP_GET_GLOBAL: {
				ObjString* name = READ_STRING();
				Value value;
				if (!tableGet(&vm.globals, name, &value)) {
					vmRuntimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				push(value);
				break;
			}
			case OP_DEFINE_IGLOBAL:
			case OP_DEFINE_GLOBAL: {
				ObjString* name = READ_STRING();
				tableSet(&vm.globals, name, peek(0));
				if (instruction == OP_DEFINE_IGLOBAL) {
					tableSetProperties(&vm.globals, name, TABLE_IMMUTABLE);
				}
				pop();
				break;
			}
			case OP_SET_LOCAL: {
				uint8_t slot = READ_BYTE();
				frame->slots[slot] = peek(0);
				break;
			}
			case OP_SET_GLOBAL: {
				ObjString* name = READ_STRING();
				uint8_t properties;
				if (!tableGetProperties(&vm.globals, name, &properties)) {
					vmRuntimeError("Undefined variable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				else if (properties & TABLE_IMMUTABLE) {
					vmRuntimeError("Unable to assign a value to immutable '%s'.", name->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
				tableSet(&vm.globals, name, peek(0));
				break;
			}
			case OP_GET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				push(*frame->closure->upvalues[slot]->location);
				break;
			}
			case OP_SET_UPVALUE: {
				uint8_t slot = READ_BYTE();
				*frame->closure->upvalues[slot]->location = peek(0);
				break;
			}
			case OP_GET_PROPERTY: {
				if (!IS_INSTANCE(peek(0))) {
					vmRuntimeError("Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjInstance* instance = AS_INSTANCE(peek(0));
				ObjString* name = READ_STRING();

				Value value;
				if (tableGet(&instance->fields, name, &value)) {
					pop(); // Instance
					push(value);
					break;
				}

				if (!bindMethod(instance->klass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SET_PROPERTY: {
				if (!IS_INSTANCE(peek(1))) {
					vmRuntimeError("Only instances have properties.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjInstance* instance = AS_INSTANCE(peek(1));
				tableSet(&instance->fields, READ_STRING(), peek(0));
				Value value = pop();
				pop();
				push(value);
				break;
			}
			case OP_GET_SUPER: {
				ObjString* name = READ_STRING();
				ObjClass* superclass = AS_CLASS(pop());

				if (!bindMethod(superclass, name)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_EQUAL_NO_POP: {
				       Value b = peek(0);
				       Value a = peek(-1);
				       replace(BOOL_VAL(valuesEqual(a, b)));
				       break;
			       }
			case OP_EQUAL: {
				       Value b = pop();
				       Value a = peek(0);
				       replace(BOOL_VAL(valuesEqual(a, b)));
				       break;
			       }
			case OP_GREATER: BIN_BOOL(BoolGreaterThan); break;
			case OP_LESS: BIN_BOOL(BoolLessThan); break;
			case OP_ADD: {
				Value va = peek(1);
				Value vb = peek(0);
				if (IS_STRING(va) && IS_STRING(vb)) {
					concatenate();
				}
				else if (IS_NUMERIC(va) && IS_NUMERIC(vb)) {
					doArith(ArithAdd);
				}
				else {
					vmRuntimeError("Operands must be two numbers or two strings.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_SUBTRACT: BIN_ARITH(ArithSub); break;
			case OP_MULTIPLY: BIN_ARITH(ArithMul); break;
			case OP_DIVIDE: BIN_ARITH(ArithDiv); break;
			case OP_NOT:
				replace(BOOL_VAL(isFalsey(peek(0))));
				break;
			case OP_NEGATE: {
				Value constant = peek(0);
				if (IS_NUMBER(constant)) {
					replace(NUMBER_VAL(-AS_NUMBER(constant)));
				}
				else if (IS_INT(constant)) {
					replace(INT_VAL(-AS_INT(constant)));
				}
				else {
					vmRuntimeError("Operand must be numeric.");
					return INTERPRET_RUNTIME_ERROR;
				}
				break;
			}
			case OP_PRINT: {
				printValue(pop());
				printf("\n");
				break;
			}
			case OP_JUMP: {
				uint16_t offset = READ_SHORT();
				frame->ip += offset;
				break;
			}
			case OP_JUMP_IF_FALSE: {
				uint16_t offset = READ_SHORT();
				if (isFalsey(peek(0))) frame->ip += offset;
				break;
			}
			case OP_LOOP: {
				uint16_t offset = READ_SHORT();
				frame->ip -= offset;
				break;
			}
			case OP_CALL: {
				int argCount = READ_BYTE();
				if (!callValue(peek(argCount), argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}
			case OP_INVOKE: {
				ObjString* method = READ_STRING();
				int argCount = READ_BYTE();
				if (!invoke(method, argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}
			case OP_SUPER_INVOKE: {
				ObjString* method = READ_STRING();
				int argCount = READ_BYTE();
				ObjClass* superclass = AS_CLASS(pop());
				if (!invokeFromClass(superclass, method, argCount)) {
					return INTERPRET_RUNTIME_ERROR;
				}
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}
			case OP_CLOSURE: {
				ObjFunction* function = AS_FUNCTION(readConstant());
				ObjClosure* closure = newClosure(function);
				push(OBJ_VAL(closure));
				for (int i = 0; i < closure->upvalueCount; i++) {
					uint8_t isLocal = READ_BYTE();
					uint8_t index = READ_BYTE();
					if (isLocal) {
						closure->upvalues[i] = captureUpvalue(frame->slots + index);
					}
					else {
						closure->upvalues[i] = frame->closure->upvalues[index];
					}
				}
				break;
			}
			case OP_CLOSE_UPVALUE:
				closeUpvalues(vm.stackTop - 1);
				pop();
				break;
			case OP_RETURN: {
				Value result = pop();
				closeUpvalues(frame->slots);
				vm.frameCount--;
				if (vm.frameCount == 0) {
					pop();
					return INTERPRET_OK;
				}

				vm.stackTop = frame->slots;
				push(result);
				frame = &vm.frames[vm.frameCount - 1];
				break;
			}
			case OP_CLASS:
				push(OBJ_VAL(newClass(READ_STRING())));
				break;
			case OP_INHERIT: {
				Value superclass = peek(1);

				if (!IS_CLASS(superclass)) {
					vmRuntimeError("Superclass must be a class.");
					return INTERPRET_RUNTIME_ERROR;
				}
				ObjClass* subclass = AS_CLASS(peek(0));
				tableAddAll(&AS_CLASS(superclass)->methods,
					    &subclass->methods);
				pop(); // Subclass.
				break;
			}
			case OP_METHOD:
				defineMethod(READ_STRING());
				break;
			case OP_BUILD_LIST: {
				ObjList* list = newList();
				int count = readConstantIndex(NULL);

				// This is to ensure that the object is not sweeped while building it.
				push(OBJ_VAL(list));
				for (int i = count; i > 0; i--) {
					appendToList(list, peek(i));
				}
				popMany(count + 1);

				push(OBJ_VAL(list));
				break;
			}
			case OP_APPEND_TO: {
				Value element = pop();
				Value vList = pop();

				if (!IS_OBJ(vList) || !IS_LIST(vList)) {
					vmRuntimeError("Can only append to a list.");
					return INTERPRET_RUNTIME_ERROR;
				}

				appendToList(AS_LIST(vList), element);
				break;
			}
			case OP_DELETE_FROM: {
				Value vIndex = pop();
				Value vList = pop();

				if (!IS_INT(vIndex)) {
					vmRuntimeError("Indices can only be integers.");
					return INTERPRET_RUNTIME_ERROR;
				}

				int64_t index = AS_INT(vIndex);

				if (!IS_OBJ(vList) || !IS_LIST(vList)) {
					vmRuntimeError("Can only delete from a list.");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjList* list = AS_LIST(vList);

				if (!isValidListIndex(list, index)) {
					vmRuntimeError("Not a valid index: %d.", index);
					return INTERPRET_RUNTIME_ERROR;
				}

				deleteFromList(list, index);
				break;
			}
			case OP_INDEX_SUBSCR:
			{
				Value vIndex = pop();
				Value vIndexed = pop();
				Value result;

				if (!IS_INT(vIndex)) {
					vmRuntimeError("Index is not an integer");
					return INTERPRET_RUNTIME_ERROR;
				}

				int index = AS_INT(vIndex);

				if (IS_LIST(vIndexed)) {
					ObjList* list = AS_LIST(vIndexed);

					if (!isValidListIndex(list, index)) {
						vmRuntimeError("List index %d is out of range.", index);
						return INTERPRET_RUNTIME_ERROR;
					}

					result = indexFromList(list, index);
				}
				else if (IS_STRING(vIndexed)) {
					ObjString* string = AS_STRING(vIndexed);

					if (!isValidStringIndex(string, index)) {
						vmRuntimeError("List index %d is out of range.", index);
						return INTERPRET_RUNTIME_ERROR;
					}

					result = indexFromString(string, index);
				}
				else {
					vmRuntimeError("Invalid type to index into.");
					return INTERPRET_RUNTIME_ERROR;
				}

				push(result);
				break;
			}
			case OP_STORE_SUBSCR:
			{
				Value item = pop();
				Value vIndex = pop();
				Value vList = pop();

				if (!IS_LIST(vList)) {
					vmRuntimeError("Invalid type to index into.");
					return INTERPRET_RUNTIME_ERROR;
				}

				if (!IS_INT(vIndex)) {
					vmRuntimeError("List index is not an integer");
					return INTERPRET_RUNTIME_ERROR;
				}

				ObjList* list = AS_LIST(vList);
				int index = AS_INT(vIndex);

				if (!isValidListIndex(list, index)) {
					vmRuntimeError("List index %d is out of range.", index);
					return INTERPRET_RUNTIME_ERROR;
				}

				storeToList(list, index, item);
				push(item);
				break;
			}
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_STRING
#undef READ_LONG_CONSTANT
#undef BIN_BOOL
#undef BIN_ARITH
}

InterpretResult interpret(const char* source) {
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILE_ERROR;

	push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	call(closure, 0);

	return run();
}
