#include "../inc/parser-helpers.h"

#include "../inc/debug.h"
#include "../inc/sym.h"

#include "../inc/parser.h"

#include "stdlib.h"
#include "stdarg.h"
#include "string.h"

static char* tokenClassGetStr (tokenClass Token);

/*:::: ERROR MESSAGING ::::*/

static void error (parserCtx* ctx, const char* format, ...) {
    printf("error(%d:%d): ", ctx->location.line, ctx->location.lineChar);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    puts(".");

    ctx->errors++;
    getchar();
}

void errorExpected (parserCtx* ctx, const char* Expected) {
    error(ctx, "expected %s, found '%s'", Expected, ctx->lexer->buffer);
}

void errorUndefSym (parserCtx* ctx) {
    error(ctx, "undefined symbol '%s'", ctx->lexer->buffer);
}

void errorIllegalBreak (parserCtx* ctx) {
    error(ctx, "cannot break when not in loop or switch");
}

void errorIdentOutsideDecl (struct parserCtx* ctx) {
    error(ctx, "identifier given outside declaration");
}

void errorDuplicateSym (struct parserCtx* ctx) {
    error(ctx, "duplicated identifier '%s'", ctx->lexer->buffer);
}

/*:::: TOKEN HANDLING ::::*/

bool tokenIs (parserCtx* ctx, const char* Match) {
    return !strcmp(ctx->lexer->buffer, Match);
}

bool tokenIsIdent (struct parserCtx* ctx) {
    return ctx->lexer->token == tokenIdent;
}

bool tokenIsInt (struct parserCtx* ctx) {
    return ctx->lexer->token == tokenInt;
}

bool tokenIsDecl (parserCtx* ctx) {
    sym* Symbol = symFind(ctx->scope, ctx->lexer->buffer);

    return    (Symbol && (   Symbol->class == symType
                          || Symbol->class == symStruct
                          || Symbol->class == symEnum))
           || tokenIs(ctx, "const");
}

void tokenNext (parserCtx* ctx) {
    lexerNext(ctx->lexer);

    ctx->location.line = ctx->lexer->stream->line;
    ctx->location.lineChar = ctx->lexer->stream->lineChar;
}

void tokenMatch (parserCtx* ctx) {
    debugMsg("matched(%d:%d): '%s'",
             ctx->location.line,
             ctx->location.lineChar,
             ctx->lexer->buffer);
    tokenNext(ctx);
}

char* tokenDupMatch (parserCtx* ctx) {
    char* Old = strdup(ctx->lexer->buffer);

    tokenMatch(ctx);

    return Old;
}

/**
 * Return the string associated with a token class
 */
static char* tokenClassGetStr (tokenClass class) {
    if (class == tokenOther)
        return "other";
    else if (class == tokenEOF)
        return "end of file";
    else if (class == tokenIdent)
        return "identifier";
    else if (class == tokenInt)
        return "int";

    else {
        char* Str = malloc(class+1);
        sprintf(Str, "%d", class);
        debugErrorUnhandled("tokenClassGetStr", "token class", Str);
        free(Str);
        return "unhandled";
    }
}

void tokenMatchToken (parserCtx* ctx, tokenClass Match) {
    if (ctx->lexer->token == Match)
        tokenMatch(ctx);

    else {
        errorExpected(ctx, tokenClassGetStr(Match));
        lexerNext(ctx->lexer);
    }
}

void tokenMatchStr (parserCtx* ctx, const char* Match) {
    if (tokenIs(ctx, Match))
        tokenMatch(ctx);

    else {
        char* expectedInQuotes = malloc(strlen(Match)+2);
        sprintf(expectedInQuotes, "'%s'", Match);
        errorExpected(ctx, expectedInQuotes);
        free(expectedInQuotes);

        lexerNext(ctx->lexer);
    }
}

bool tokenTryMatchStr (parserCtx* ctx, const char* Match) {
    if (tokenIs(ctx, Match)) {
        tokenMatch(ctx);
        return true;

    } else
        return false;
}

int tokenMatchInt (parserCtx* ctx) {
    int ret = atoi(ctx->lexer->buffer);

    tokenMatchToken(ctx, tokenInt);

    return ret;
}

char* tokenMatchIdent (parserCtx* ctx) {
    char* Old = strdup(ctx->lexer->buffer);

    tokenMatchToken(ctx, tokenIdent);

    return Old;
}