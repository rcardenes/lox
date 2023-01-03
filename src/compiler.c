#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
} Parser;

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT,
	PREC_TERNARY,
	PREC_OR,
	PREC_AND,
	PREC_EQUALITY,
	PREC_COMPARISON,
	PREC_TERM,
	PREC_FACTOR,
	PREC_UNARY,
	PREC_CALL,
	PREC_SUBSCRIPT,
	PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool);

typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
	bool isMutable;
} Local;

typedef struct {
	uint8_t index;
	bool isLocal;
} Upvalue;

typedef enum {
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT
} FunctionType;

typedef enum {
	BREAK,
	CONTINUE
} LoopJumpType;

typedef struct LoopJump {
	struct LoopJump *next;
	LoopJumpType type;
	int index;
} LoopJump;

typedef struct LoopContext {
	struct LoopContext* outer;
	LoopJump* jumps;
	int start;
	int depth;
} LoopContext;

typedef struct Compiler {
	struct Compiler* enclosing;
	ObjFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;
	LoopContext* currentLoop;
} Compiler;

typedef struct ClassCompiler {
	struct ClassCompiler* enclosing;
	bool hasSuperClass;
} ClassCompiler;

Parser parser;
Compiler* current;
ClassCompiler* currentClass = NULL;

static Chunk* currentChunk() {
	return &current->function->chunk;
}

void printToken(Token* token) {
	char str[token->length + 1];
	memcpy(str, token->start, token->length);
	str[token->length] = 0;
	puts(str);
}

static void errorAt(Token* token, const char* message) {
	if (parser.panicMode) return;
	parser.panicMode = true;
	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR) {
		// Nothing
	}
	else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

static void error(const char* message) {
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
	errorAt(&parser.current, message);
}

static void advance() {
	parser.previous = parser.current;

	for (;;) {
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR) break;

		errorAtCurrent(parser.current.start);
	}
}

static void consume(TokenType type, const char* message) {
	if (parser.current.type == type) {
		advance();
		return;
	}

	errorAtCurrent(message);
}

static bool check(TokenType type) {
	return parser.current.type == type;
}

static bool match(TokenType type) {
	if (!check(type)) return false;
	advance();
	return true;
}

static void emitByte(uint8_t byte) {
	writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
	emitByte(byte1);
	emitByte(byte2);
}

static void emitLoop(int loopStart) {
	emitByte(OP_LOOP);

	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
	emitByte(instruction);
	emitBytes(0xff, 0xff);
	return currentChunk()->count - 2;
}

static void emitReturn() {
	if (current->type == TYPE_INITIALIZER) {
		emitBytes(OP_GET_LOCAL, 0);
	}
	else {
		emitByte(OP_NIL);
	}

	emitByte(OP_RETURN);
}

static int makeConstant(Value value) {
	return addConstant(currentChunk(), value);
}

static void emitConstantBytes(OpCode opCode, int constant) {
	writeConstant(currentChunk(), opCode, constant, parser.previous.line);
}

static void emitConstant(Value value) {
	emitConstantBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset) {
	// - 2 to adjust for the bytecode for the jump offset itself.
	int jump = currentChunk()->count - offset - 2;

	if (jump > UINT16_MAX) {
		error("Too much code to jump over.");
	}

	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
	compiler->enclosing = current;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->function = newFunction();
	current = compiler;
	if (type != TYPE_SCRIPT) {
		current->function->name = copyString(parser.previous.start,
						     parser.previous.length);
	}

	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->isMutable = false;
	if (type != TYPE_FUNCTION) {
		local->name.start = "this";
		local->name.length = 4;
	}
	else {
		local->name.start = "";
		local->name.length = 0;
	}
}

static ObjFunction* endCompiler() {
	emitReturn();
	ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassembleChunk(currentChunk(),
				function->name != NULL ? function->name->chars : "code");
	}
#endif

	current = current->enclosing;
	return function;
}

static void beginScope() {
	current->scopeDepth++;
}

static void endScope() {
	current->scopeDepth--;

	while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
		if (current->locals[current->localCount - 1].isCaptured) {
			emitByte(OP_CLOSE_UPVALUE);
		}
		else {
			emitByte(OP_POP);
		}
		current->localCount--;
	}
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType);
static void parsePrecedence(Precedence);

static int identifierConstant(Token* name) {
	return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name, bool* isMutable) {
	for (int i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Can't read local variable in its own initializer.");
			}
			if (isMutable)
				*isMutable = local->isMutable;
			return i;
		}
	}

	return -1;
}

static int addUpvalue(Compiler* compiler, int index, bool isLocal) {
	int upvalueCount = compiler->function->upvalueCount;

	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name, bool* isMutable) {
	if (compiler->enclosing == NULL) return -1;

	int local = resolveLocal(compiler->enclosing, name, isMutable);
	if (local != -1) {
		compiler->enclosing->locals[local].isCaptured = true;
		return addUpvalue(compiler, local, true);
	}

	int upvalue = resolveUpvalue(compiler->enclosing, name, isMutable);
	if (upvalue != -1) {
		return addUpvalue(compiler, upvalue, false);
	}

	return -1;
}

static void addLocal(Token name, bool isMutable) {
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}
	Local* local = &current->locals[current->localCount++];
	local->name = name;
	// local->depth = current->scopeDepth;
	local->depth = -1;
	local->isCaptured = false;
	local->isMutable = isMutable;
}

static void declareVariable(bool isMutable) {
	if (current->scopeDepth == 0) return;

	Token* name = &parser.previous;
	for (int i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}

		if (identifiersEqual(name, &local->name)) {
			error("Already a variable with this name in this scope.");
		}
	}
	addLocal(*name, isMutable);
}

static int parseVariable(const char* errorMessage, bool isMutable) {
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable(isMutable);
	if (current->scopeDepth > 0) {
		return 0;
	}

	return identifierConstant(&parser.previous);
}

static void markInitialized() {
	if (current->scopeDepth == 0) return;
	current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(int global, bool isMutable) {
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}

	emitConstantBytes(isMutable ? OP_DEFINE_GLOBAL : OP_DEFINE_IGLOBAL, global);
}

static uint8_t argumentList() {
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			expression();
			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}
			argCount++;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void and_(bool) {
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void binary(bool) {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType) {
		case TOKEN_BANG_EQUAL:	  emitBytes(OP_EQUAL, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL:	  emitByte(OP_EQUAL); break;
		case TOKEN_GREATER:	  emitByte(OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
		case TOKEN_LESS:	  emitByte(OP_LESS); break;
		case TOKEN_LESS_EQUAL:	  emitBytes(OP_GREATER, OP_NOT); break;
		case TOKEN_PLUS:	  emitByte(OP_ADD); break;
		case TOKEN_MINUS:	  emitByte(OP_SUBTRACT); break;
		case TOKEN_STAR:	  emitByte(OP_MULTIPLY); break;
		case TOKEN_SLASH:	  emitByte(OP_DIVIDE); break;
		default:
			return;
	}
}

static void call(bool) {
	uint8_t argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint32_t name = identifierConstant(&parser.previous);

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(OP_SET_PROPERTY, name);
	}
	else if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		emitBytes(OP_INVOKE, name);
		emitByte(argCount);
	}
	else {
		emitBytes(OP_GET_PROPERTY, name);
	}
}

static void literal(bool) {
	switch (parser.previous.type) {
		case TOKEN_FALSE: emitByte(OP_FALSE); break;
		case TOKEN_NIL: emitByte(OP_NIL); break;
		case TOKEN_TRUE: emitByte(OP_TRUE); break;
		default: return; // Unreachable
	}
}

static void grouping(bool) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void integer(bool) {
	int64_t value;
	int l = parser.previous.length;
	bool starts_with_zero = parser.previous.start[0] == '0';

	if (l > 2 && starts_with_zero && parser.previous.start[1] == 'x') {
		value = strtoll(&parser.previous.start[2], NULL, 16);
	} else if (l > 2 && starts_with_zero && parser.previous.start[1] == 'o') {
		value = strtoll(&parser.previous.start[2], NULL, 8);
	} else {
		value = strtoll(parser.previous.start, NULL, 10);
	}
	emitConstant(INT_VAL(value));
}

static void number(bool) {
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

static void or_(bool) {
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

static void string(bool) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
					parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
	uint8_t getOp, setOp;
	char nameChars[name.length + 1];
	bool isMutable = true;
	int arg = resolveLocal(current, &name, &isMutable);

	snprintf(nameChars, name.length + 1, "%s", name.start);

	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else if ((arg = resolveUpvalue(current, &name, &isMutable)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	else {
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		if (!isMutable) {
			error("Can't assign to immutable declaration.");
		}
		expression();
		emitConstantBytes(setOp, arg);
	}
	else {
		emitConstantBytes(getOp, arg);
	}
}

static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text) {
	Token token;
	token.start = text;
	token.length = (int)strlen(text);
	return token;
}

static void super_(bool) {
	if (currentClass == NULL) {
		error("Can't use 'super' outside of a class.");
	}
	else if (!currentClass->hasSuperClass) {
		error("Can't use 'super' in a class with no superclass.");
	}

	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint32_t name = identifierConstant(&parser.previous);

	namedVariable(syntheticToken("this"), false);
	if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_SUPER_INVOKE, name);
		emitByte(argCount);
	}
}

static void this_(bool) {
	if (currentClass == NULL) {
		error("Can't use 'this' outside of a class.");
		return;
		return;
	}

	variable(false);
}

static void unary(bool) {
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	parsePrecedence(PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
		case TOKEN_BANG: emitByte(OP_NOT); break;
		case TOKEN_MINUS: emitByte(OP_NEGATE); break;
		default: return; // Unreachable
	}
}

static void ternary(bool) {
	int midJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_TERNARY - 1);
	int exitJump = emitJump(OP_JUMP);

	consume(TOKEN_COLON, "Expect ':' after first ternary expression.");

	patchJump(midJump);
	parsePrecedence(PREC_TERNARY - 1);
	patchJump(exitJump);
}

static void list(bool) {
	int itemCount = 0;

	if (!check(TOKEN_RIGHT_BRACKET)) {
		do {
			if (check(TOKEN_RIGHT_BRACKET)) {
				// We found a case of trailing comma
				break;
			}

			parsePrecedence(PREC_TERNARY);
			itemCount++;
		} while(match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after list literal.");

	emitConstantBytes(OP_BUILD_LIST, itemCount);
}

static void subscript(bool canAssign) {
	parsePrecedence(PREC_TERNARY);
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitByte(OP_STORE_SUBSCR);
	}
	else {
		emitByte(OP_INDEX_SUBSCR);
	}
}

static ParseRule rules[] = {
	[TOKEN_LEFT_PAREN]	= {grouping,	call,		PREC_CALL},
	[TOKEN_RIGHT_PAREN]	= {NULL,	NULL,		PREC_NONE},
	[TOKEN_LEFT_BRACE]	= {NULL,	NULL,		PREC_NONE},
	[TOKEN_RIGHT_BRACE]	= {NULL,	NULL,		PREC_NONE},
	[TOKEN_LEFT_BRACKET]	= {list,	subscript,	PREC_SUBSCRIPT},
	[TOKEN_RIGHT_BRACKET]	= {NULL,	NULL,		PREC_NONE},
	[TOKEN_COMMA] 		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_DOT]		= {NULL,	dot,		PREC_CALL},
	[TOKEN_MINUS]		= {unary,	binary,		PREC_TERM},
	[TOKEN_PLUS]		= {NULL,	binary,		PREC_TERM},
	[TOKEN_SEMICOLON]	= {NULL,	NULL,		PREC_NONE},
	[TOKEN_SLASH]		= {NULL,	binary,		PREC_FACTOR},
	[TOKEN_STAR]		= {NULL,	binary,		PREC_FACTOR},
	[TOKEN_BANG]		= {unary,	NULL,		PREC_NONE},
	[TOKEN_BANG_EQUAL]	= {NULL,	binary,		PREC_EQUALITY},
	[TOKEN_EQUAL]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_EQUAL_EQUAL]	= {NULL,	binary,		PREC_EQUALITY},
	[TOKEN_GREATER]		= {NULL,	binary,		PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL]	= {NULL,	binary,		PREC_COMPARISON},
	[TOKEN_LESS]		= {NULL,	binary,		PREC_COMPARISON},
	[TOKEN_LESS_EQUAL]	= {NULL,	binary,		PREC_COMPARISON},
	[TOKEN_IDENTIFIER]	= {variable,	NULL,		PREC_NONE},
	[TOKEN_STRING]		= {string,	NULL,		PREC_NONE},
	[TOKEN_INTEGER]		= {integer,	NULL,		PREC_NONE},
	[TOKEN_NUMBER]		= {number,	NULL,		PREC_NONE},
	[TOKEN_AND]		= {NULL,	and_,		PREC_AND},
	[TOKEN_CLASS]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_ELSE]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_FALSE]		= {literal,	NULL,		PREC_NONE},
	[TOKEN_FOR]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_FUN]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_IF]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_NIL]		= {literal,	NULL,		PREC_NONE},
	[TOKEN_OR]		= {NULL,	or_,		PREC_OR},
	[TOKEN_PRINT]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_QUESTION_MARK]	= {NULL,	ternary,	PREC_TERNARY},
	[TOKEN_RETURN]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_SWITCH]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_SUPER]		= {super_,	NULL,		PREC_NONE},
	[TOKEN_THIS]		= {this_,	NULL,		PREC_NONE},
	[TOKEN_TRUE]		= {literal,	NULL,		PREC_NONE},
	[TOKEN_VAR]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_VAL]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_WHILE]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_ERROR]		= {NULL,	NULL,		PREC_NONE},
	[TOKEN_EOF]		= {NULL,	NULL,		PREC_NONE}
};

void parsePrecedence(Precedence precedence) {
	advance();
	ParseFn prefixRule = getRule(parser.previous.type)->prefix;
	if (prefixRule == NULL) {
		error("Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence) {
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		printf("Invalid assignment target.\n");
	}
}

ParseRule* getRule(TokenType type) {
	return &rules[type];
}

void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect ')' after block.");
}

static void function(FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			current->function->arity++;
			if (current->function->arity > 255) {
				errorAtCurrent("Can't have more than 255 parameters.");
			}
			int constant = parseVariable("Expect parameter name.", true);
			defineVariable(constant, true);
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body,");
	block();

	ObjFunction* function = endCompiler();
	emitConstantBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	for (int i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

static void method() {
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint32_t constant = identifierConstant(&parser.previous);

	FunctionType type = TYPE_METHOD;
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
		type = TYPE_INITIALIZER;
	}
	function(type);
	emitConstantBytes(OP_METHOD, constant);
}

static void classDeclaration() {
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.previous;
	uint32_t nameConstant = identifierConstant(&parser.previous);
	declareVariable(true);

	emitConstantBytes(OP_CLASS, nameConstant);
	defineVariable(nameConstant, true);

	ClassCompiler classCompiler;
	classCompiler.enclosing = currentClass;
	classCompiler.hasSuperClass = false;
	currentClass = &classCompiler;

	if (match(TOKEN_LESS)) {
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(false);

		if (identifiersEqual(&className, &parser.previous)) {
			error("A class can't inherit from itself.");
		}

		beginScope();
		addLocal(syntheticToken("super"), false);
		defineVariable(0, false);

		namedVariable(className, false);
		emitByte(OP_INHERIT);
		classCompiler.hasSuperClass = true;
	}

	namedVariable(className, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(OP_POP);

	if (classCompiler.hasSuperClass) {
		endScope();
	}

	currentClass = currentClass->enclosing;
}

static void funDeclaration() {
	int global = parseVariable("Expect function name.", true);
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global, true);
}

static void varDeclaration(bool isMutable) {
	int global = parseVariable("Expect variable name.", isMutable);

	if (match(TOKEN_EQUAL)) {
		expression();
	}
	else if (isMutable) {
		error("Immutable objects need to be assigned at declaration.");
	}
	else {
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(global, isMutable);
}

static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

static void emitLoopJump(LoopJumpType type) {
	LoopContext* context = current->currentLoop;

	if (current->localCount > 0) {
		for (int i = current->localCount - 1; i >= 0 && (current->locals[i].depth > context->depth); i--) {
			emitByte(OP_POP);
		}
	}

	if (type == BREAK) {
		LoopJump* jump = ALLOCATE(LoopJump, 1);
		jump->next = context->jumps;
		jump->type = type;
		jump->index = emitJump(OP_JUMP);

		context->jumps = jump;
	}
	else {
		emitLoop(context->start);
	}
}

static void appendStatement() {
	expression();
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after append expression.");
	emitByte(OP_APPEND_TO);
}

static void deleteStatement() {
	if (!match(TOKEN_IDENTIFIER)) {
		error("Expected an identifier after delete.");
	}
	variable(false);
	consume(TOKEN_LEFT_BRACKET, "Expect '[' after identifier.");
	expression();
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index expression.");
	consume(TOKEN_SEMICOLON, "Expect ';' after delete expression.");
	emitByte(OP_DELETE_FROM);
}

static void breakStatement() {
	consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");

	if (!current->currentLoop) {
		error("Found 'break' outside a loop.");
	}
	else {
		emitLoopJump(BREAK);
	}
}

static void continueStatement() {
	consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

	if (!current->currentLoop) {
		error("Found 'continue' outside a loop.");
	}
	else {
		emitLoopJump(CONTINUE);
	}
}

static void beginLoop(int loopStart) {
	LoopContext* context = ALLOCATE(LoopContext, 1);
	context->outer = current->currentLoop;
	context->jumps = NULL;
	context->start = loopStart;
	context->depth = current->scopeDepth;
	current->currentLoop = context;
}

static void endLoop() {
	LoopContext* context = current->currentLoop;

	if (context != NULL) { // Shouldn't happen, but just in case...
		LoopJump* currentJump = context->jumps;
		while (currentJump != NULL) {
			if (currentJump->type == BREAK) {
				patchJump(currentJump->index);
			}
			LoopJump* next = currentJump->next;
			FREE(LoopJump, currentJump);
			currentJump = next;
		}
		current->currentLoop = context->outer;
		FREE(LoopContext, context);
	}
}

static void forStatement() {
	beginScope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	if (match(TOKEN_SEMICOLON)) {
		// Do nothing, empty initializer.
	}
	else if (match(TOKEN_VAR)) {
		varDeclaration(true);
	}
	else {
		expressionStatement();
	}

	int loopStart = currentChunk()->count;
	int exitJump = -1;
	if (!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // Condition
	}

	if (!match(TOKEN_RIGHT_PAREN)) {
		int bodyJump = emitJump(OP_JUMP);
		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		emitLoop(loopStart);
		loopStart = incrementStart;
		patchJump(bodyJump);
	}

	beginLoop(loopStart);
	statement();
	emitLoop(loopStart);

	endLoop();
	if (exitJump != -1) {
		patchJump(exitJump);
		emitByte(OP_POP);
	}
	endScope();
}

static void ifStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();

	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE)) statement();
	patchJump(elseJump);
}

void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

void returnStatement() {
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}

	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	}
	else {
		if (current->type == TYPE_INITIALIZER) {
			error("Can't return a value from an initializer.");
		}

		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

static void switchStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' after switch expression.");

	int nCases = 0;
	int cases[UINT8_MAX];
	for (int i = 0; i < UINT8_MAX; i++) {
		cases[i] = -1;
	}

	bool inCase = false;
	bool inDefault = false;
	while (!match(TOKEN_RIGHT_BRACE)) {
		if (match(TOKEN_CASE)) {
			inCase = true;
			if (inDefault) {
				error("Unexpected 'case' after 'default'.");
			}
			else if (nCases >= UINT8_MAX) {
				error("More than 256 case clauses in a switch are not allowed.");
			}
			else if (nCases > 0) {
				int jumpHere = cases[nCases - 1];
				cases[nCases - 1] = emitJump(OP_JUMP);
				patchJump(jumpHere);
				emitByte(OP_POP);
			}

			expression();
			consume(TOKEN_COLON, "Expected ':' after case expression.");
			emitByte(OP_EQUAL_NO_POP);
			cases[nCases++] = emitJump(OP_JUMP_IF_FALSE);
			emitByte(OP_POP);
		}
		else if (match(TOKEN_DEFAULT)) {
			inCase = true;
			if (inDefault) {
				error("Duplicate 'default'.");
			}
			consume(TOKEN_COLON, "Expected ':' after 'default'.");
			if (nCases > 0) {
				int jumpHere = cases[nCases - 1];
				cases[nCases - 1] = emitJump(OP_JUMP);
				patchJump(jumpHere);
				emitByte(OP_POP);
			}
		}
		else {
			if (!inCase) {
				error("Code outside 'case' or 'default' clauses.");
			}
			statement();
		}
	}

	if (nCases > 0) {
		for (int i = 0; i < nCases; i++) {
			patchJump(cases[i]);
		}
		if (!inDefault) {
			emitByte(OP_POP);
		}
	}
	emitByte(OP_POP);
}

void whileStatement() {
	int loopStart = currentChunk()->count;
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	beginLoop(loopStart);

	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();
	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP);
	endLoop();
}

void synchronize() {
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF) {
		if (parser.previous.type == TOKEN_SEMICOLON) return;
		switch (parser.current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUN:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_PRINT:
			case TOKEN_RETURN:
			case TOKEN_SWITCH:
				return;

			default:
				; // Do nothing;
		}

		advance();
	}
}

void statement() {
	if (match(TOKEN_PRINT)) {
		printStatement();
	}
	else if (match(TOKEN_APPEND)) {
		appendStatement();
	}
	else if (match(TOKEN_BREAK)) {
		breakStatement();
	}
	else if (match(TOKEN_CONTINUE)) {
		continueStatement();
	}
	else if (match(TOKEN_DELETE)) {
		deleteStatement();
	}
	else if (match(TOKEN_FOR)) {
		forStatement();
	}
	else if (match(TOKEN_IF)) {
		ifStatement();
	}
	else if (match(TOKEN_SWITCH)) {
		switchStatement();
	}
	else if (match(TOKEN_RETURN)) {
		returnStatement();
	}
	else if (match(TOKEN_WHILE)) {
		whileStatement();
	}
	else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	}
	else {
		expressionStatement();
	}
}

void declaration() {
	if (match(TOKEN_CLASS)) {
		classDeclaration();
	}
	else if (match(TOKEN_FUN)) {
		funDeclaration();
	}
	else if (match(TOKEN_VAR)) {
		varDeclaration(true);
	}
	else if (match(TOKEN_VAL)) {
		varDeclaration(false);
	}
	else {
		statement();
	}

	if (parser.panicMode) synchronize();
}

ObjFunction* compile(const char* source) {
	initScanner(source);
	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);

	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while (!match(TOKEN_EOF)) {
		declaration();
	}

	ObjFunction* function = endCompiler();
	return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
	Compiler* compiler = current;
	while (compiler != NULL) {
		markObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}
