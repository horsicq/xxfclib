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

#define BC_FLAG_STR      0x0001
#define BC_FLAG_STR2     0x0002
#define BC_FLAG_NODE_A   0x0004
#define BC_FLAG_NODE_B   0x0008
#define BC_FLAG_NODE_C   0x0010
#define BC_FLAG_NODE_D   0x0020
#define BC_FLAG_LIST     0x0040
#define BC_FLAG_VARNAMES 0x0080
#define BC_FLAG_NUM      0x0100

int js_is_bytecode(const void *pData, size_t nSize)
{
    const uint8_t *p = (const uint8_t *)pData;
    if (!p || nSize < 8) {
        return 0;
    }
    return (p[0] == JS_BC_MAGIC_0 && p[1] == JS_BC_MAGIC_1 &&
            p[2] == JS_BC_MAGIC_2 && p[3] == JS_BC_MAGIC_3);
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
