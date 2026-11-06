/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_dotnet.c - .NET script API dispatch and inspection helpers. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_reader.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_cil.h"
#include "../formats/xx_executable_inspect_internal.h"

/* Bounded metadata views for signatures. Format decoding stays native. */
static int net_arg_rid(int argc, JSVal *args, int index, uint32_t limit, uint32_t *rid)
{
    double value;
    if (index >= argc || args[index].tag != JT_NUM) return 0;
    value = args[index].u.n;
    if (!(value >= 1 && value <= limit) || (double)(uint32_t)value != value) return 0;
    *rid = (uint32_t)value; return 1;
}
static uint32_t net_index(xx_dotnet_inspection *net, int64_t offset, int width)
{
    return width == 2 ? xx_exec_u16(net->pe.pInput, offset, false) : xx_exec_u32(net->pe.pInput, offset, false);
}
static int net_name(xx_dotnet_inspection *net, uint32_t index, char *text, size_t capacity, int *truncated)
{
    int64_t offset, remaining; size_t used = 0;
    *truncated = 0; text[0] = 0;
    if (!capacity || net->cli.nStringsSize <= 0 || index >= (uint64_t)net->cli.nStringsSize) return 0;
    offset = net->cli.nStringsOffset + index; remaining = net->cli.nStringsSize - index;
    while (remaining && used < capacity - 1) {
        uint8_t bytes[256]; size_t count = (uint64_t)remaining > sizeof(bytes) ? sizeof(bytes) : (size_t)remaining, j;
        if (count > capacity - used - 1) count = capacity - used - 1;
        if (!xx_exec_read(net->pe.pInput, offset, bytes, count) || (!index && !used && bytes[0])) return 0;
        for (j = 0; j < count; ++j) {
            if (!bytes[j]) { text[used] = 0; return 1; }
            text[used++] = (char)bytes[j];
        }
        offset += (int64_t)count; remaining -= (int64_t)count;
    }
    text[used] = 0;
    if (!remaining) return 0;
    *truncated = 1; return 1;
}
/* MethodList values are sorted TypeDef ranges. Binary search keeps per-query
 * work bounded; pointer-table layouts are outside this narrow owner query. */
static uint32_t net_method_owner(xx_dotnet_inspection *net, uint32_t rid)
{
    uint32_t low = 1, high = net->cli.pRows[2] + 1, candidate = 0, first, next;
    int64_t offset; uint32_t size;
    int delta = 4 + 2 * net->cli.nStringIndexSize + net->cli.nTypeDefOrRefSize + net->cli.nFieldListSize;
    if (net->cli.pRows[5]) return 0;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2;
        if (!xx_dotnet_inspect_table_row(net, 2, mid, &offset, &size)) return 0;
        first = net_index(net, offset + delta, net->cli.nMethodListSize);
        if (!first || first > net->cli.pRows[6] + 1) return 0;
        if (first <= rid) { candidate = mid; low = mid + 1; } else high = mid;
    }
    if (!candidate || !xx_dotnet_inspect_table_row(net, 2, candidate, &offset, &size)) return 0;
    first = net_index(net, offset + delta, net->cli.nMethodListSize); next = net->cli.pRows[6] + 1;
    if (candidate < net->cli.pRows[2]) {
        if (!xx_dotnet_inspect_table_row(net, 2, candidate + 1, &offset, &size)) return 0;
        next = net_index(net, offset + delta, net->cli.nMethodListSize);
    }
    return first <= rid && rid < next && next <= net->cli.pRows[6] + 1 ? candidate : 0;
}
static JSVal net_method_info(DieEngine *engine, JSCtx *ctx, uint32_t rid)
{
    xx_dotnet_inspection *net = &engine->dotnet; xx_dotnet_method_body body = {0};
    int64_t row, blob_offset, type_row; uint32_t row_size, blob_size, name_index, sig_index, rva, owner;
    uint16_t flags, impl; char name[4097], type[4097], space[4097], signature[513];
    int name_cut, type_cut = 0, space_cut = 0, has_body; uint8_t bytes[256]; size_t i;
    JSVal object;
    if (!engine->bHasDotNet || !xx_dotnet_inspect_table_row(net, 6, rid, &row, &row_size)) return js_null();
    rva = xx_exec_u32(net->pe.pInput, row, false); impl = xx_exec_u16(net->pe.pInput, row + 4, false);
    flags = xx_exec_u16(net->pe.pInput, row + 6, false);
    name_index = net_index(net, row + 8, net->cli.nStringIndexSize);
    sig_index = net_index(net, row + 8 + net->cli.nStringIndexSize, net->cli.nBlobIndexSize);
    if (!net_name(net, name_index, name, sizeof(name), &name_cut) ||
        !xx_dotnet_inspect_blob(net, sig_index, &blob_offset, &blob_size)) return js_null();
    if (blob_size > sizeof(bytes) || !xx_exec_read(net->pe.pInput, blob_offset, bytes, blob_size)) return js_null();
    for (i = 0; i < blob_size; ++i) { static const char hex[] = "0123456789ABCDEF"; signature[i * 2] = hex[bytes[i] >> 4]; signature[i * 2 + 1] = hex[bytes[i] & 15]; }
    signature[blob_size * 2] = 0;
    owner = net_method_owner(net, rid); type[0] = space[0] = 0;
    if (owner && xx_dotnet_inspect_table_row(net, 2, owner, &type_row, &row_size)) {
        if (!net_name(net, net_index(net, type_row + 4, net->cli.nStringIndexSize), type, sizeof(type), &type_cut) ||
            !net_name(net, net_index(net, type_row + 4 + net->cli.nStringIndexSize, net->cli.nStringIndexSize), space, sizeof(space), &space_cut)) return js_null();
    }
    has_body = rva && !(impl & 7) && !(flags & 0x2000) && xx_dotnet_inspect_method_body(net, rva, &body);
    object = js_new_object(ctx);
#define NET_NUM(key,value) js_set(ctx,object,key,js_num((double)(value)))
    NET_NUM("rid",rid); NET_NUM("token",UINT32_C(0x06000000) + rid); NET_NUM("rva",rva);
    NET_NUM("flags",flags); NET_NUM("implFlags",impl); NET_NUM("owner",owner);
    NET_NUM("codeOffset",has_body ? body.code_offset : 0); NET_NUM("codeSize",has_body ? body.code_size : 0);
    NET_NUM("localSignatureToken",has_body ? body.local_signature_token : 0);
#undef NET_NUM
    js_set(ctx,object,"name",js_str(ctx,name)); js_set(ctx,object,"typeName",js_str(ctx,type)); js_set(ctx,object,"namespace",js_str(ctx,space));
    js_set(ctx,object,"signature",js_str(ctx,signature)); js_set(ctx,object,"nameTruncated",js_bool(name_cut));
    js_set(ctx,object,"typeNameTruncated",js_bool(type_cut || space_cut)); js_set(ctx,object,"hasBody",js_bool(has_body));
    js_set(ctx,object,"hasExceptionHandlers",js_bool(has_body && body.has_exception_handlers));
    return object;
}
static JSVal net_user_string(DieEngine *engine, JSCtx *ctx, uint32_t token)
{
    xx_dotnet_inspection *net = &engine->dotnet; uint32_t index = token & 0xffffff, length, i, used = 0; unsigned prefix;
    int64_t offset, remaining; uint8_t first, *bytes; char *utf8; JSVal value;
    if (!engine->bHasDotNet || !net->cli.bValid || (token >> 24) != 0x70 || !index || index >= (uint64_t)net->cli.nUSSize) return js_null();
    if (!net->pe.pInput || xx_pd_is_stopped(net->pe.pInput->pd)) return js_null();
    net->pe.pInput->read_work = 0; net->pe.pInput->failed = false;
    offset = net->cli.nUSOffset + index; remaining = net->cli.nUSSize - index;
    if (!xx_exec_read(net->pe.pInput, offset, &first, 1)) return js_null();
    prefix = first < 0x80 ? 1 : first < 0xc0 ? 2 : first < 0xe0 ? 4 : 0;
    if (!prefix || remaining < prefix) return js_null();
    if (prefix == 1) length = first;
    else if (prefix == 2) length = (uint32_t)(first & 0x3f) * 0x100 + xx_exec_u8(net->pe.pInput, offset + 1);
    else length = (uint32_t)(first & 0x1f) * 0x1000000 + (uint32_t)xx_exec_u8(net->pe.pInput, offset + 1) * 0x10000 + (uint32_t)xx_exec_u8(net->pe.pInput, offset + 2) * 0x100 + xx_exec_u8(net->pe.pInput, offset + 3);
    if (!length || !(length & 1) || length > (uint64_t)(remaining - prefix) || length > 0x20001 ||
        (prefix == 2 && length < 0x80) || (prefix == 4 && length < 0x4000)) return js_null();
    bytes = (uint8_t *)xx_mem_alloc(length); utf8 = (char *)xx_mem_alloc(((size_t)length - 1) / 2 * 3 + 1);
    if (!bytes || !utf8 || !xx_exec_read(net->pe.pInput, offset + prefix, bytes, length) || bytes[length - 1] > 1) { xx_mem_free(bytes); xx_mem_free(utf8); return js_null(); }
    for (i = 0; i < length - 1; i += 2) {
        uint32_t cp = bytes[i] + (uint32_t)bytes[i + 1] * 0x100;
        if (cp >= 0xd800 && cp <= 0xdbff && i + 3 < length - 1) {
            uint32_t lo = bytes[i + 2] + (uint32_t)bytes[i + 3] * 0x100;
            if (lo >= 0xdc00 && lo <= 0xdfff) { cp = 0x10000 + (cp - 0xd800) * 0x400 + lo - 0xdc00; i += 2; }
        }
        if (cp < 0x80) utf8[used++] = (char)cp;
        else if (cp < 0x800) { utf8[used++] = (char)(0xc0 | cp >> 6); utf8[used++] = (char)(0x80 | (cp & 63)); }
        else if (cp < 0x10000) { utf8[used++] = (char)(0xe0 | cp >> 12); utf8[used++] = (char)(0x80 | (cp >> 6 & 63)); utf8[used++] = (char)(0x80 | (cp & 63)); }
        else { utf8[used++] = (char)(0xf0 | cp >> 18); utf8[used++] = (char)(0x80 | (cp >> 12 & 63)); utf8[used++] = (char)(0x80 | (cp >> 6 & 63)); utf8[used++] = (char)(0x80 | (cp & 63)); }
    }
    value = js_strn(ctx,utf8,used); xx_mem_free(bytes); xx_mem_free(utf8); return value;
}
static int net_called_by(DieEngine *engine, uint32_t callee, uint32_t caller)
{
    xx_dotnet_inspection *net = &engine->dotnet; xx_dotnet_method_body body; int64_t row; uint32_t size, rva, at = 0;
    uint8_t *bytes; int found = 0;
    if (!engine->bHasDotNet || !callee || callee > net->cli.pRows[6] || !xx_dotnet_inspect_table_row(net,6,caller,&row,&size)) return 0;
    if ((xx_exec_u16(net->pe.pInput,row+4,false)&7) || (xx_exec_u16(net->pe.pInput,row+6,false)&0x2000)) return 0;
    rva = xx_exec_u32(net->pe.pInput,row,false);
    if (!rva || !xx_dotnet_inspect_method_body(net,rva,&body) || !body.code_size) return 0;
    bytes = (uint8_t *)xx_mem_alloc(body.code_size); if (!bytes) return 0;
    if (!xx_exec_read(net->pe.pInput,body.code_offset,bytes,body.code_size)) { xx_mem_free(bytes); return 0; }
    while (at < body.code_size) {
        xx_dotnet_cil_instruction instruction;
        if (xx_dotnet_cil_decode(bytes,body.code_size,at,&instruction) != XX_DOTNET_CIL_OK) { found = 0; break; }
        if ((instruction.opcode == 0x28 || instruction.opcode == 0x6f) && instruction.value == UINT32_C(0x06000000) + callee) found = 1;
        at += instruction.width;
    }
    xx_mem_free(bytes); return found;
}

static const ApiEntry g_dotnet_api_table[] = {
    {"isNETStringPresent", A_isNetObjectPresent, 1},
    {"isNetObjectPresent", A_isNetObjectPresent, 1},
    {"isNetStringPresent", A_isNetObjectPresent, 1},
    {"isNETUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetUStringPresent", A_isNetUStringPresent, 1},
    {"isNetUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetGlobalCctorPresent", A_isNetGlobalCctorPresent, 0},
    {"isNetTypePresent", A_isNetTypePresent, 2},
    {"isNetMethodPresent", A_isNetMethodPresent, 3},
    {"getNumberOfNetMethods", A_getNumberOfNetMethods, 0},
    {"getNetMethodInfo", A_getNetMethodInfo, 1},
    {"getNetUserString", A_getNetUserString, 1},
    {"isNetMethodCalledBy", A_isNetMethodCalledBy, 2},
    {"isNetFieldPresent", A_isNetFieldPresent, 3},
    {"findSignatureInBlob_NET", A_findSignatureInBlob_NET, 1},
    {"isSignatureInBlobPresent_NET", A_isSignatureInBlobPresent_NET, 1},
    {"getNetModuleName", A_getNetModuleName, 0},
    {"getNetAssemblyName", A_getNetAssemblyName, 0},
    {"getNetVersion", A_getNETVersion, 0},
    {"getNETVersion", A_getNETVersion, 0},
    {"compareEP_NET", A_compareEP_NET, 2},
    {NULL, A_NONE, 0}
};

void die_engine_api_register_dotnet(JSCtx *pCtx, JSVal object)
{
    die_engine_api_register_entries(pCtx, object, g_dotnet_api_table);
}
bool die_engine_api_dispatch_dotnet(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_dotnet_inspection *pDotNet = &pEngine->dotnet;
    xx_memory_map *pMap = pEngine->pMap;

    switch (id) {
        case A_isNetObjectPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasDotNet) {
                for (i = 0; i < pDotNet->nNetAnsiCount; i++) {
                    if (x_strcmp(pDotNet->ppNetAnsiStrings[i], pString) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isNetUStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasDotNet) {
                for (i = 0; i < pDotNet->nNetUnicodeCount; i++) {
                    if (x_strcmp(pDotNet->ppNetUnicodeStrings[i], pString) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pString);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isNetGlobalCctorPresent:
            *pResult = js_bool(pEngine->bHasDotNet && xx_dotnet_inspect_net_global_cctor_present(pDotNet));
            return true;

        case A_isNetTypePresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = pEngine->bHasDotNet && xx_dotnet_inspect_net_type_present(pDotNet, pNamespace, pTypeName);

            cd_free(pNamespace);
            cd_free(pTypeName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getNumberOfNetMethods:
            *pResult = js_num(pEngine->bHasDotNet && pDotNet->cli.bValid ? pDotNet->cli.pRows[6] : 0);
            return true;
        case A_getNetMethodInfo: {
            uint32_t rid;
            *pResult = net_arg_rid(nArgc,pArgv,0,pDotNet->cli.pRows[6],&rid) ? net_method_info(pEngine,pCtx,rid) : js_null();
            return true;
        }
        case A_getNetUserString: {
            uint32_t token;
            *pResult = net_arg_rid(nArgc,pArgv,0,UINT32_MAX,&token) ? net_user_string(pEngine,pCtx,token) : js_null();
            return true;
        }
        case A_isNetMethodCalledBy: {
            uint32_t callee,caller;
            *pResult = js_bool(net_arg_rid(nArgc,pArgv,0,pDotNet->cli.pRows[6],&callee) && net_arg_rid(nArgc,pArgv,1,pDotNet->cli.pRows[6],&caller) && net_called_by(pEngine,callee,caller));
            return true;
        }

        case A_isNetMethodPresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            char *pMethodName = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = pEngine->bHasDotNet && xx_dotnet_inspect_net_method_present(pDotNet, pNamespace, pTypeName, pMethodName);

            cd_free(pNamespace);
            cd_free(pTypeName);
            cd_free(pMethodName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isNetFieldPresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            char *pFieldName = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = pEngine->bHasDotNet && xx_dotnet_inspect_net_field_present(pDotNet, pNamespace, pTypeName, pFieldName);

            cd_free(pNamespace);
            cd_free(pTypeName);
            cd_free(pFieldName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_findSignatureInBlob_NET:
        case A_isSignatureInBlobPresent_NET: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nResult = -1;

            if (pEngine->bHasDotNet && pDotNet->cli.bValid && (pDotNet->cli.nBlobSize > 0)) {
                nResult = die_engine_signature_find(pEngine, pDotNet->cli.nBlobOffset, pDotNet->cli.nBlobSize, pSignature);
            }

            cd_free(pSignature);

            if (id == A_isSignatureInBlobPresent_NET) {
                *pResult = js_bool(nResult != -1);
                return true;
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getNetModuleName:
            if (!pEngine->bHasDotNet) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = native_str_take(pCtx, xx_dotnet_inspect_net_module_name(pDotNet));
            return true;

        case A_getNetAssemblyName:
            if (!pEngine->bHasDotNet) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = native_str_take(pCtx, xx_dotnet_inspect_net_assembly_name(pDotNet));
            return true;

        case A_getNETVersion:
            *pResult = js_str(pCtx, (pEngine->bHasDotNet && pDotNet->pNetVersion) ? pDotNet->pNetVersion : "");
            return true;

        case A_compareEP_NET: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = 0;

            if (pEngine->bHasDotNet && pDotNet->cli.bValid && pDotNet->cli.nEntryPointRVA && nOffset >= -(cd_i64)pDotNet->cli.nEntryPointRVA &&
                nOffset <= INT64_MAX - (cd_i64)pDotNet->cli.nEntryPointRVA) {
                cd_i64 nTarget = xx_memory_map_relative_address_to_offset(pMap, (cd_i64)pDotNet->cli.nEntryPointRVA + nOffset);

                if (nTarget != -1) {
                    bResult = die_engine_signature_compare(pEngine, nTarget, pSignature, 3, -1);
                }
            }

            cd_free(pSignature);

            *pResult = js_bool(bResult);
            return true;
        }

        default:
            break;
    }

    return false;
}
