#include "../inc/emitter-decl.h"

#include "../std/std.h"

#include "../inc/debug.h"
#include "../inc/type.h"
#include "../inc/ast.h"
#include "../inc/sym.h"
#include "../inc/reg.h"
#include "../inc/asm.h"
#include "../inc/asm-amd64.h"

#include "../inc/emitter.h"
#include "../inc/emitter-value.h"

#include "string.h"

static void emitterDeclBasic (emitterCtx* ctx, ast* Node);
static void emitterStruct (emitterCtx* ctx, ast* Node);
static void emitterUnion (emitterCtx* ctx, ast* Node);

static void emitterDeclNode (emitterCtx* ctx, ast* Node);
static void emitterDeclAssignBOP (emitterCtx* ctx, const ast* Node);
static void emitterDeclCall (emitterCtx* ctx, const ast* Node);

static void emitterInitOrCompoundLiteral (emitterCtx* ctx, const ast* Node, sym* Symbol, operand base);

void emitterDecl (emitterCtx* ctx, const ast* Node) {
    debugEnter("Decl");

    emitterDeclBasic(ctx, Node->l);

    for (ast* Current = Node->firstChild;
         Current;
         Current = Current->nextSibling)
        emitterDeclNode(ctx, Current);

    debugLeave();
}

static void emitterDeclBasic (emitterCtx* ctx, ast* Node) {
    if (Node->tag == astStruct)
        emitterStruct(ctx, Node);

    else if (Node->tag == astUnion)
        emitterUnion(ctx, Node);

    else if (Node->tag == astLiteral)
        ;

    else
        debugErrorUnhandled("emitterDeclBasic", "AST tag", astTagGetStr(Node->tag));
}

static void emitterStruct (emitterCtx* ctx, ast* Node) {
    debugEnter("Struct");

    for (sym* Current = Node->symbol->firstChild;
         Current;
         Current = Current->nextSibling) {
        Current->offset = Node->symbol->size;
        /*Add the size of this field, rounded up to the nearest word boundary*/
        int alignment = ctx->arch->wordsize;
        Node->symbol->size += ((typeGetSize(ctx->arch, Current->dt) - 1)/alignment)*alignment + alignment;
        reportSymbol(Current);
    }

    reportSymbol(Node->symbol);

    debugLeave();
}

static void emitterUnion (emitterCtx* ctx, ast* Node) {
    debugEnter("Union");

    for (sym* Current = Node->symbol->firstChild;
         Current;
         Current = Current->nextSibling) {
        Current->offset = 0;
        Node->symbol->size = max(Node->symbol->size, typeGetSize(ctx->arch, Current->dt));
        reportSymbol(Current);
    }

    reportSymbol(Node->symbol);

    debugLeave();
}

static void emitterDeclNode (emitterCtx* ctx, ast* Node) {
    if (Node->tag == astInvalid || Node->tag == astEmpty)
        ;

    else if (Node->tag == astBOP) {
        if (!strcmp(Node->o, "="))
            emitterDeclAssignBOP(ctx, Node);

        else
            debugErrorUnhandled("emitterDeclNode", "operator", Node->o);

    } else if (Node->tag == astUOP) {
        if (!strcmp(Node->o, "*"))
            emitterDeclNode(ctx, Node->r);

        else
            debugErrorUnhandled("emitterDeclNode", "operator", Node->o);

    } else if (Node->tag == astCall)
        emitterDeclCall(ctx, Node);

    else if (Node->tag == astIndex)
        /*The emitter does nothing the size of the array, so only go
          down the left branch*/
        emitterDeclNode(ctx, Node->l);

    else if (Node->tag == astLiteral) {
        if (Node->litTag == literalIdent)
            ;

        else
            debugErrorUnhandled("emitterDeclNode", "literal tag", literalTagGetStr(Node->litTag));

    } else
        debugErrorUnhandled("emitterDeclNode", "AST tag", astTagGetStr(Node->tag));
}

static void emitterDeclAssignBOP (emitterCtx* ctx, const ast* Node) {
    debugEnter("DeclAssignBOP");

    /*The emitter doesn't need to trace the RHS*/
    emitterDeclNode(ctx, Node->l);

    asmEnter(ctx->Asm);
    operand L = operandCreateMem(&regs[regRBP],
                                 Node->symbol->offset,
                                 typeGetSize(ctx->arch,
                                             Node->symbol->dt));
    asmLeave(ctx->Asm);

    if (Node->r->tag == astLiteral && Node->r->litTag == literalInit)
        emitterInitOrCompoundLiteral(ctx, Node->r, Node->symbol, L);

    else {
        if (Node->symbol->storage == storageAuto) {
            asmEnter(ctx->Asm);
            operand R = emitterValue(ctx, Node->r, operandCreate(operandUndefined));
            asmLeave(ctx->Asm);
            asmMove(ctx->Asm, L, R);
            operandFree(R);

        } else
            debugErrorUnhandled("emitterDeclAssignBOP", "storage tag", storageTagGetStr(Node->symbol->storage));
    }

    debugLeave();
}

static void emitterDeclCall (emitterCtx* ctx, const ast* Node) {
    debugEnter("DeclCall");

    /*Nothing to do with the params*/
    emitterDeclNode(ctx, Node->l);

    if (Node->symbol->label.label == 0)
        Node->symbol->label = labelNamed(Node->symbol->ident);

    debugLeave();
}

static void emitterInitOrCompoundLiteral (emitterCtx* ctx, const ast* Node, sym* Symbol, operand base) {
    debugEnter("InitOrCompoundLiteral");
    (void) Symbol;

    /*Struct initialization*/
    if (Node->dt->tag == typeBasic && Node->dt->basic->tag == symStruct) {
        sym* structSym = Node->dt->basic;

        ast* value;
        sym* field;

        /*For every field*/
        for (value = Node->firstChild, field = structSym->firstChild;
             value && field;
             value = value->nextSibling, field = field->nextSibling) {
            /*Prepare the left operand*/
            operand L = base;
            L.size = typeGetSize(ctx->arch, field->dt);
            L.offset += field->offset;

            /*Recursive initialization*/
            if (value->tag == astLiteral && value->litTag == literalInit) {
                emitterInitOrCompoundLiteral(ctx, value, field, L);

            /*Regular value*/
            } else {
                asmEnter(ctx->Asm);
                operand R = emitterValue(ctx, value, operandCreate(operandUndefined));
                asmLeave(ctx->Asm);

                asmMove(ctx->Asm, L, R);
                operandFree(R);
            }
        }

    /*Array initialization*/
    } else if (typeIsArray(Node->dt)) {
        int elementSize = typeGetSize(ctx->arch, Symbol->dt->base);
        operand L = base;
        L.size = elementSize;

        /*For every element*/
        for (ast* Current = Node->firstChild;
             Current;
             Current = Current->nextSibling) {
            asmEnter(ctx->Asm);
            operand R = emitterValue(ctx, Current, operandCreate(operandUndefined));
            asmLeave(ctx->Asm);

            L.offset += elementSize;
            asmMove(ctx->Asm, L, R);
            operandFree(R);
        }

    /*Scalar*/
    } else {
        asmEnter(ctx->Asm);
        operand R = emitterValue(ctx, Node->firstChild, operandCreate(operandUndefined));
        asmLeave(ctx->Asm);
        asmMove(ctx->Asm, base, R);
        operandFree(R);
    }

    debugLeave();
}
