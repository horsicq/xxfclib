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

/* js_parse.c - recursive descent parser producing the AST of js_ast.h. */

#include "xx_js_ast.h"
#include "xx_js_lex.h"

/* Bound on the recursive descent. Nesting this deep does not occur in a
 * signature script, while the C stack of a 1 MB thread does not survive
 * much more.                                                           */
#define JS_MAX_PARSE_DEPTH 200

typedef struct {
    JSLexer lexer;
    size_t nPos;
    char *pError;
    const char *pName;
    int bError; /* set once; every accessor then reports end of input */
    int nDepth; /* recursive descent depth, capped at JS_MAX_PARSE_DEPTH */
} JSParser;

static JSNode *parse_statement(JSParser *pParser);
static JSNode *parse_expression(JSParser *pParser, int bNoIn);
static JSNode *parse_assignment(JSParser *pParser, int bNoIn);
static JSNode *parse_unary(JSParser *pParser);
static JSNode *parse_function(JSParser *pParser, int bExpression);

/* ------------------------------------------------------------------ util  */

JSNode *js_node_new(JSNodeType type, int nLine)
{
    JSNode *pNode = (JSNode *)xx_js_calloc(1, sizeof(JSNode));

    pNode->type = type;
    pNode->nLine = nLine;

    return pNode;
}

static JSNode *parser_node_new(JSParser *pParser, JSNodeType type, int nLine)
{
    JSNode *pNode = js_node_new(type, nLine);
    size_t nPos = pParser->bError || pParser->nPos >= pParser->lexer.nCount ? pParser->lexer.nCount - 1 : pParser->nPos;
    pNode->nColumn = pParser->lexer.pTokens[nPos].nColumn;
    return pNode;
}
#define node_new(type, line) parser_node_new(pParser, type, line)

static void node_set_source_walk(JSNode *pNode, JSNode *pRoot)
{
    while (pNode) {
        size_t i;
        pNode->pSourceRoot = pRoot;
        node_set_source_walk(pNode->b, pRoot);
        node_set_source_walk(pNode->c, pRoot);
        node_set_source_walk(pNode->d, pRoot);
        for (i = 0; i < pNode->nList; i++) node_set_source_walk(pNode->ppList[i], pRoot);
        pNode = pNode->a;
    }
}
void js_node_set_source(JSNode *pRoot, const char *pName)
{
    if (!pRoot) return;
    if (pName || !pRoot->pSourceName) {
        xx_js_free(pRoot->pSourceName);
        pRoot->pSourceName = xx_js_strdup(pName ? pName : "");
    }
    node_set_source_walk(pRoot, pRoot);
}

static void node_add(JSNode *pNode, JSNode *pChild)
{
    if (pNode->nList == pNode->nListCapacity) {
        size_t nCapacity = pNode->nListCapacity ? pNode->nListCapacity * 2 : 8;
        pNode->ppList = (JSNode **)xx_js_realloc(pNode->ppList, nCapacity * sizeof(JSNode *));
        pNode->nListCapacity = nCapacity;
    }
    pNode->ppList[pNode->nList++] = pChild;
}

/* The parser folds left-associative chains iteratively, so 'a+a+a+...' is a
 * shallow parse but an arbitrarily long ->a spine. That spine is therefore
 * unrolled here instead of recursed over.                                  */
void js_free_node(JSNode *pNode)
{
    while (pNode != NULL) {
        JSNode *pLeft = pNode->a;
        size_t i = 0;

        js_free_node(pNode->b);
        js_free_node(pNode->c);
        js_free_node(pNode->d);

        for (i = 0; i < pNode->nList; i++) {
            js_free_node(pNode->ppList[i]);
        }

        for (i = 0; i < pNode->nVarNames; i++) {
            xx_js_free(pNode->ppVarNames[i]);
        }

        if (pNode->pRegExpLiteral) xx_regex_free(pNode->pRegExpLiteral);
        xx_js_free(pNode->pHoistPlan);
        xx_js_free(pNode->ppVarNames);
        xx_js_free(pNode->ppList);
        xx_js_free(pNode->pStr);
        xx_js_free(pNode->pStr2);
        xx_js_free(pNode->pSourceName);
        xx_js_free(pNode);

        pNode = pLeft;
    }
}

/* Preparation also returns conservative function-call metadata to the
 * enclosing function. One traversal replaces a separate scan of every
 * function body (including repeated scans of nested bodies). */
#define PREPARE_ARGUMENTS 1U
#define PREPARE_HOIST 2U

typedef struct {
    JSHoistPlan *pPlan;
    size_t nSteps;
    size_t nVars;
    unsigned nCheckDepth;
} HoistBuilder;

static void hoist_plan_add(HoistBuilder *pBuilder, JSNode *pNode, unsigned nDepth)
{
    if (pBuilder->pPlan) {
        JSHoistStep *pStep = &pBuilder->pPlan->steps[pBuilder->nSteps];
        pStep->pNode = pNode;
        pStep->nDepth = nDepth;
        pStep->nCheckDepth = pBuilder->nCheckDepth;
    }
    if (pNode && pNode->type == N_VARDECL) pBuilder->nVars++;
    pBuilder->nSteps++;
    pBuilder->nCheckDepth = 0;
}

/* Match hoist_declarations_node's order exactly, including declarations in
 * both conditional branches, loop initializers, catches and finally blocks.
 * Initializers/expressions and nested function bodies belong to other scopes.
 * A checkpoint includes empty/NULL branches because the old walker applied
 * its nesting guard before checking whether the node was NULL. */
static void hoist_plan_walk(HoistBuilder *pBuilder, JSNode *pNode, unsigned nDepth)
{
    size_t i;
    if (nDepth > pBuilder->nCheckDepth) pBuilder->nCheckDepth = nDepth;
    if (!pNode) return;
    switch (pNode->type) {
        case N_FUNCTION:
            if (pNode->nNum == 0 && pNode->pStr) hoist_plan_add(pBuilder, pNode, nDepth);
            return;
        case N_VAR:
            for (i = 0; i < pNode->nList; i++) {
                if (nDepth > pBuilder->nCheckDepth) pBuilder->nCheckDepth = nDepth;
                hoist_plan_add(pBuilder, pNode->ppList[i], nDepth);
            }
            return;
        case N_PROGRAM:
        case N_BLOCK:
        case N_CASE:
        case N_SWITCH:
            for (i = 0; i < pNode->nList; i++) hoist_plan_walk(pBuilder, pNode->ppList[i], nDepth + 1);
            return;
        case N_IF:
            hoist_plan_walk(pBuilder, pNode->b, nDepth + 1);
            hoist_plan_walk(pBuilder, pNode->c, nDepth + 1);
            return;
        case N_FOR:
        case N_FORIN:
            hoist_plan_walk(pBuilder, pNode->a, nDepth + 1);
            hoist_plan_walk(pBuilder, pNode->d, nDepth + 1);
            return;
        case N_WHILE:
        case N_DOWHILE: hoist_plan_walk(pBuilder, pNode->d, nDepth + 1); return;
        case N_TRY:
            hoist_plan_walk(pBuilder, pNode->a, nDepth + 1);
            hoist_plan_walk(pBuilder, pNode->b, nDepth + 1);
            hoist_plan_walk(pBuilder, pNode->c, nDepth + 1);
            return;
        case N_LABELED: hoist_plan_walk(pBuilder, pNode->a, nDepth + 1); return;
        default: return;
    }
}

/* Repeated var declarations cannot change a binding already created at a
 * shallower (or equal) depth. Keep shallower repetitions: a low runtime depth
 * budget may reject the first declaration and still accept a later sibling.
 * Function declarations always run in order because each replaces the value.
 * Merge skipped guards into the next step so rejected empty/deep branches
 * still raise the same exception before the next observable binding. */
static void hoist_plan_compact(JSHoistPlan *pPlan)
{
    size_t nIndexSize = 8, i, nUsed = 0;
    size_t *pIndex;
    unsigned nCheckDepth = 0;
    if (pPlan->nSteps < 3) return;
    while (nIndexSize / 2 < pPlan->nSteps) {
        if (nIndexSize > SIZE_MAX / 2) return;
        nIndexSize *= 2;
    }
    if (nIndexSize > SIZE_MAX / sizeof(*pIndex)) return;
    pIndex = (size_t *)xx_js_calloc(nIndexSize, sizeof(*pIndex));
    for (i = 0; i < pPlan->nSteps; i++) {
        JSHoistStep step = pPlan->steps[i];
        JSNode *pNode = step.pNode;
        size_t nSlot = 0;
        if (step.nCheckDepth > nCheckDepth) nCheckDepth = step.nCheckDepth;
        if (pNode) {
            nSlot = (size_t)pNode->nStrHash & (nIndexSize - 1);
            while (pIndex[nSlot]) {
                JSHoistStep *pPrevious = &pPlan->steps[pIndex[nSlot] - 1];
                JSNode *pPreviousNode = pPrevious->pNode;
                if (pPreviousNode->nStrHash == pNode->nStrHash && pPreviousNode->nStrSize == pNode->nStrSize &&
                    !xx_rt_memcmp(pPreviousNode->pStr, pNode->pStr, pNode->nStrSize)) break;
                nSlot = (nSlot + 1) & (nIndexSize - 1);
            }
            if (pNode->type == N_VARDECL && pIndex[nSlot] &&
                pPlan->steps[pIndex[nSlot] - 1].nDepth <= step.nDepth) continue;
        }
        step.nCheckDepth = nCheckDepth;
        nCheckDepth = 0;
        pPlan->steps[nUsed] = step;
        if (pNode && (!pIndex[nSlot] || step.nDepth < pPlan->steps[pIndex[nSlot] - 1].nDepth)) pIndex[nSlot] = nUsed + 1;
        nUsed++;
    }
    pPlan->nSteps = nUsed;
    xx_js_free(pIndex);
}

static JSHoistPlan *prepare_hoist_plan(JSNode *pBody)
{
    HoistBuilder builder = {0};
    JSHoistPlan *pPlan;
    hoist_plan_walk(&builder, pBody, 0);
    hoist_plan_add(&builder, NULL, 0);
    if (builder.nSteps > (SIZE_MAX - sizeof(*pPlan)) / sizeof(JSHoistStep)) return NULL;
    pPlan = (JSHoistPlan *)xx_js_malloc(sizeof(*pPlan) + builder.nSteps * sizeof(JSHoistStep));
    pPlan->nSteps = builder.nSteps;
    builder.pPlan = pPlan;
    builder.nSteps = 0;
    builder.nVars = 0;
    builder.nCheckDepth = 0;
    hoist_plan_walk(&builder, pBody, 0);
    hoist_plan_add(&builder, NULL, 0);
    if (builder.nVars > 1) hoist_plan_compact(pPlan);
    if (pPlan->nSteps < builder.nSteps) {
        pPlan = (JSHoistPlan *)xx_js_realloc(pPlan, sizeof(*pPlan) + pPlan->nSteps * sizeof(JSHoistStep));
    }
    return pPlan;
}

static unsigned prepare_program_node(JSNode *pNode, unsigned nDepth)
{
    unsigned nInfo = 0;
    /* Beyond the analysis ceiling retain the original allocations. We still
     * prepare hashes/literals so that even deeply nested ASTs stay immutable.
     * The parser's own recursion limit bounds this traversal; left-associated
     * expression chains are walked iteratively below. */
    if (pNode && nDepth >= 128) nInfo = PREPARE_ARGUMENTS | PREPARE_HOIST;
    while (pNode) {
        size_t i;
        unsigned nChildren = 0;
        JSNode *pLeft = pNode->a;
        /* Only identifier/declaration nodes are passed to node_key(). Literal
         * strings and property/member names need no cached identifier hash. */
        if (pNode->pStr && (pNode->type == N_IDENT || pNode->type == N_VARDECL || pNode->type == N_FUNCTION)) {
            pNode->nStrSize = xx_rt_strlen(pNode->pStr);
            pNode->nStrHash = xx_js_hash_str(pNode->pStr, pNode->nStrSize);
            pNode->bStrKey = 1;
        }
        if (pNode->type == N_REGEXP && !pNode->pRegExpLiteral) {
            char *pError = NULL;
            pNode->pRegExpLiteral = js_compile_regexp(pNode->pStr ? pNode->pStr : "", pNode->pStr2 ? pNode->pStr2 : "", &pError);
            /* Preserve evaluation-time errors for invalid, unreachable literals. */
            xx_js_free(pError);
        }
        nChildren |= prepare_program_node(pNode->b, nDepth + 1);
        nChildren |= prepare_program_node(pNode->c, nDepth + 1);
        nChildren |= prepare_program_node(pNode->d, nDepth + 1);
        for (i = 0; i < pNode->nList; i++) nChildren |= prepare_program_node(pNode->ppList[i], nDepth + 1);
        if (pNode->type == N_FUNCTION) {
            /* Parameters use N_IDENT nodes too, but their spellings declare
             * bindings rather than reading arguments or obtaining eval. */
            (void)prepare_program_node(pLeft, nDepth + 1);
            pNode->bNeedsArguments = (nChildren & PREPARE_ARGUMENTS) != 0;
            pNode->bNeedsHoist = (nChildren & PREPARE_HOIST) != 0;
            if (pNode->bNeedsHoist && !pNode->pHoistPlan) pNode->pHoistPlan = prepare_hoist_plan(pNode->b);
            pLeft = NULL; /* parameter list was prepared above */
        }
        nInfo |= nChildren;
        /* Only a variable reference can observe the arguments binding or
         * obtain eval. Literal strings, object keys and member spellings do
         * not read a variable. Keep propagating identifier references from
         * nested functions because closures can observe enclosing scopes. */
        if (pNode->type == N_IDENT && pNode->pStr &&
            (!xx_rt_strcmp(pNode->pStr, "arguments") || !xx_rt_strcmp(pNode->pStr, "eval"))) nInfo |= PREPARE_ARGUMENTS;
        if (pNode->type == N_VAR || (pNode->type == N_FUNCTION && pNode->nNum == 0 && pNode->pStr)) nInfo |= PREPARE_HOIST;
        pNode = pLeft;
    }
    return nInfo;
}

void js_prepare_program(JSNode *pNode)
{
    (void)prepare_program_node(pNode, 0);
    if (pNode && pNode->type == N_PROGRAM && !pNode->pHoistPlan) pNode->pHoistPlan = prepare_hoist_plan(pNode);
}

/* After an error the stream reports end of input, so every loop and every
 * recursive descent unwinds on its own without a non-local jump.          */
static JSToken *tok(JSParser *pParser)
{
    /* A label at the very end of the input steps past the EOF token; the
     * EOF token answers every read beyond the stream. */
    if (pParser->bError || (pParser->nPos >= pParser->lexer.nCount)) {
        return &pParser->lexer.pTokens[pParser->lexer.nCount - 1];
    }

    return &pParser->lexer.pTokens[pParser->nPos];
}

static JSToken *tok_at(JSParser *pParser, size_t nOffset)
{
    size_t nIndex = pParser->nPos + nOffset;

    if (nIndex >= pParser->lexer.nCount) {
        nIndex = pParser->lexer.nCount - 1;
    }

    return &pParser->lexer.pTokens[nIndex];
}

static void parse_error(JSParser *pParser, const char *pMessage)
{
    char sBuf[512];

    if (pParser->bError) {
        return; /* keep the first diagnostic */
    }

    xx_rt_snprintf(sBuf, sizeof(sBuf), "SyntaxError: %s (line %d)", pMessage, tok(pParser)->nLine);
    xx_js_free(pParser->pError);
    pParser->pError = xx_js_strdup(sBuf);
    pParser->bError = 1;
}

/* Guards one level of the descent. A refused level reports the error and
 * returns 0, which unwinds through the existing bError plumbing.        */
static int enter_recursion(JSParser *pParser)
{
    if (pParser->nDepth >= JS_MAX_PARSE_DEPTH) {
        parse_error(pParser, "too much recursion");

        return 0;
    }

    pParser->nDepth++;

    return 1;
}

static int accept(JSParser *pParser, JSTokType type)
{
    if (tok(pParser)->type == type) {
        pParser->nPos++;
        return 1;
    }

    return 0;
}

static void expect(JSParser *pParser, JSTokType type, const char *pWhat)
{
    if (!accept(pParser, type)) {
        char sBuf[128];

        xx_rt_snprintf(sBuf, sizeof(sBuf), "expected %s", pWhat);
        parse_error(pParser, sBuf);
    }
}

/* Automatic semicolon insertion. */
static void consume_semicolon(JSParser *pParser)
{
    if (accept(pParser, T_SEMI)) {
        return;
    }

    if ((tok(pParser)->type == T_RBRACE) || (tok(pParser)->type == T_EOF) || tok(pParser)->bNewLineBefore) {
        return;
    }

    parse_error(pParser, "expected ';'");
}

/* An identifier token is consumed exactly once. Its owned text can become
 * the AST's owned text, avoiding a duplicate allocation and copy. Keyword
 * tokens instead borrow the lexer's immutable table and must be copied. */
static char *take_token_text(JSToken *pToken)
{
    if (pToken->type == T_IDENT) {
        char *pText = pToken->pText;
        pToken->pText = NULL;
        return pText;
    }
    return xx_js_strdup(pToken->pText);
}

/* Identifier-like tokens usable as property names. */
static char *property_name(JSParser *pParser)
{
    JSToken *pTok = tok(pParser);

    if ((pTok->type == T_IDENT) || ((pTok->type >= T_BREAK) && (pTok->type <= T_FALSE))) {
        char *pResult = NULL;

        if (pTok->pText) {
            pResult = take_token_text(pTok);
        } else {
            static const char *pNames[] = {"break",    "case", "catch", "continue",   "default", "delete", "do",     "else", "finally", "for",
                                           "function", "if",   "in",    "instanceof", "new",     "return", "switch", "this", "throw",   "try",
                                           "typeof",   "var",  "void",  "while",      "with",    "null",   "true",   "false"};

            pResult = xx_js_strdup(pNames[pTok->type - T_BREAK]);
        }

        pParser->nPos++;

        return pResult;
    }

    parse_error(pParser, "expected property name");

    return NULL;
}

/* -------------------------------------------------------------- literals  */

static JSNode *parse_object_literal(JSParser *pParser)
{
    JSNode *pNode = node_new(N_OBJECT, tok(pParser)->nLine);

    expect(pParser, T_LBRACE, "'{'");

    while ((tok(pParser)->type != T_RBRACE) && (tok(pParser)->type != T_EOF)) {
        JSNode *pProp = node_new(N_PROP, tok(pParser)->nLine);

        if (tok(pParser)->type == T_STRING) {
            /* The lexer keeps embedded NULs, so the key carries its byte
             * count the same way N_STR does.                            */
            pProp->pStr = xx_js_strndup(tok(pParser)->pText, tok(pParser)->nTextSize);
            pProp->nNum = (double)tok(pParser)->nTextSize;
            pParser->nPos++;
        } else if (tok(pParser)->type == T_NUMBER) {
            char sBuf[64];

            /* ECMAScript ToString(Number), so the literal key and a computed
             * lookup of the same number agree.                              */
            xx_rt_dtoa_shortest(tok(pParser)->nNum, sBuf, sizeof(sBuf));
            pProp->pStr = xx_js_strdup(sBuf);
            pProp->nNum = (double)xx_rt_strlen(sBuf);
            pParser->nPos++;
        } else {
            pProp->pStr = property_name(pParser);
            pProp->nNum = (pProp->pStr != NULL) ? (double)xx_rt_strlen(pProp->pStr) : 0;
        }

        expect(pParser, T_COLON, "':'");
        pProp->a = parse_assignment(pParser, 0);
        node_add(pNode, pProp);

        if (!accept(pParser, T_COMMA)) {
            break;
        }
    }

    expect(pParser, T_RBRACE, "'}'");

    return pNode;
}

static JSNode *parse_array_literal(JSParser *pParser)
{
    JSNode *pNode = node_new(N_ARRAY, tok(pParser)->nLine);

    expect(pParser, T_LBRACKET, "'['");

    while ((tok(pParser)->type != T_RBRACKET) && (tok(pParser)->type != T_EOF)) {
        if (tok(pParser)->type == T_COMMA) {
            node_add(pNode, node_new(N_EMPTY, tok(pParser)->nLine));
            pParser->nPos++;
            continue;
        }

        node_add(pNode, parse_assignment(pParser, 0));

        if (!accept(pParser, T_COMMA)) {
            break;
        }
    }

    expect(pParser, T_RBRACKET, "']'");

    return pNode;
}

/* ------------------------------------------------------------ expression  */

static JSNode *parse_primary(JSParser *pParser)
{
    JSToken *pTok = tok(pParser);
    JSNode *pNode = NULL;

    switch (pTok->type) {
        case T_NUMBER:
            pNode = node_new(N_NUM, pTok->nLine);
            pNode->nNum = pTok->nNum;
            pParser->nPos++;
            return pNode;

        case T_STRING:
            pNode = node_new(N_STR, pTok->nLine);
            pNode->pStr = xx_js_strndup(pTok->pText, pTok->nTextSize);
            pNode->nNum = (double)pTok->nTextSize;
            pParser->nPos++;
            return pNode;

        case T_REGEXP:
            pNode = node_new(N_REGEXP, pTok->nLine);
            pNode->pStr = take_token_text(pTok);
            pNode->pStr2 = xx_js_strdup(pTok->pText2);
            pParser->nPos++;
            return pNode;

        case T_IDENT:
            pNode = node_new(N_IDENT, pTok->nLine);
            pNode->pStr = take_token_text(pTok);
            pParser->nPos++;
            return pNode;

        case T_THIS: pParser->nPos++; return node_new(N_THIS, pTok->nLine);

        case T_NULL: pParser->nPos++; return node_new(N_NULL, pTok->nLine);

        case T_TRUE:
        case T_FALSE:
            pNode = node_new(N_BOOL, pTok->nLine);
            pNode->nNum = (pTok->type == T_TRUE) ? 1 : 0;
            pParser->nPos++;
            return pNode;

        case T_LBRACKET: return parse_array_literal(pParser);
        case T_LBRACE: return parse_object_literal(pParser);
        case T_FUNCTION: return parse_function(pParser, 1);

        case T_LPAREN: {
            JSNode *pInner = NULL;

            pParser->nPos++;
            pInner = parse_expression(pParser, 0);
            expect(pParser, T_RPAREN, "')'");

            return pInner;
        }

        default: break;
    }

    parse_error(pParser, "unexpected token in expression");

    return NULL;
}

static void parse_arguments(JSParser *pParser, JSNode *pNode)
{
    expect(pParser, T_LPAREN, "'('");

    while ((tok(pParser)->type != T_RPAREN) && (tok(pParser)->type != T_EOF)) {
        node_add(pNode, parse_assignment(pParser, 0));

        if (!accept(pParser, T_COMMA)) {
            break;
        }
    }

    expect(pParser, T_RPAREN, "')'");
}

static JSNode *parse_member_tail(JSParser *pParser, JSNode *pBase, int bAllowCall)
{
    for (;;) {
        if (tok(pParser)->type == T_DOT) {
            JSNode *pNode = node_new(N_MEMBER, tok(pParser)->nLine);

            pParser->nPos++;
            pNode->a = pBase;
            pNode->pStr = property_name(pParser);
            pBase = pNode;
        } else if (tok(pParser)->type == T_LBRACKET) {
            JSNode *pNode = node_new(N_INDEX, tok(pParser)->nLine);

            pParser->nPos++;
            pNode->a = pBase;
            pNode->b = parse_expression(pParser, 0);
            expect(pParser, T_RBRACKET, "']'");
            pBase = pNode;
        } else if (bAllowCall && (tok(pParser)->type == T_LPAREN)) {
            JSNode *pNode = node_new(N_CALL, tok(pParser)->nLine);

            pNode->a = pBase;
            parse_arguments(pParser, pNode);
            pBase = pNode;
        } else {
            break;
        }
    }

    return pBase;
}

static JSNode *parse_new(JSParser *pParser)
{
    JSNode *pNode = NULL;
    JSNode *pCallee = NULL;

    if (!enter_recursion(pParser)) {
        return NULL;
    }

    pNode = node_new(N_NEW, tok(pParser)->nLine);

    expect(pParser, T_NEW, "'new'");

    if (tok(pParser)->type == T_NEW) {
        pCallee = parse_new(pParser);
    } else {
        pCallee = parse_primary(pParser);
        pCallee = parse_member_tail(pParser, pCallee, 0);
    }

    pNode->a = pCallee;

    if (tok(pParser)->type == T_LPAREN) {
        parse_arguments(pParser, pNode);
    }

    pParser->nDepth--;

    return parse_member_tail(pParser, pNode, 1);
}

static JSNode *parse_left_hand_side(JSParser *pParser)
{
    JSNode *pNode = NULL;

    if (tok(pParser)->type == T_NEW) {
        return parse_new(pParser);
    }

    pNode = parse_primary(pParser);

    return parse_member_tail(pParser, pNode, 1);
}

static JSNode *parse_postfix(JSParser *pParser)
{
    JSNode *pNode = parse_left_hand_side(pParser);

    if (((tok(pParser)->type == T_INC) || (tok(pParser)->type == T_DEC)) && (!tok(pParser)->bNewLineBefore)) {
        JSNode *pUpdate = node_new(N_UPDATE, tok(pParser)->nLine);

        pUpdate->op = (tok(pParser)->type == T_INC) ? OP_INC : OP_DEC;
        pUpdate->nNum = 0; /* postfix */
        pUpdate->a = pNode;
        pParser->nPos++;

        return pUpdate;
    }

    return pNode;
}

static JSNode *parse_unary_inner(JSParser *pParser)
{
    JSToken *pTok = tok(pParser);
    JSOp op = OP_NONE;

    switch (pTok->type) {
        case T_NOT: op = OP_NOT; break;
        case T_BNOT: op = OP_BNOT; break;
        case T_ADD: op = OP_POS; break;
        case T_SUB: op = OP_NEG; break;
        case T_TYPEOF: op = OP_TYPEOF; break;
        case T_VOID: op = OP_VOID; break;
        case T_DELETE: op = OP_DELETE; break;
        default: break;
    }

    if (op != OP_NONE) {
        JSNode *pNode = node_new(N_UNARY, pTok->nLine);

        pParser->nPos++;
        pNode->op = op;
        pNode->a = parse_unary(pParser);

        return pNode;
    }

    if ((pTok->type == T_INC) || (pTok->type == T_DEC)) {
        JSNode *pNode = node_new(N_UPDATE, pTok->nLine);

        pParser->nPos++;
        pNode->op = (pTok->type == T_INC) ? OP_INC : OP_DEC;
        pNode->nNum = 1; /* prefix */
        pNode->a = parse_unary(pParser);

        return pNode;
    }

    return parse_postfix(pParser);
}

static JSNode *parse_unary(JSParser *pParser)
{
    JSNode *pNode = NULL;

    if (!enter_recursion(pParser)) {
        return NULL;
    }

    pNode = parse_unary_inner(pParser);
    pParser->nDepth--;

    return pNode;
}

typedef struct {
    JSTokType token;
    JSOp op;
    int nPrecedence;
} BinOpInfo;

static const BinOpInfo g_binOps[] = {{T_MUL, OP_MUL, 11},
                                     {T_DIV, OP_DIV, 11},
                                     {T_MOD, OP_MOD, 11},
                                     {T_ADD, OP_ADD, 10},
                                     {T_SUB, OP_SUB, 10},
                                     {T_SHL, OP_SHL, 9},
                                     {T_SHR, OP_SHR, 9},
                                     {T_USHR, OP_USHR, 9},
                                     {T_LT, OP_LT, 8},
                                     {T_GT, OP_GT, 8},
                                     {T_LE, OP_LE, 8},
                                     {T_GE, OP_GE, 8},
                                     {T_INSTANCEOF, OP_INSTANCEOF, 8},
                                     {T_IN, OP_IN, 8},
                                     {T_EQ, OP_EQ, 7},
                                     {T_NE, OP_NE, 7},
                                     {T_SEQ, OP_SEQ, 7},
                                     {T_SNE, OP_SNE, 7},
                                     {T_AND, OP_AND, 6},
                                     {T_XOR, OP_XOR, 5},
                                     {T_OR, OP_OR, 4},
                                     {T_LAND, OP_LAND, 3},
                                     {T_LOR, OP_LOR, 2},
                                     {T_EOF, OP_NONE, 0}};

static JSNode *parse_binary(JSParser *pParser, int nMinPrecedence, int bNoIn)
{
    JSNode *pLeft = parse_unary(pParser);

    for (;;) {
        int i = 0;
        const BinOpInfo *pInfo = NULL;

        for (i = 0; g_binOps[i].nPrecedence; i++) {
            if (g_binOps[i].token == tok(pParser)->type) {
                pInfo = &g_binOps[i];
                break;
            }
        }

        if (pInfo == NULL) {
            break;
        }

        if (bNoIn && (pInfo->op == OP_IN)) {
            break;
        }

        if (pInfo->nPrecedence < nMinPrecedence) {
            break;
        }

        {
            JSNode *pNode = node_new((pInfo->op == OP_LAND) || (pInfo->op == OP_LOR) ? N_LOGICAL : N_BINARY, tok(pParser)->nLine);

            pParser->nPos++;
            pNode->op = pInfo->op;
            pNode->a = pLeft;
            pNode->b = parse_binary(pParser, pInfo->nPrecedence + 1, bNoIn);
            pLeft = pNode;
        }
    }

    return pLeft;
}

static JSNode *parse_conditional(JSParser *pParser, int bNoIn)
{
    JSNode *pTest = parse_binary(pParser, 1, bNoIn);

    if (tok(pParser)->type == T_QUESTION) {
        JSNode *pNode = node_new(N_COND, tok(pParser)->nLine);

        pParser->nPos++;
        pNode->a = pTest;
        pNode->b = parse_assignment(pParser, 0);
        expect(pParser, T_COLON, "':'");
        pNode->c = parse_assignment(pParser, bNoIn);

        return pNode;
    }

    return pTest;
}

static JSOp assign_op(JSTokType type)
{
    switch (type) {
        case T_ASSIGN: return OP_ASSIGN;
        case T_ADD_ASSIGN: return OP_ADD;
        case T_SUB_ASSIGN: return OP_SUB;
        case T_MUL_ASSIGN: return OP_MUL;
        case T_DIV_ASSIGN: return OP_DIV;
        case T_MOD_ASSIGN: return OP_MOD;
        case T_AND_ASSIGN: return OP_AND;
        case T_OR_ASSIGN: return OP_OR;
        case T_XOR_ASSIGN: return OP_XOR;
        case T_SHL_ASSIGN: return OP_SHL;
        case T_SHR_ASSIGN: return OP_SHR;
        case T_USHR_ASSIGN: return OP_USHR;
        default: return OP_NONE;
    }
}

static JSNode *parse_assignment(JSParser *pParser, int bNoIn)
{
    JSNode *pLeft = NULL;
    JSOp op = OP_NONE;

    if (!enter_recursion(pParser)) {
        return NULL;
    }

    pLeft = parse_conditional(pParser, bNoIn);
    op = assign_op(tok(pParser)->type);

    if (op != OP_NONE) {
        JSNode *pNode = NULL;

        if ((pLeft->type != N_IDENT) && (pLeft->type != N_MEMBER) && (pLeft->type != N_INDEX)) {
            parse_error(pParser, "invalid assignment target");
        }

        pNode = node_new(N_ASSIGN, tok(pParser)->nLine);
        pNode->op = op;
        pParser->nPos++;
        pNode->a = pLeft;
        pNode->b = parse_assignment(pParser, bNoIn);
        pLeft = pNode;
    }

    pParser->nDepth--;

    return pLeft;
}

static JSNode *parse_expression(JSParser *pParser, int bNoIn)
{
    JSNode *pNode = parse_assignment(pParser, bNoIn);

    while (tok(pParser)->type == T_COMMA) {
        JSNode *pSeq = node_new(N_SEQ, tok(pParser)->nLine);

        pParser->nPos++;
        pSeq->a = pNode;
        pSeq->b = parse_assignment(pParser, bNoIn);
        pNode = pSeq;
    }

    return pNode;
}

/* ------------------------------------------------------------- functions  */

static JSNode *parse_block(JSParser *pParser)
{
    JSNode *pNode = node_new(N_BLOCK, tok(pParser)->nLine);

    expect(pParser, T_LBRACE, "'{'");

    while ((tok(pParser)->type != T_RBRACE) && (tok(pParser)->type != T_EOF)) {
        node_add(pNode, parse_statement(pParser));
    }

    expect(pParser, T_RBRACE, "'}'");

    return pNode;
}

static JSNode *parse_function(JSParser *pParser, int bExpression)
{
    JSNode *pNode = node_new(N_FUNCTION, tok(pParser)->nLine);

    expect(pParser, T_FUNCTION, "'function'");

    if (tok(pParser)->type == T_IDENT) {
        pNode->pStr = take_token_text(tok(pParser));
        pParser->nPos++;
    } else if (!bExpression) {
        parse_error(pParser, "expected function name");
    }

    pNode->a = node_new(N_BLOCK, tok(pParser)->nLine); /* parameter list holder */

    expect(pParser, T_LPAREN, "'('");

    while ((tok(pParser)->type != T_RPAREN) && (tok(pParser)->type != T_EOF)) {
        JSNode *pParam = node_new(N_IDENT, tok(pParser)->nLine);

        if (tok(pParser)->type != T_IDENT) {
            parse_error(pParser, "expected parameter name");
        }

        pParam->pStr = take_token_text(tok(pParser));
        pParser->nPos++;
        node_add(pNode->a, pParam);

        if (!accept(pParser, T_COMMA)) {
            break;
        }
    }

    expect(pParser, T_RPAREN, "')'");

    pNode->b = parse_block(pParser);
    pNode->nNum = bExpression ? 1 : 0;

    return pNode;
}

/* ------------------------------------------------------------ statements  */

static JSNode *parse_var_statement(JSParser *pParser, int bNoIn)
{
    JSNode *pNode = node_new(N_VAR, tok(pParser)->nLine);

    expect(pParser, T_VAR, "'var'");

    for (;;) {
        JSNode *pDecl = node_new(N_VARDECL, tok(pParser)->nLine);

        if (tok(pParser)->type != T_IDENT) {
            parse_error(pParser, "expected variable name");
        }

        pDecl->pStr = take_token_text(tok(pParser));
        pParser->nPos++;

        if (accept(pParser, T_ASSIGN)) {
            pDecl->a = parse_assignment(pParser, bNoIn);
        }

        node_add(pNode, pDecl);

        if (!accept(pParser, T_COMMA)) {
            break;
        }
    }

    return pNode;
}

static JSNode *parse_for(JSParser *pParser)
{
    int nLine = tok(pParser)->nLine;
    JSNode *pInit = NULL;

    expect(pParser, T_FOR, "'for'");
    expect(pParser, T_LPAREN, "'('");

    if (tok(pParser)->type == T_SEMI) {
        pInit = NULL;
    } else if (tok(pParser)->type == T_VAR) {
        pInit = parse_var_statement(pParser, 1);
    } else {
        pInit = node_new(N_EXPRSTMT, nLine);
        pInit->a = parse_expression(pParser, 1);
    }

    if (tok(pParser)->type == T_IN) {
        JSNode *pNode = node_new(N_FORIN, nLine);

        /* for (var a, b in o) is an early error: one binding only. */
        if (pInit && (pInit->type == N_VAR) && (pInit->nList != 1)) {
            parse_error(pParser, "invalid for-in variable declaration");
        }
        pParser->nPos++;
        pNode->a = pInit;
        pNode->b = parse_expression(pParser, 0);
        expect(pParser, T_RPAREN, "')'");
        pNode->d = parse_statement(pParser);

        return pNode;
    }

    {
        JSNode *pNode = node_new(N_FOR, nLine);

        pNode->a = pInit;
        expect(pParser, T_SEMI, "';'");

        if (tok(pParser)->type != T_SEMI) {
            pNode->b = parse_expression(pParser, 0);
        }

        expect(pParser, T_SEMI, "';'");

        if (tok(pParser)->type != T_RPAREN) {
            pNode->c = parse_expression(pParser, 0);
        }

        expect(pParser, T_RPAREN, "')'");
        pNode->d = parse_statement(pParser);

        return pNode;
    }
}

static JSNode *parse_switch(JSParser *pParser)
{
    JSNode *pNode = node_new(N_SWITCH, tok(pParser)->nLine);

    expect(pParser, T_SWITCH, "'switch'");
    expect(pParser, T_LPAREN, "'('");
    pNode->a = parse_expression(pParser, 0);
    expect(pParser, T_RPAREN, "')'");
    expect(pParser, T_LBRACE, "'{'");

    while ((tok(pParser)->type != T_RBRACE) && (tok(pParser)->type != T_EOF)) {
        JSNode *pCase = node_new(N_CASE, tok(pParser)->nLine);

        if (accept(pParser, T_CASE)) {
            pCase->a = parse_expression(pParser, 0);
        } else {
            expect(pParser, T_DEFAULT, "'case' or 'default'");
        }

        expect(pParser, T_COLON, "':'");

        while ((tok(pParser)->type != T_CASE) && (tok(pParser)->type != T_DEFAULT) && (tok(pParser)->type != T_RBRACE) && (tok(pParser)->type != T_EOF)) {
            node_add(pCase, parse_statement(pParser));
        }

        node_add(pNode, pCase);
    }

    expect(pParser, T_RBRACE, "'}'");

    return pNode;
}

static JSNode *parse_try(JSParser *pParser)
{
    JSNode *pNode = node_new(N_TRY, tok(pParser)->nLine);

    expect(pParser, T_TRY, "'try'");
    pNode->a = parse_block(pParser);

    if (accept(pParser, T_CATCH)) {
        if (accept(pParser, T_LPAREN)) {
            if (tok(pParser)->type != T_IDENT) {
                parse_error(pParser, "expected catch parameter");
            }

            pNode->pStr = take_token_text(tok(pParser));
            pParser->nPos++;
            expect(pParser, T_RPAREN, "')'");
        }

        pNode->b = parse_block(pParser);
    }

    if (accept(pParser, T_FINALLY)) {
        pNode->c = parse_block(pParser);
    }
    if (!pNode->b && !pNode->c) {
        parse_error(pParser, "missing catch or finally after try");
    }

    return pNode;
}

static JSNode *parse_statement_inner(JSParser *pParser)
{
    JSToken *pTok = tok(pParser);

    switch (pTok->type) {
        case T_LBRACE: return parse_block(pParser);

        case T_SEMI: pParser->nPos++; return node_new(N_EMPTY, pTok->nLine);
        case T_DEBUGGER: {
            JSNode *pNode = node_new(N_DEBUGGER, pTok->nLine);
            pParser->nPos++;
            consume_semicolon(pParser);
            return pNode;
        }

        case T_VAR: {
            JSNode *pNode = parse_var_statement(pParser, 0);

            consume_semicolon(pParser);

            return pNode;
        }

        case T_FUNCTION: return parse_function(pParser, 0);

        case T_IF: {
            JSNode *pNode = node_new(N_IF, pTok->nLine);

            pParser->nPos++;
            expect(pParser, T_LPAREN, "'('");
            pNode->a = parse_expression(pParser, 0);
            expect(pParser, T_RPAREN, "')'");
            pNode->b = parse_statement(pParser);

            if (accept(pParser, T_ELSE)) {
                pNode->c = parse_statement(pParser);
            }

            return pNode;
        }

        case T_WHILE: {
            JSNode *pNode = node_new(N_WHILE, pTok->nLine);

            pParser->nPos++;
            expect(pParser, T_LPAREN, "'('");
            pNode->a = parse_expression(pParser, 0);
            expect(pParser, T_RPAREN, "')'");
            pNode->d = parse_statement(pParser);

            return pNode;
        }

        case T_DO: {
            JSNode *pNode = node_new(N_DOWHILE, pTok->nLine);

            pParser->nPos++;
            pNode->d = parse_statement(pParser);
            expect(pParser, T_WHILE, "'while'");
            expect(pParser, T_LPAREN, "'('");
            pNode->a = parse_expression(pParser, 0);
            expect(pParser, T_RPAREN, "')'");
            accept(pParser, T_SEMI);

            return pNode;
        }

        case T_FOR: return parse_for(pParser);
        case T_SWITCH: return parse_switch(pParser);
        case T_TRY: return parse_try(pParser);

        case T_RETURN: {
            JSNode *pNode = node_new(N_RETURN, pTok->nLine);

            pParser->nPos++;

            if ((tok(pParser)->type != T_SEMI) && (tok(pParser)->type != T_RBRACE) && (tok(pParser)->type != T_EOF) && (!tok(pParser)->bNewLineBefore)) {
                pNode->a = parse_expression(pParser, 0);
            }

            consume_semicolon(pParser);

            return pNode;
        }

        case T_THROW: {
            JSNode *pNode = node_new(N_THROW, pTok->nLine);

            pParser->nPos++;
            pNode->a = parse_expression(pParser, 0);
            consume_semicolon(pParser);

            return pNode;
        }

        case T_BREAK:
        case T_CONTINUE: {
            JSNode *pNode = node_new((pTok->type == T_BREAK) ? N_BREAK : N_CONTINUE, pTok->nLine);

            pParser->nPos++;

            if ((tok(pParser)->type == T_IDENT) && (!tok(pParser)->bNewLineBefore)) {
                pNode->pStr = take_token_text(tok(pParser));
                pParser->nPos++;
            }

            consume_semicolon(pParser);

            return pNode;
        }

        default: break;
    }

    /* Labelled statement. */
    if ((pTok->type == T_IDENT) && (tok_at(pParser, 1)->type == T_COLON)) {
        JSNode *pNode = node_new(N_LABELED, pTok->nLine);

        pNode->pStr = take_token_text(pTok);
        pParser->nPos += 2;
        pNode->a = parse_statement(pParser);

        return pNode;
    }

    {
        JSNode *pNode = node_new(N_EXPRSTMT, pTok->nLine);

        pNode->a = parse_expression(pParser, 0);
        consume_semicolon(pParser);

        return pNode;
    }
}

static JSNode *parse_statement(JSParser *pParser)
{
    JSNode *pNode = NULL;

    if (!enter_recursion(pParser)) {
        return NULL;
    }

    {
        int nLine = tok(pParser)->nLine;
        int nColumn = tok(pParser)->nColumn;
        pNode = parse_statement_inner(pParser);
        if (pNode) { pNode->nLine = nLine; pNode->nColumn = nColumn; }
    }
    pParser->nDepth--;

    return pNode;
}

/* ---------------------------------------------------------------- driver  */

JSNode *js_parse_program(JSCtx *pCtx, const char *pSource, const char *pName, char **ppError)
{
    JSParser parser;
    JSNode *pProgram = NULL;
    char *pLexError = NULL;

    (void)pCtx;

    xx_rt_memset(&parser, 0, sizeof(parser));
    parser.pName = pName;

    if (!js_lex_run(&parser.lexer, pSource, &pLexError)) {
        if (ppError) {
            *ppError = pLexError;
        } else {
            xx_js_free(pLexError);
        }

        js_lex_free(&parser.lexer);

        return NULL;
    }

    pProgram = js_node_new(N_PROGRAM, 1);
    pProgram->nColumn = 1;
    js_node_set_source(pProgram, pName);

    while ((tok(&parser)->type != T_EOF) && (!parser.bError)) {
        node_add(pProgram, parse_statement(&parser));
    }

    js_lex_free(&parser.lexer);

    if (parser.bError) {
        js_free_node(pProgram);

        if (ppError) {
            *ppError = parser.pError;
        } else {
            xx_js_free(parser.pError);
        }

        return NULL;
    }

    js_node_set_source(pProgram, NULL);
    js_prepare_program(pProgram);
    return pProgram;
}
