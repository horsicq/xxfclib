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

/* xx_js_bytecode.c - binary AST / bytecode serialization and deserialization. */

#include "xx_js_internal.h"
#include "xx_js_ast.h"
#include "xxfclib/buf/xx_buf.h"

#define JS_BC_MAGIC_0 0x1B
#define JS_BC_MAGIC_1 'D'
#define JS_BC_MAGIC_2 'I'
#define JS_BC_MAGIC_3 'E'
#define JS_BC_VERSION 2

#define BC_FLAG_STR 0x0001
#define BC_FLAG_STR2 0x0002
#define BC_FLAG_NODE_A 0x0004
#define BC_FLAG_NODE_B 0x0008
#define BC_FLAG_NODE_C 0x0010
#define BC_FLAG_NODE_D 0x0020
#define BC_FLAG_LIST 0x0040
#define BC_FLAG_VARNAMES 0x0080
#define BC_FLAG_NUM 0x0100

int js_is_bytecode(const void *pData, size_t nSize)
{
    const uint8_t *p = (const uint8_t *)pData;
    if (!p || nSize < 8) {
        return 0;
    }
    return (p[0] == JS_BC_MAGIC_0 && p[1] == JS_BC_MAGIC_1 && p[2] == JS_BC_MAGIC_2 && p[3] == JS_BC_MAGIC_3);
}

/* ----------------------------------------------------------- serialization  */

static void serialize_node(xx_buf_t *pBuf, const JSNode *pNode)
{
    uint8_t type = 0;
    uint8_t op = 0;
    int32_t line = 0;
    uint16_t flags = 0;

    if (!pNode) {
        return;
    }

    type = (uint8_t)pNode->type;
    op = (uint8_t)pNode->op;
    line = (int32_t)pNode->nLine;

    xx_buf_append(pBuf, &type, 1);
    xx_buf_append(pBuf, &op, 1);
    xx_buf_append(pBuf, &line, sizeof(line));

    if (pNode->pStr) flags |= BC_FLAG_STR;
    if (pNode->pStr2) flags |= BC_FLAG_STR2;
    if (pNode->a) flags |= BC_FLAG_NODE_A;
    if (pNode->b) flags |= BC_FLAG_NODE_B;
    if (pNode->c) flags |= BC_FLAG_NODE_C;
    if (pNode->d) flags |= BC_FLAG_NODE_D;
    if (pNode->nList > 0 && pNode->ppList) flags |= BC_FLAG_LIST;
    if (pNode->nVarNames > 0 && pNode->ppVarNames) flags |= BC_FLAG_VARNAMES;
    if (pNode->type == N_NUM || pNode->nNum != 0.0) flags |= BC_FLAG_NUM;

    xx_buf_append(pBuf, &flags, sizeof(flags));

    if (flags & BC_FLAG_NUM) {
        xx_buf_append(pBuf, &pNode->nNum, sizeof(double));
    }

    if (flags & BC_FLAG_STR) {
        uint32_t len = 0;
        if ((pNode->type == N_STR || pNode->type == N_PROP) && pNode->nNum > 0.0) {
            len = (uint32_t)pNode->nNum;
        } else {
            len = (uint32_t)xx_rt_strlen(pNode->pStr);
        }
        xx_buf_append(pBuf, &len, sizeof(len));
        xx_buf_append(pBuf, pNode->pStr, len);
    }
    if (flags & BC_FLAG_STR2) {
        uint32_t len = (uint32_t)xx_rt_strlen(pNode->pStr2);
        xx_buf_append(pBuf, &len, sizeof(len));
        xx_buf_append(pBuf, pNode->pStr2, len);
    }
    if (flags & BC_FLAG_NODE_A) serialize_node(pBuf, pNode->a);
    if (flags & BC_FLAG_NODE_B) serialize_node(pBuf, pNode->b);
    if (flags & BC_FLAG_NODE_C) serialize_node(pBuf, pNode->c);
    if (flags & BC_FLAG_NODE_D) serialize_node(pBuf, pNode->d);

    if (flags & BC_FLAG_LIST) {
        uint32_t count = (uint32_t)pNode->nList;
        size_t i = 0;
        xx_buf_append(pBuf, &count, sizeof(count));
        for (i = 0; i < pNode->nList; i++) {
            if (pNode->ppList[i] == NULL) {
                uint8_t marker = 0;
                xx_buf_append(pBuf, &marker, 1);
            } else {
                uint8_t marker = 1;
                xx_buf_append(pBuf, &marker, 1);
                serialize_node(pBuf, pNode->ppList[i]);
            }
        }
    }
    if (flags & BC_FLAG_VARNAMES) {
        uint32_t count = (uint32_t)pNode->nVarNames;
        size_t i = 0;
        xx_buf_append(pBuf, &count, sizeof(count));
        for (i = 0; i < pNode->nVarNames; i++) {
            uint32_t len = (uint32_t)xx_rt_strlen(pNode->ppVarNames[i]);
            xx_buf_append(pBuf, &len, sizeof(len));
            xx_buf_append(pBuf, pNode->ppVarNames[i], len);
        }
    }
}

void *js_compile_to_bytecode(JSCtx *pCtx, const char *pSource, const char *pName, size_t *pOutSize)
{
    char *pError = NULL;
    JSNode *pProgram = NULL;
    xx_buf_t buf;
    uint8_t header[8];
    uint16_t version = JS_BC_VERSION;
    uint16_t reserved = 0;

    if (!pCtx || !pSource) {
        return NULL;
    }

    pProgram = js_parse_program(pCtx, pSource, pName, &pError);
    if (!pProgram) {
        if (pError) {
            xx_js_free(pError);
        }
        return NULL;
    }

    xx_buf_init(&buf);

    header[0] = JS_BC_MAGIC_0;
    header[1] = JS_BC_MAGIC_1;
    header[2] = JS_BC_MAGIC_2;
    header[3] = JS_BC_MAGIC_3;
    xx_rt_memcpy(&header[4], &version, sizeof(version));
    xx_rt_memcpy(&header[6], &reserved, sizeof(reserved));

    xx_buf_append(&buf, header, sizeof(header));
    serialize_node(&buf, pProgram);

    js_free_node(pProgram);

    if (!xx_buf_ok(&buf)) {
        xx_buf_free(&buf);
        return NULL;
    }

    return xx_buf_detach(&buf, pOutSize);
}

/* --------------------------------------------------------- deserialization  */

typedef struct {
    const uint8_t *pData;
    size_t nSize;
    size_t nPos;
    int bError;
} BcReader;

static bool bc_read(BcReader *r, void *dest, size_t len)
{
    if (r->bError || (r->nPos + len > r->nSize)) {
        r->bError = 1;
        return false;
    }
    xx_rt_memcpy(dest, r->pData + r->nPos, len);
    r->nPos += len;
    return true;
}

static char *bc_read_str(BcReader *r)
{
    uint32_t len = 0;
    char *s = NULL;

    if (!bc_read(r, &len, sizeof(len))) {
        return NULL;
    }
    if (r->nPos + len > r->nSize) {
        r->bError = 1;
        return NULL;
    }

    s = (char *)xx_js_malloc(len + 1);
    xx_rt_memcpy(s, r->pData + r->nPos, len);
    s[len] = '\0';
    r->nPos += len;
    return s;
}

static JSNode *deserialize_node(BcReader *r)
{
    uint8_t type = 0;
    uint8_t op = 0;
    int32_t line = 0;
    uint16_t flags = 0;
    JSNode *pNode = NULL;

    if (r->bError || (r->nPos >= r->nSize)) {
        return NULL;
    }

    if (!bc_read(r, &type, 1) || !bc_read(r, &op, 1) || !bc_read(r, &line, sizeof(line))) {
        return NULL;
    }

    pNode = js_node_new((JSNodeType)type, (int)line);
    pNode->op = (JSOp)op;

    if (!bc_read(r, &flags, sizeof(flags))) {
        js_free_node(pNode);
        return NULL;
    }

    if (flags & BC_FLAG_NUM) {
        if (!bc_read(r, &pNode->nNum, sizeof(double))) {
            js_free_node(pNode);
            return NULL;
        }
    }

    if (flags & BC_FLAG_STR) pNode->pStr = bc_read_str(r);
    if (flags & BC_FLAG_STR2) pNode->pStr2 = bc_read_str(r);
    if (flags & BC_FLAG_NODE_A) pNode->a = deserialize_node(r);
    if (flags & BC_FLAG_NODE_B) pNode->b = deserialize_node(r);
    if (flags & BC_FLAG_NODE_C) pNode->c = deserialize_node(r);
    if (flags & BC_FLAG_NODE_D) pNode->d = deserialize_node(r);

    if (flags & BC_FLAG_LIST) {
        uint32_t count = 0;
        if (bc_read(r, &count, sizeof(count)) && count > 0) {
            uint32_t i = 0;
            pNode->ppList = (JSNode **)xx_js_calloc(count, sizeof(JSNode *));
            pNode->nList = count;
            for (i = 0; i < count; i++) {
                uint8_t marker = 0;
                if (!bc_read(r, &marker, 1)) {
                    r->bError = 1;
                    break;
                }
                if (marker != 0) {
                    pNode->ppList[i] = deserialize_node(r);
                } else {
                    pNode->ppList[i] = NULL;
                }
            }
        }
    }

    if (flags & BC_FLAG_VARNAMES) {
        uint32_t count = 0;
        if (bc_read(r, &count, sizeof(count)) && count > 0) {
            uint32_t i = 0;
            pNode->ppVarNames = (char **)xx_js_calloc(count, sizeof(char *));
            pNode->nVarNames = count;
            for (i = 0; i < count; i++) {
                pNode->ppVarNames[i] = bc_read_str(r);
            }
        }
    }

    if (r->bError) {
        js_free_node(pNode);
        return NULL;
    }

    return pNode;
}

int js_eval_nested_bytecode(JSCtx *pCtx, const void *pBytecode, size_t nSize, const char *pName)
{
    BcReader r;
    JSNode *pProgram = NULL;
    JSVal value;

    (void)pName;

    if (!pCtx || !js_is_bytecode(pBytecode, nSize)) {
        if (pCtx) {
            js_throw(pCtx, "Invalid bytecode format");
        }
        return 0;
    }

    r.pData = (const uint8_t *)pBytecode;
    r.nSize = nSize;
    r.nPos = 8; /* skip 8-byte header */
    r.bError = 0;

    pProgram = deserialize_node(&r);
    if (!pProgram || r.bError) {
        if (pProgram) {
            js_free_node(pProgram);
        }
        js_throw(pCtx, "Corrupt bytecode stream");
        return 0;
    }

    xx_list_append(&pCtx->vecPrograms, &pProgram);

    value = js_run_program(pCtx, pProgram, pCtx->pGlobalScope, jsval_obj(jsobj_ref(pCtx->pGlobal)));
    js_release(pCtx, value);

    return pCtx->bException ? 0 : 1;
}

/* ------------------------------------------------------------- decompiler  */

static const char *bc_op_str(JSOp op)
{
    switch (op) {
        case OP_ADD: return "+";
        case OP_SUB: return "-";
        case OP_MUL: return "*";
        case OP_DIV: return "/";
        case OP_MOD: return "%";
        case OP_LT: return "<";
        case OP_GT: return ">";
        case OP_LE: return "<=";
        case OP_GE: return ">=";
        case OP_EQ: return "==";
        case OP_NE: return "!=";
        case OP_SEQ: return "===";
        case OP_SNE: return "!==";
        case OP_AND: return "&";
        case OP_OR: return "|";
        case OP_XOR: return "^";
        case OP_SHL: return "<<";
        case OP_SHR: return ">>";
        case OP_USHR: return ">>>";
        case OP_LAND: return "&&";
        case OP_LOR: return "||";
        case OP_NOT: return "!";
        case OP_BNOT: return "~";
        case OP_NEG: return "-";
        case OP_POS: return "+";
        case OP_TYPEOF: return "typeof ";
        case OP_VOID: return "void ";
        case OP_DELETE: return "delete ";
        case OP_IN: return " in ";
        case OP_INSTANCEOF: return " instanceof ";
        case OP_INC: return "++";
        case OP_DEC: return "--";
        case OP_ASSIGN: return "=";
        default: return "";
    }
}

static void bc_print_escaped_str(xx_buf_t *buf, const char *str)
{
    xx_buf_append_str(buf, "\"");
    if (str) {
        const char *p = str;
        while (*p) {
            switch (*p) {
                case '\"': xx_buf_append_str(buf, "\\\""); break;
                case '\\': xx_buf_append_str(buf, "\\\\"); break;
                case '\n': xx_buf_append_str(buf, "\\n"); break;
                case '\r': xx_buf_append_str(buf, "\\r"); break;
                case '\t': xx_buf_append_str(buf, "\\t"); break;
                default: {
                    unsigned char c = (unsigned char)*p;
                    if (c < 32) {
                        char tmp[8];
                        xx_rt_snprintf(tmp, sizeof(tmp), "\\x%02x", c);
                        xx_buf_append_str(buf, tmp);
                    } else {
                        xx_buf_append(buf, p, 1);
                    }
                    break;
                }
            }
            p++;
        }
    }
    xx_buf_append_str(buf, "\"");
}

static void bc_decompile_node(xx_buf_t *buf, const JSNode *node)
{
    if (!node) return;

    switch (node->type) {
        case N_PROGRAM: {
            size_t i;
            for (i = 0; i < node->nList; i++) {
                bc_decompile_node(buf, node->ppList[i]);
                if (node->ppList[i] && node->ppList[i]->type != N_FUNCTION && node->ppList[i]->type != N_IF && node->ppList[i]->type != N_FOR &&
                    node->ppList[i]->type != N_WHILE && node->ppList[i]->type != N_BLOCK) {
                    xx_buf_append_str(buf, ";\n");
                } else {
                    xx_buf_append_str(buf, "\n");
                }
            }
            break;
        }
        case N_BLOCK: {
            size_t i;
            xx_buf_append_str(buf, "{\n");
            for (i = 0; i < node->nList; i++) {
                bc_decompile_node(buf, node->ppList[i]);
                if (node->ppList[i] && node->ppList[i]->type != N_FUNCTION && node->ppList[i]->type != N_IF && node->ppList[i]->type != N_FOR &&
                    node->ppList[i]->type != N_WHILE && node->ppList[i]->type != N_BLOCK) {
                    xx_buf_append_str(buf, ";\n");
                } else {
                    xx_buf_append_str(buf, "\n");
                }
            }
            xx_buf_append_str(buf, "}\n");
            break;
        }
        case N_EMPTY: {
            xx_buf_append_str(buf, ";");
            break;
        }
        case N_EXPRSTMT: {
            bc_decompile_node(buf, node->a);
            break;
        }
        case N_NUM: {
            char num_str[64];
            if (node->nNum == (double)(int64_t)node->nNum) {
                xx_rt_snprintf(num_str, sizeof(num_str), "%lld", (long long)node->nNum);
            } else {
                xx_rt_snprintf(num_str, sizeof(num_str), "%.14g", node->nNum);
            }
            xx_buf_append_str(buf, num_str);
            break;
        }
        case N_STR: {
            bc_print_escaped_str(buf, node->pStr);
            break;
        }
        case N_REGEXP: {
            if (node->pStr) {
                if (node->pStr[0] == '/') {
                    xx_buf_append_str(buf, node->pStr);
                } else {
                    xx_buf_append_str(buf, "/");
                    xx_buf_append_str(buf, node->pStr);
                    xx_buf_append_str(buf, "/");
                    if (node->pStr2) xx_buf_append_str(buf, node->pStr2);
                }
            }
            break;
        }
        case N_IDENT: {
            xx_buf_append_str(buf, node->pStr ? node->pStr : "");
            break;
        }
        case N_THIS: {
            xx_buf_append_str(buf, "this");
            break;
        }
        case N_NULL: {
            xx_buf_append_str(buf, "null");
            break;
        }
        case N_BOOL: {
            xx_buf_append_str(buf, node->nNum != 0.0 ? "true" : "false");
            break;
        }
        case N_ARRAY: {
            size_t i;
            xx_buf_append_str(buf, "[");
            for (i = 0; i < node->nList; i++) {
                if (i > 0) xx_buf_append_str(buf, ", ");
                bc_decompile_node(buf, node->ppList[i]);
            }
            xx_buf_append_str(buf, "]");
            break;
        }
        case N_OBJECT: {
            size_t i;
            xx_buf_append_str(buf, "{");
            for (i = 0; i < node->nList; i++) {
                if (i > 0) xx_buf_append_str(buf, ", ");
                bc_decompile_node(buf, node->ppList[i]);
            }
            xx_buf_append_str(buf, "}");
            break;
        }
        case N_PROP: {
            xx_buf_append_str(buf, node->pStr ? node->pStr : "");
            xx_buf_append_str(buf, ": ");
            bc_decompile_node(buf, node->a);
            break;
        }
        case N_FUNCTION: {
            size_t i;
            xx_buf_append_str(buf, "function");
            if (node->pStr && node->pStr[0]) {
                xx_buf_append_str(buf, " ");
                xx_buf_append_str(buf, node->pStr);
            }
            xx_buf_append_str(buf, "(");
            if (node->a && node->a->ppList) {
                for (i = 0; i < node->a->nList; i++) {
                    if (i > 0) xx_buf_append_str(buf, ", ");
                    bc_decompile_node(buf, node->a->ppList[i]);
                }
            }
            xx_buf_append_str(buf, ") ");
            bc_decompile_node(buf, node->b);
            break;
        }
        case N_CALL: {
            size_t i;
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, "(");
            for (i = 0; i < node->nList; i++) {
                if (i > 0) xx_buf_append_str(buf, ", ");
                bc_decompile_node(buf, node->ppList[i]);
            }
            xx_buf_append_str(buf, ")");
            break;
        }
        case N_NEW: {
            size_t i;
            xx_buf_append_str(buf, "new ");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, "(");
            for (i = 0; i < node->nList; i++) {
                if (i > 0) xx_buf_append_str(buf, ", ");
                bc_decompile_node(buf, node->ppList[i]);
            }
            xx_buf_append_str(buf, ")");
            break;
        }
        case N_MEMBER: {
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ".");
            xx_buf_append_str(buf, node->pStr ? node->pStr : "");
            break;
        }
        case N_INDEX: {
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, "[");
            bc_decompile_node(buf, node->b);
            xx_buf_append_str(buf, "]");
            break;
        }
        case N_UNARY: {
            xx_buf_append_str(buf, bc_op_str(node->op));
            xx_buf_append_str(buf, "(");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ")");
            break;
        }
        case N_UPDATE: {
            if (node->nNum == 1.0) {
                xx_buf_append_str(buf, bc_op_str(node->op));
                bc_decompile_node(buf, node->a);
            } else {
                bc_decompile_node(buf, node->a);
                xx_buf_append_str(buf, bc_op_str(node->op));
            }
            break;
        }
        case N_BINARY:
        case N_LOGICAL: {
            xx_buf_append_str(buf, "(");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, " ");
            xx_buf_append_str(buf, bc_op_str(node->op));
            xx_buf_append_str(buf, " ");
            bc_decompile_node(buf, node->b);
            xx_buf_append_str(buf, ")");
            break;
        }
        case N_ASSIGN: {
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, " ");
            if (node->op != OP_ASSIGN && node->op != OP_NONE) {
                xx_buf_append_str(buf, bc_op_str(node->op));
            }
            xx_buf_append_str(buf, "= ");
            bc_decompile_node(buf, node->b);
            break;
        }
        case N_COND: {
            xx_buf_append_str(buf, "(");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, " ? ");
            bc_decompile_node(buf, node->b);
            xx_buf_append_str(buf, " : ");
            bc_decompile_node(buf, node->c);
            xx_buf_append_str(buf, ")");
            break;
        }
        case N_SEQ: {
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ", ");
            bc_decompile_node(buf, node->b);
            break;
        }
        case N_VAR: {
            size_t i;
            xx_buf_append_str(buf, "var ");
            for (i = 0; i < node->nList; i++) {
                if (i > 0) xx_buf_append_str(buf, ", ");
                bc_decompile_node(buf, node->ppList[i]);
            }
            break;
        }
        case N_VARDECL: {
            xx_buf_append_str(buf, node->pStr ? node->pStr : "");
            if (node->a) {
                xx_buf_append_str(buf, " = ");
                bc_decompile_node(buf, node->a);
            }
            break;
        }
        case N_IF: {
            xx_buf_append_str(buf, "if (");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ") ");
            bc_decompile_node(buf, node->b);
            if (node->c) {
                xx_buf_append_str(buf, " else ");
                bc_decompile_node(buf, node->c);
            }
            break;
        }
        case N_FOR: {
            xx_buf_append_str(buf, "for (");
            if (node->a) bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, "; ");
            if (node->b) bc_decompile_node(buf, node->b);
            xx_buf_append_str(buf, "; ");
            if (node->c) bc_decompile_node(buf, node->c);
            xx_buf_append_str(buf, ") ");
            bc_decompile_node(buf, node->d);
            break;
        }
        case N_FORIN: {
            xx_buf_append_str(buf, "for (");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, " in ");
            bc_decompile_node(buf, node->b);
            xx_buf_append_str(buf, ") ");
            bc_decompile_node(buf, node->d);
            break;
        }
        case N_WHILE: {
            xx_buf_append_str(buf, "while (");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ") ");
            bc_decompile_node(buf, node->d);
            break;
        }
        case N_DOWHILE: {
            xx_buf_append_str(buf, "do ");
            bc_decompile_node(buf, node->d);
            xx_buf_append_str(buf, " while (");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ");");
            break;
        }
        case N_RETURN: {
            xx_buf_append_str(buf, "return");
            if (node->a) {
                xx_buf_append_str(buf, " ");
                bc_decompile_node(buf, node->a);
            }
            break;
        }
        case N_BREAK: {
            xx_buf_append_str(buf, "break");
            if (node->pStr && node->pStr[0]) {
                xx_buf_append_str(buf, " ");
                xx_buf_append_str(buf, node->pStr);
            }
            break;
        }
        case N_CONTINUE: {
            xx_buf_append_str(buf, "continue");
            if (node->pStr && node->pStr[0]) {
                xx_buf_append_str(buf, " ");
                xx_buf_append_str(buf, node->pStr);
            }
            break;
        }
        case N_THROW: {
            xx_buf_append_str(buf, "throw ");
            bc_decompile_node(buf, node->a);
            break;
        }
        case N_TRY: {
            xx_buf_append_str(buf, "try ");
            bc_decompile_node(buf, node->a);
            if (node->pStr) {
                xx_buf_append_str(buf, " catch (");
                xx_buf_append_str(buf, node->pStr);
                xx_buf_append_str(buf, ") ");
                bc_decompile_node(buf, node->b);
            }
            if (node->c) {
                xx_buf_append_str(buf, " finally ");
                bc_decompile_node(buf, node->c);
            }
            break;
        }
        case N_SWITCH: {
            size_t i;
            xx_buf_append_str(buf, "switch (");
            bc_decompile_node(buf, node->a);
            xx_buf_append_str(buf, ") {\n");
            for (i = 0; i < node->nList; i++) {
                bc_decompile_node(buf, node->ppList[i]);
            }
            xx_buf_append_str(buf, "}\n");
            break;
        }
        case N_CASE: {
            size_t i;
            if (node->a) {
                xx_buf_append_str(buf, "case ");
                bc_decompile_node(buf, node->a);
                xx_buf_append_str(buf, ":\n");
            } else {
                xx_buf_append_str(buf, "default:\n");
            }
            for (i = 0; i < node->nList; i++) {
                bc_decompile_node(buf, node->ppList[i]);
                xx_buf_append_str(buf, ";\n");
            }
            break;
        }
        case N_LABELED: {
            xx_buf_append_str(buf, node->pStr ? node->pStr : "");
            xx_buf_append_str(buf, ": ");
            bc_decompile_node(buf, node->a);
            break;
        }
        default: break;
    }
}

char *js_decompile_bytecode(const void *pBytecode, size_t nSize)
{
    BcReader r;
    JSNode *pProgram = NULL;
    xx_buf_t buf;
    char *pResult = NULL;

    if (!pBytecode || !js_is_bytecode(pBytecode, nSize)) {
        return NULL;
    }

    r.pData = (const uint8_t *)pBytecode;
    r.nSize = nSize;
    r.nPos = 8;
    r.bError = 0;

    pProgram = deserialize_node(&r);
    if (!pProgram || r.bError) {
        if (pProgram) {
            js_free_node(pProgram);
        }
        return NULL;
    }

    xx_buf_init(&buf);
    bc_decompile_node(&buf, pProgram);
    js_free_node(pProgram);

    if (buf.data) {
        pResult = xx_js_strdup(buf.data);
    }
    xx_buf_free(&buf);

    return pResult;
}

void js_free_decompiled(char *pStr)
{
    if (pStr) {
        xx_rt_free(pStr);
    }
}
