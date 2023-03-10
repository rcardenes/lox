#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
	const char* start;
	const char* current;
	int line;
} Scanner;

Scanner scanner;

void initScanner(const char* source) {
	scanner.start = source;
	scanner.current = source;
	scanner.line = 1;
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       (c == '_');
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static bool isHexDigit(char c) {
	char k = c & 0x5f;
	return (c >= '0' && c <= '9') || (k >= 'A' && k <= 'F');
}

static bool isOctDigit(char c) {
	return (c >= '0' && c <= '7');
}

static bool isAtEnd() {
	return *scanner.current == '\0';
}

static Token makeToken(TokenType type) {
	Token token;
	token.type = type;
	token.start = scanner.start;
	token.length = (int)(scanner.current - scanner.start);
	token.line = scanner.line;
	return token;
}

static Token errorToken(const char* message) {
	Token token;
	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner.line;
	return token;
}

static char advance() {
	scanner.current++;
	return scanner.current[-1];
}

static bool match(char expected) {
	if (isAtEnd()) return false;
	if (*scanner.current != expected) return false;
	scanner.current++;
	return true;
}

static char peek() {
	return *scanner.current;
}

static char peekNext() {
	if (isAtEnd()) return '\0';
	return scanner.current[1];
}

static void skipWhitespace() {
	for (;;) {
		char c = peek();
		switch (c) {
			case '\r':
			case '\n':
				scanner.line++;
			case '\t':
			case ' ':
				advance();
				break;
			case '/':
				if (peekNext() == '/') {
					// A comment goes to the end of the line.
					while (peek() != '\n' && (!isAtEnd())) advance();
				}
				else {
					return;
				}
				break;
			default:
				return;
		}
	}
}

static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
	if ((scanner.current - scanner.start) == (start + length) && memcmp(scanner.start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
	switch (scanner.start[0]) {
		case 'a':
			  if (scanner.current - scanner.start > 1) {
				  switch (scanner.start[1]) {
					  case 'n': return checkKeyword(2, 1, "d", TOKEN_AND);
					  case 'p': return checkKeyword(2, 4, "pend", TOKEN_APPEND);
				  }
			  }
			  break;
		case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
		case 'c':
			  if (scanner.current - scanner.start > 1) {
				  switch (scanner.start[1]) {
					  case 'a': return checkKeyword(2, 2, "se", TOKEN_CASE);
					  case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
					  case 'o': return checkKeyword(2, 6, "ntinue", TOKEN_CONTINUE);
				  }
			  }
			  break;
		case 'd':
			  if ((scanner.current - scanner.start > 2) && scanner.start[1] == 'e') {
				  switch (scanner.start[2]) {
					  case 'f': return checkKeyword(3, 4, "ault", TOKEN_DEFAULT);
					  case 'l': return checkKeyword(3, 3, "ete", TOKEN_DELETE);
				  }
			  }
			  break;
		case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
		case 'f':
			  if (scanner.current - scanner.start > 1) {
				  switch (scanner.start[1]) {
					  case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
					  case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
					  case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
				  }
			  }
			  break;
		case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
		case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
		case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
		case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
		case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
		case 's':
			  if (scanner.current - scanner.start > 1) {
				  switch (scanner.start[1]) {
					  case 'u': return checkKeyword(2, 3, "per", TOKEN_SUPER);
					  case 'w': return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
				  }
			  }
			  break;
		case 't':
			  if (scanner.current - scanner.start > 1) {
				  switch (scanner.start[1]) {
					  case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
					  case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
				  }
			  }
			  break;
		case 'v':
			  if ((scanner.current - scanner.start == 3) && (scanner.start[1] == 'a')) {
				  switch (scanner.start[2]) {
					  case 'l': return TOKEN_VAL;
					  case 'r': return TOKEN_VAR;
				  }
			  }
			  break;
		case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static Token identifier() {
	while (isAlpha(peek()) || isDigit(peek())) advance();
	return makeToken(identifierType());
}

static Token number() {
	char second = peek();

	if (second == 'x') {
		advance();
		if (!isHexDigit(peek()))
			return errorToken("Unexpected character after '0x'.");
		while (isHexDigit(peek())) advance();
	}
	else if (second == 'o') {
		advance();
		if (!isOctDigit(peek()))
			return errorToken("Unexpected character after '0o'.");
		while (isOctDigit(peek())) advance();
	}
	else {
		while (isDigit(peek())) advance();

		// Look for a fractional part
		if (peek() == '.' && isDigit(peekNext())) {
			// Consume the '.'
			advance();

			while (isDigit(peek())) advance();
			return makeToken(TOKEN_NUMBER);
		}
	}
	return makeToken(TOKEN_INTEGER);
}

static Token string() {
	while (peek() != '"' && !isAtEnd()) {
		if (peek() == '\n') scanner.line++;
		advance();
	}

	if (isAtEnd()) return errorToken("Unterminated string.");

	// The closing quote.
	advance();
	return makeToken(TOKEN_STRING);
}

Token scanToken() {
	skipWhitespace();
	scanner.start = scanner.current;

	if (isAtEnd()) return makeToken(TOKEN_EOF);

	char c = advance();

	if (isAlpha(c)) return identifier();
	if (isDigit(c)) return number();

	switch (c) {
		case '(': return makeToken(TOKEN_LEFT_PAREN);
		case ')': return makeToken(TOKEN_RIGHT_PAREN);
		case '{': return makeToken(TOKEN_LEFT_BRACE);
		case '}': return makeToken(TOKEN_RIGHT_BRACE);
		case '[': return makeToken(TOKEN_LEFT_BRACKET);
		case ']': return makeToken(TOKEN_RIGHT_BRACKET);
		case ';': return makeToken(TOKEN_SEMICOLON);
		case ':': return makeToken(TOKEN_COLON);
		case ',': return makeToken(TOKEN_COMMA);
		case '.': return makeToken(TOKEN_DOT);
		case '-': return makeToken(TOKEN_MINUS);
		case '+': return makeToken(TOKEN_PLUS);
		case '?': return makeToken(TOKEN_QUESTION_MARK);
		case '/': return makeToken(TOKEN_SLASH);
		case '*': return makeToken(TOKEN_STAR);
		case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
		case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
		case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
		case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
		case '"': return string();
	}

	return errorToken("Unexpected character.");
}
