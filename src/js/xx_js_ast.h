/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* js_ast.h - abstract syntax tree shared by the parser and the interpreter. */

#ifndef JS_AST_H
#define JS_AST_H

#include "xx_js_internal.h"

typedef enum {
    N_PROGRAM = 0,
    N_NUM,
    N_STR,
    N_REGEXP,
    N_IDENT,
    N_THIS,
    N_NULL,
    N_BOOL,
    N_ARRAY,
    N_OBJECT,
    N_PROP,     /* key/value pair inside an object literal, num = key size */
    N_FUNCTION, /* a = params list, b = body block, str = name */
    N_CALL,     /* a = callee, list = arguments */
    N_NEW,      /* a = callee, list = arguments */
    N_MEMBER,   /* a = object, str = property name */
    N_INDEX,    /* a = object, b = index expression */
    N_UNARY,    /* op, a */
    N_UPDATE,   /* op (++/--), a, num = 1 for prefix */
    N_BINARY,   /* op, a, b */
    N_LOGICAL,  /* op (&& ||), a, b */
    N_ASSIGN,   /* op, a = target, b = value */
    N_COND,     /* a ? b : c */
    N_SEQ,      /* a, b */
    N_VAR,      /* list of N_VARDECL */
    N_VARDECL,  /* str = name, a = initialiser (optional) */
    N_BLOCK,    /* list of statements */
    N_IF,       /* a = test, b = then, c = else */
    N_FOR,      /* a = init, b = test, c = update, d = body */
    N_FORIN,    /* a = left (N_VAR or target expr), b = object, d = body */
    N_WHILE,    /* a = test, d = body */
    N_DOWHILE,  /* a = test, d = body */
    N_RETURN,   /* a = argument */
    N_BREAK,    /* str = label */
    N_CONTINUE, /* str = label */
    N_THROW,    /* a = argument */
    N_TRY,      /* a = block, b = catch body, str = catch param, c = finally */
    N_SWITCH,   /* a = discriminant, list = N_CASE */
    N_CASE,     /* a = test (NULL for default), list = statements */
    N_LABELED,  /* str = label, a = statement */
    N_EMPTY,
    N_EXPRSTMT, /* a = expression */
    N_DEBUGGER
} JSNodeType;

/* Operator identifiers used by N_UNARY/N_BINARY/N_ASSIGN/N_LOGICAL. */
typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_EQ,
    OP_NE,
    OP_SEQ,
    OP_SNE,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_SHL,
    OP_SHR,
    OP_USHR,
    OP_LAND,
    OP_LOR,
    OP_NOT,
    OP_BNOT,
    OP_NEG,
    OP_POS,
    OP_TYPEOF,
    OP_VOID,
    OP_DELETE,
    OP_IN,
    OP_INSTANCEOF,
    OP_INC,
    OP_DEC,
    OP_ASSIGN
} JSOp;

/* The ordered binding operations for one program/function. Nodes are borrowed
 * from its immutable AST. Depth checkpoints retain the evaluator's existing
 * nesting-limit behavior without walking unrelated statements on each call. */
#define JS_ASSIGN_PLAIN 1  /* neither of the forms below */
#define JS_ASSIGN_ADD 2    /* x = x + ... */
#define JS_ASSIGN_CONCAT 3 /* x = x.concat(one argument) */

typedef struct {
    JSNode *pNode; /* N_VARDECL / declaration N_FUNCTION; NULL = final guard */
    unsigned nDepth;
    unsigned nCheckDepth;
} JSHoistStep;

typedef struct {
    size_t nSteps;
    JSHoistStep steps[];
} JSHoistPlan;

/* The bindings of one function activation in the order the call sequence
 * creates them: arguments (when referenced), the name of a named function
 * expression, the parameters, then the hoisted declarations; a repeated name
 * keeps its first position. A fresh scope record is this template copied: its
 * entries borrow the AST spellings, and an identifier inside the function
 * body knows its entry index (JSNode.nSlot). Built once with the AST and
 * immutable afterwards, like the hoist plan. */
typedef struct JSScopeLayout {
    uint32_t nCount;     /* bindings */
    uint32_t nCapacity;  /* entry capacity of a fresh record (0 with no bindings) */
    uint32_t nIndexSize; /* hash index size of a fresh record */
    uint32_t nMaxDepth;  /* deepest nesting check of the hoist plan */
    size_t nBytes;       /* size of this block, tables included */
    int32_t nArgumentsSlot;
    int32_t nSelfSlot;
    int32_t *pParamSlots; /* one per parameter */
    int32_t *pStepSlots;  /* one per hoist step: the slot of a function declaration, else -1 */
    JSProp *pEntries;     /* nCount entries: borrowed keys, undefined values */
    int32_t *pIndex;      /* nIndexSize slots, as inserting the keys in order builds it */
} JSScopeLayout;

/* The members the evaluator reads on every visit come first so that they
 * share a cache line. */
struct JSNode {
    JSNodeType type;
    JSOp op;
    JSNode *a;
    JSNode *b;
    char *pStr;
    double nNum;
    /* Length and hash of pStr, filled before a program is published:
     * variable access would otherwise rehash the same name endlessly.     */
    size_t nStrSize;
    uint32_t nStrHash;
    /* N_IDENT / N_VARDECL inside a function with a scope layout: the entry
     * index of the name in that function's fresh scope record, or -1 when
     * the function does not bind the name. Meaningless while pLayout is NULL. */
    int32_t nSlot;
    /* N_FUNCTION: the layout it owns. N_IDENT / N_VARDECL: the layout of the
     * enclosing function (borrowed). */
    JSScopeLayout *pLayout;
    JSNode *c;
    JSNode *d;
    JSNode **ppList;
    size_t nList;
    uint8_t bStrKey;
    /* Immutable function-call metadata, prepared with the AST. */
    uint8_t bNeedsArguments;
    uint8_t bNeedsHoist;
    /* N_ASSIGN with OP_ASSIGN: which form eval_assign found by comparing
     * spellings on every evaluation. 0 = not analysed (compare at run time). */
    uint8_t nAssignKind;
    int nLine;
    int nColumn;
    JSHoistPlan *pHoistPlan;
    size_t nListCapacity;
    char *pStr2;
    /* All nodes borrow the source root, whose name is owned by N_PROGRAM. */
    JSNode *pSourceRoot;
    char *pSourceName;
    /* Compiled literals are immutable; evaluation makes a fresh wrapper. */
    JSRegExp *pRegExpLiteral;
    /* Function bodies cache their declared variable names for hoisting. */
    char **ppVarNames;
    size_t nVarNames;
};

void js_node_set_source(JSNode *pRoot, const char *pName);

JSNode *js_node_new(JSNodeType type, int nLine);

#endif /* JS_AST_H */
