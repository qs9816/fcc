#include "../inc/analyzer-value.h"

#include "../inc/debug.h"
#include "../inc/type.h"
#include "../inc/ast.h"
#include "../inc/sym.h"

#include "../inc/analyzer.h"

#include "string.h"

static const type* analyzerBOP (analyzerCtx* ctx, ast* Node);
static const type* analyzerComparisonBOP (analyzerCtx* ctx, ast* Node);
static const type* analyzerMemberBOP (analyzerCtx* ctx, ast* Node);
static const type* analyzerCommaBOP (analyzerCtx* ctx, ast* Node);
static const type* analyzerUOP (analyzerCtx* ctx, ast* Node);
static const type* analyzerTernary (analyzerCtx* ctx, ast* Node);
static const type* analyzerIndex (analyzerCtx* ctx, ast* Node);
static const type* analyzerCall (analyzerCtx* ctx, ast* Node);
static const type* analyzerLiteral (analyzerCtx* ctx, ast* Node);
static const type* analyzerArrayLiteral (analyzerCtx* ctx, ast* Node);

/**
 * Returns whether the (binary) operator is one that can only act on
 * numeric types (e.g. int, char, not bool, not x*)
 */
static bool isNumericBOP (char* o) {
    return    !strcmp(o, "+") || !strcmp(o, "-")
           || !strcmp(o, "*") || !strcmp(o, "/")
           || !strcmp(o, "%")
           || !strcmp(o, "&") || !strcmp(o, "|")
           || !strcmp(o, "^")
           || !strcmp(o, "<<") || !strcmp(o, ">>")
           || !strcmp(o, "+=") || !strcmp(o, "-=")
           || !strcmp(o, "*=") || !strcmp(o, "/=")
           || !strcmp(o, "%=")
           || !strcmp(o, "&=") || !strcmp(o, "|=")
           || !strcmp(o, "^=")
           || !strcmp(o, "<<=") || !strcmp(o, ">>=");
}

/**
 * Is it an ordinal operator (defines an ordering...)?
 */
static bool isOrdinalBOP (char* o) {
    return    !strcmp(o, ">") || !strcmp(o, "<")
           || !strcmp(o, ">=") || !strcmp(o, "<=");
}

static bool isEqualityBOP (char* o) {
    return !strcmp(o, "==") || !strcmp(o, "!=");
}

static bool isAssignmentBOP (char* o) {
    return    !strcmp(o, "=")
           || !strcmp(o, "+=") || !strcmp(o, "-=")
           || !strcmp(o, "*=") || !strcmp(o, "/=")
           || !strcmp(o, "%=")
           || !strcmp(o, "&=") || !strcmp(o, "|=")
           || !strcmp(o, "^=")
           || !strcmp(o, "<<=") || !strcmp(o, ">>=");
}

/**
 * Does this operator access struct/class members of its LHS?
 */
static bool isMemberBOP (char* o) {
    return !strcmp(o, ".") || !strcmp(o, "->");
}

/**
 * Does this member dereference its LHS?
 */
static bool isDerefBOP (char* o) {
    return !strcmp(o, "->");
}

/**
 * Is this the ',' operator? Yes, a bit trivial, this class
 */
static bool isCommaBOP (char* o) {
    return !strcmp(o, ",");
}

const type* analyzerValue (analyzerCtx* ctx, ast* Node) {
    if (Node->class == astBOP) {
        if (isNumericBOP(Node->o) || isAssignmentBOP(Node->o))
            return analyzerBOP(ctx, Node);

        else if (isOrdinalBOP(Node->o) || isEqualityBOP(Node->o))
            return analyzerComparisonBOP(ctx, Node);

        else if (isMemberBOP(Node->o))
            return analyzerMemberBOP(ctx, Node);

        else if (isCommaBOP(Node->o))
            return analyzerCommaBOP(ctx, Node);

        else {
            debugErrorUnhandled("analyzerValue", "operator", Node->o);
            return Node->dt = typeCreateInvalid();
        }

    } else if (Node->class == astUOP)
        return analyzerUOP(ctx, Node);

    else if (Node->class == astTOP)
        return analyzerTernary(ctx, Node);

    else if (Node->class == astIndex)
        return analyzerIndex(ctx, Node);

    else if (Node->class == astCall)
        return analyzerCall(ctx, Node);

    else if (Node->class == astLiteral) {
        if (Node->litClass == literalArray)
            return analyzerArrayLiteral(ctx, Node);

        else
            return analyzerLiteral(ctx, Node);

    } else if (Node->class == astInvalid) {
        return Node->dt = typeCreateInvalid();

    } else {
        debugErrorUnhandled("analyzerValue", "AST class", astClassGetStr(Node->class));
        return Node->dt = typeCreateInvalid();
    }
}

static const type* analyzerBOP (analyzerCtx* ctx, ast* Node) {
    debugEnter("BOP");

    const type* L = analyzerValue(ctx, Node->l);
    const type* R = analyzerValue(ctx, Node->r);

    /*Check that the operation are allowed on the operands given*/

    if (isNumericBOP(Node->o))
        if (!typeIsNumeric(L) || !typeIsNumeric(R))
            analyzerErrorOp(ctx, Node->o, "numeric type",
                            !typeIsNumeric(L) ? Node->l : Node->r,
                            !typeIsNumeric(L) ? L : R);

    if (isAssignmentBOP(Node->o)) {
        if (!typeIsAssignment(L) || !typeIsAssignment(R))
            analyzerErrorOp(ctx, Node->o, "assignable type",
                            !typeIsAssignment(L) ? Node->l : Node->r,
                            !typeIsAssignment(L) ? L : R);

        /*!!!LVALUE CHECK ON L*/
        if (false)
            analyzerErrorOp(ctx, Node->o, "lvalue", Node->l, L);
    }

    /*Work out the type of the result*/

    if (typeIsCompatible(L, R)) {
        /*The type of the right hand side
          (assignment does not return an lvalue)*/
        if (isAssignmentBOP(Node->o))
            Node->dt = typeDeriveFrom(R);

        else
            Node->dt = typeDeriveFromTwo(L, R);

    } else {
        analyzerErrorMismatch(ctx, Node, Node->o, L, R);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerComparisonBOP (analyzerCtx* ctx, ast* Node) {
    debugEnter("ComparisonBOP");

    const type* L = analyzerValue(ctx, Node->l);
    const type* R = analyzerValue(ctx, Node->r);

    /*Allowed?*/

    if (isOrdinalBOP(Node->o)) {
        if (!typeIsOrdinal(L) || !typeIsOrdinal(R))
            analyzerErrorOp(ctx, Node->o, "comparable type",
                            !typeIsOrdinal(L) ? Node->l : Node->r,
                            !typeIsOrdinal(L) ? L : R);

    } else /*if (isEqualityBOP(Node->o))*/
        if (!typeIsEquality(L) || !typeIsEquality(R))
            analyzerErrorOp(ctx, Node->o, "comparable type",
                            !typeIsEquality(L) ? Node->l : Node->r,
                            !typeIsEquality(L) ? L : R);

    /*Result*/

    if (typeIsCompatible(L, R))
        Node->dt = typeDeriveFromTwo(L, R);

    else {
        analyzerErrorMismatch(ctx, Node, Node->o, L, R);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerMemberBOP (analyzerCtx* ctx, ast* Node) {
    debugEnter("MemberBOP");

    const type* L = analyzerValue(ctx, Node->l);

    /*Operator allowed?*/

    /* -> */
    if (isDerefBOP(Node->o)) {
        if (!typeIsPtr(L))
            analyzerErrorOp(ctx, Node->o, "pointer", Node->l, L);

        else if (!typeIsRecord(L->base))
            analyzerErrorOp(ctx, Node->o, "structure pointer", Node->l, L);

    /* . */
    } else
        if (!typeIsRecord(L))
            analyzerErrorOp(ctx, Node->o, "structure type", Node->l, L);

    /*Return type: the field*/

    if (typeIsBasic(L))
        Node->symbol = symChild(L->basic, (char*) Node->r->literal);

    else if (typeIsPtr(L) && typeIsBasic(L->base))
        Node->symbol = symChild(L->base->basic, (char*) Node->r->literal);

    if (Node->symbol)
        Node->dt = typeDeepDuplicate(Node->symbol->dt);

    else {
        analyzerErrorMember(ctx, Node->o, Node->r, L);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerCommaBOP (analyzerCtx* ctx, ast* Node) {
    debugEnter("CommaBOP");

    const type* R = analyzerValue(ctx, Node->r);

    /*typeXXX functions always respond positively when provided with
      invalids. As this is one of the rare times when a negative response
      is desired, specifically let invalids through.*/
    if (!typeIsVoid(R) || typeIsInvalid(R))
        Node->dt = typeDeepDuplicate(R);

    else {
        analyzerErrorOp(ctx, Node->o, "non-void", Node->r, R);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerUOP (analyzerCtx* ctx, ast* Node) {
    debugEnter("UOP");

    const type* R = analyzerValue(ctx, Node->r);

    /*Numeric operator*/
    if (   !strcmp(Node->o, "+") || !strcmp(Node->o, "-")
        || !strcmp(Node->o, "++") || !strcmp(Node->o, "--")
        || !strcmp(Node->o, "!")
        || !strcmp(Node->o, "~")) {
        if (!typeIsNumeric(R)) {
            analyzerErrorOp(ctx, Node->o, "numeric type", Node->r, R);
            Node->dt = typeCreateInvalid();

        /*Assignment operator*/
        } else if (!strcmp(Node->o, "++") || !strcmp(Node->o, "--")) {
            /*!!!LVALUE CHECK ON R*/
            if (true)
                Node->dt = typeDeriveFrom(R);

            else {
                analyzerErrorOp(ctx, Node->o, "lvalue", Node->r, R);
                Node->dt = typeCreateInvalid();
            }

        } else
            Node->dt = typeDeriveFrom(R);

    /*Dereferencing a pointer*/
    } else if (!strcmp(Node->o, "*")) {
        if (typeIsPtr(R))
            Node->dt = typeDeriveBase(R);

        else {
            analyzerErrorOp(ctx, Node->o, "pointer", Node->r, R);
            Node->dt = typeCreateInvalid();
        }

    /*Referencing an lvalue*/
    } else if (!strcmp(Node->o, "&")) {
        /*!!!LVALUE CHECK ON R*/
        if (true)
            Node->dt = typeDerivePtr(R);

        else {
            analyzerErrorOp(ctx, Node->o, "lvalue", Node->r, R);
            Node->dt = typeCreateInvalid();
        }

    } else {
        debugErrorUnhandled("analyzerUOP", "operator", Node->o);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerTernary (analyzerCtx* ctx, ast* Node) {
    debugEnter("Ternary");

    const type* Cond = analyzerValue(ctx, Node->firstChild);
    const type* L = analyzerValue(ctx, Node->l);
    const type* R = analyzerValue(ctx, Node->r);

    /*Operation allowed*/

    if (!typeIsCondition(Cond))
        analyzerErrorOp(ctx, "ternary ?:", "condition value", Node->firstChild, Cond);

    /*Result types match => return type*/

    if (typeIsCompatible(L, R))
        Node->dt = typeDeriveUnified(L, R);

    else {
        analyzerErrorMismatch(ctx, Node, "ternary ?:", L, R);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerIndex (analyzerCtx* ctx, ast* Node) {
    debugEnter("Index");

    const type* L = analyzerValue(ctx, Node->l);
    const type* R = analyzerValue(ctx, Node->r);

    if (!typeIsNumeric(R))
        analyzerErrorOp(ctx, "[]", "numeric index", Node->r, R);

    if (typeIsArray(L) || typeIsPtr(L))
        Node->dt = typeDeriveBase(L);

    else {
        analyzerErrorOp(ctx, "[]", "array or pointer", Node->l, L);
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerCall (analyzerCtx* ctx, ast* Node) {
    debugEnter("Call");

    const type* L = analyzerValue(ctx, Node->l);

    /*Callable*/
    if (!typeIsCallable(L)) {
        analyzerErrorOp(ctx, "()", "function", Node->firstChild, Node->symbol->dt);
        Node->dt = typeCreateInvalid();

    } else {
        /*If callable, then a result type can be derived,
          regardless of parameter matches*/
        Node->dt = typeDeepDuplicate(typeDeriveReturn(Node->symbol->dt));

        /*Right number of params?*/
        if ((typeIsPtr(L) ? L->base->params : L->params) != Node->children)
            analyzerErrorDegree(ctx, Node, "parameters",
                                typeIsPtr(L) ? L->base->params : L->params, Node->children,
                                Node->symbol->ident);

        /*Do the parameter types match?*/
        else {
            ast* cNode;
            sym* cParam;
            int n;

            /*Traverse down both lists at once, checking types, leaving
              once either ends (we already know they have the same length)*/
            for (cNode = Node->firstChild, cParam = Node->symbol->firstChild, n = 0;
                 cNode && cParam;
                 cNode = cNode->nextSibling, cParam = cParam->nextSibling, n++) {
                const type* Param = analyzerValue(ctx, cNode);

                if (!typeIsCompatible(Param, cParam->dt))
                    analyzerErrorParamMismatch(ctx, Node, n, cParam->dt, Param);
            }
        }
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerLiteral (analyzerCtx* ctx, ast* Node) {
    debugEnter("Literal");

    if (Node->litClass == literalInt)
        Node->dt = typeCreateBasic(ctx->types[builtinInt]);

    else if (Node->litClass == literalBool)
        Node->dt = typeCreateBasic(ctx->types[builtinBool]);

    else if (Node->litClass == literalIdent)
        Node->dt = typeDeepDuplicate(Node->symbol->dt);

    else {
        debugErrorUnhandled("analyzerLiteral", "AST class", astClassGetStr(Node->class));
        Node->dt = typeCreateInvalid();
    }

    debugLeave();

    return Node->dt;
}

static const type* analyzerArrayLiteral (analyzerCtx* ctx, ast* Node) {
    debugEnter("ArrayLiteral");

    /*Allowed?*/

    /*TODO: Check element types match*/

    /*Return type*/

    Node->dt = typeDeriveArray(analyzerValue(ctx, Node->firstChild),
                               Node->children);

    debugLeave();

    return Node->dt;
}