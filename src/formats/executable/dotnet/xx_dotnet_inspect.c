/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/dotnet/xx_dotnet_inspect.h"
#include "xxfclib/global/xx_global.h"
#include "../../xx_executable_inspect_internal.h"
#include "platforms/xx_dotnet_us_simd.h"

#if (defined(_M_ARM64) && !defined(_M_ARM64EC)) || (defined(__aarch64__) && defined(__ARM_NEON))
#include <arm_neon.h>
#define XX_DOTNET_HAVE_NEON 1
#else
#define XX_DOTNET_HAVE_NEON 0
#endif

#define MD(t) XX_DOTNET_MDT_##t

/* The standard heaps use only a handful of streams. Bound the directory so
 * overlap checks cannot turn a crafted header into an
 * unbounded quadratic walk. Custom stream names remain supported. */
#define DOTNET_MAX_STREAMS 128
typedef struct {
    uint32_t offset, size;
} dotnet_stream_extent;

static int coded_width(const xx_dotnet_inspect_cli *cli, unsigned bits, const unsigned *tables, size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i)
        if (cli->pRows[tables[i]] >= (UINT32_C(1) << (16U - bits))) return 4;
    return 2;
}

static void parse_net_tables(xx_dotnet_inspection *state)
{
    static const unsigned resolution[] = {MD(Module), MD(ModuleRef), MD(AssemblyRef), MD(TypeRef)};
    static const unsigned type[] = {MD(TypeDef), MD(TypeRef), MD(TypeSpec)};
    static const unsigned member[] = {MD(TypeDef), MD(TypeRef), MD(ModuleRef), MD(MethodDef), MD(TypeSpec)};
    static const unsigned constant[] = {MD(Field), MD(Param), MD(Property)};
    static const unsigned attribute[] = {MD(MethodDef),        MD(Field),        MD(TypeRef),
                                         MD(TypeDef),          MD(Param),        MD(InterfaceImpl),
                                         MD(MemberRef),        MD(Module),       MD(DeclSecurity),
                                         MD(Property),         MD(Event),        MD(StandAloneSig),
                                         MD(ModuleRef),        MD(TypeSpec),     MD(Assembly),
                                         MD(AssemblyRef),      MD(File),         MD(ExportedType),
                                         MD(ManifestResource), MD(GenericParam), MD(GenericParamConstraint),
                                         MD(MethodSpec)};
    static const unsigned catype[] = {MD(MethodDef), MD(MemberRef)};
    static const unsigned marshal[] = {MD(Field), MD(Param)};
    static const unsigned security[] = {MD(TypeDef), MD(MethodDef), MD(Assembly)};
    static const unsigned semantics[] = {MD(Event), MD(Property)};
    static const unsigned forwarded[] = {MD(Field), MD(MethodDef)};
    static const unsigned implementation[] = {MD(File), MD(AssemblyRef), MD(ExportedType)};
    static const unsigned owner[] = {MD(TypeDef), MD(MethodDef)};
    static const unsigned debug_parent[] = {MD(MethodDef),        MD(Field),         MD(TypeRef),
                                            MD(TypeDef),          MD(Param),         MD(InterfaceImpl),
                                            MD(MemberRef),        MD(Module),        MD(DeclSecurity),
                                            MD(Property),         MD(Event),         MD(StandAloneSig),
                                            MD(ModuleRef),        MD(TypeSpec),      MD(Assembly),
                                            MD(AssemblyRef),      MD(File),          MD(ExportedType),
                                            MD(ManifestResource), MD(GenericParam),  MD(GenericParamConstraint),
                                            MD(MethodSpec),       MD(Document),      MD(LocalScope),
                                            MD(LocalVariable),    MD(LocalConstant), MD(ImportScope)};
    xx_dotnet_inspect_cli *cli = &state->cli;
    xx_executable_input *input = state->pe.pInput;
    int64_t pos = cli->nTablesOffset, end = pos + cli->nTablesSize;
    uint64_t valid;
    uint8_t heaps;
    unsigned i;
    int str, guid, blob, param, event, property;
    cli->bValid = 0;
    if (pos <= 0 || cli->nTablesSize < 24) return;
    heaps = xx_exec_u8(input, pos + 6);
    valid = xx_exec_u64(input, pos + 8, false);
    /* Reserved 0x2d..0x2f and 0x38..0x3f have no known row schema.
     * Reject their presence, including zero-row bits, rather than silently
     * treating subsequent table bytes as another table. */
    if (valid & UINT64_C(0xff00e00000000000)) return;
    pos += 24;
    for (i = 0; i < 64; ++i) {
        if (valid & (UINT64_C(1) << i)) {
            if (end - pos < 4) return;
            cli->pRows[i] = xx_exec_u32(input, pos, false);
            pos += 4;
            if (cli->pRows[i] > 0xFFFFFFU) return; /* CLI token RID width */
        }
        cli->pIndexSize[i] = cli->pRows[i] >= 0x10000U ? 4 : 2;
    }
    cli->nStringIndexSize = str = (heaps & 1) ? 4 : 2;
    cli->nGuidIndexSize = guid = (heaps & 2) ? 4 : 2;
    cli->nBlobIndexSize = blob = (heaps & 4) ? 4 : 2;
#define CW(field, bits, array) cli->field = coded_width(cli, bits, array, sizeof(array) / sizeof(array[0]))
    CW(nResolutionScopeSize, 2, resolution);
    CW(nTypeDefOrRefSize, 2, type);
    CW(nMemberRefParentSize, 3, member);
    CW(nHasConstantSize, 2, constant);
    CW(nHasCustomAttributeSize, 5, attribute);
    CW(nCustomAttributeTypeSize, 3, catype);
    CW(nHasFieldMarshalSize, 1, marshal);
    CW(nHasDeclSecuritySize, 2, security);
    CW(nHasSemanticsSize, 1, semantics);
    CW(nMethodDefOrRefSize, 1, catype);
    CW(nMemberForwardedSize, 1, forwarded);
    CW(nImplementationSize, 2, implementation);
    CW(nTypeOrMethodDefSize, 1, owner);
#undef CW
#define IX(t) cli->pIndexSize[MD(t)]
#define LIST(t, p) (IX(t) > IX(p) ? IX(t) : IX(p))
    cli->nFieldListSize = LIST(Field, FieldPtr);
    cli->nMethodListSize = LIST(MethodDef, MethodPtr);
    param = LIST(Param, ParamPtr);
    event = LIST(Event, EventPtr);
    property = LIST(Property, PropertyPtr);
#define ROW(t, value) cli->pElementSize[MD(t)] = (value)
    ROW(Module, 2 + str + 3 * guid);
    ROW(TypeRef, cli->nResolutionScopeSize + 2 * str);
    ROW(TypeDef, 4 + 2 * str + cli->nTypeDefOrRefSize + cli->nFieldListSize + cli->nMethodListSize);
    ROW(FieldPtr, IX(Field));
    ROW(Field, 2 + str + blob);
    ROW(MethodPtr, IX(MethodDef));
    ROW(MethodDef, 8 + str + blob + param);
    ROW(ParamPtr, IX(Param));
    ROW(Param, 4 + str);
    ROW(InterfaceImpl, IX(TypeDef) + cli->nTypeDefOrRefSize);
    ROW(MemberRef, cli->nMemberRefParentSize + str + blob);
    ROW(Constant, 2 + cli->nHasConstantSize + blob);
    ROW(CustomAttribute, cli->nHasCustomAttributeSize + cli->nCustomAttributeTypeSize + blob);
    ROW(FieldMarshal, cli->nHasFieldMarshalSize + blob);
    ROW(DeclSecurity, 2 + cli->nHasDeclSecuritySize + blob);
    ROW(ClassLayout, 6 + IX(TypeDef));
    ROW(FieldLayout, 4 + IX(Field));
    ROW(StandAloneSig, blob);
    ROW(EventMap, IX(TypeDef) + event);
    ROW(EventPtr, IX(Event));
    ROW(Event, 2 + str + cli->nTypeDefOrRefSize);
    ROW(PropertyMap, IX(TypeDef) + property);
    ROW(PropertyPtr, IX(Property));
    ROW(Property, 2 + str + blob);
    ROW(MethodSemantics, 2 + IX(MethodDef) + cli->nHasSemanticsSize);
    ROW(MethodImpl, IX(TypeDef) + 2 * cli->nMethodDefOrRefSize);
    ROW(ModuleRef, str);
    ROW(TypeSpec, blob);
    ROW(ImplMap, 2 + cli->nMemberForwardedSize + str + IX(ModuleRef));
    ROW(FieldRVA, 4 + IX(Field));
    ROW(ENCLog, 8);
    ROW(ENCMap, 4);
    ROW(Assembly, 16 + blob + 2 * str);
    ROW(AssemblyProcessor, 4);
    ROW(AssemblyOS, 12);
    ROW(AssemblyRef, 12 + 2 * blob + 2 * str);
    ROW(AssemblyRefProcessor, 4 + IX(AssemblyRef));
    ROW(AssemblyRefOS, 12 + IX(AssemblyRef));
    ROW(File, 4 + str + blob);
    ROW(ExportedType, 8 + 2 * str + cli->nImplementationSize);
    ROW(ManifestResource, 8 + str + cli->nImplementationSize);
    ROW(NestedClass, 2 * IX(TypeDef));
    ROW(GenericParam, 4 + cli->nTypeOrMethodDefSize + str);
    ROW(MethodSpec, cli->nMethodDefOrRefSize + blob);
    ROW(GenericParamConstraint, IX(GenericParam) + cli->nTypeDefOrRefSize);
    ROW(Document, blob + guid + blob + guid);
    ROW(MethodDebugInformation, IX(Document) + blob);
    ROW(LocalScope, IX(MethodDef) + IX(ImportScope) + IX(LocalVariable) + IX(LocalConstant) + 8);
    ROW(LocalVariable, 4 + str);
    ROW(LocalConstant, str + blob);
    ROW(ImportScope, IX(ImportScope) + blob);
    ROW(StateMachineMethod, 2 * IX(MethodDef));
    ROW(CustomDebugInformation, coded_width(cli, 5, debug_parent, sizeof(debug_parent) / sizeof(debug_parent[0])) + guid + blob);
#undef ROW
#undef LIST
#undef IX
    if (heaps & 0x40) {
        if (end - pos < 4) return;
        pos += 4;
    }
    for (i = 0; i < 64; ++i) {
        if (cli->pRows[i]) {
            int64_t size = (int64_t)cli->pRows[i] * cli->pElementSize[i];
            if (!cli->pElementSize[i] || pos > end || size > end - pos) {
                xx_rt_memset(cli->pTableOffset, 0, sizeof(cli->pTableOffset));
                return;
            }
            cli->pTableOffset[i] = pos;
            pos += size;
        }
    }
    if (!input->failed) cli->bValid = 1;
}

/* Heap reads must contain their terminator within both the heap and the
 * ordinary string budget. Invalid indices never inspect neighboring data. */
static char *heap_string(xx_dotnet_inspection *state, int64_t offset, int64_t available)
{
    char *value;
    int64_t maximum = available < 0x10000 ? available : 0x10000;
    if (maximum <= 0) return xx_exec_copy_string(state->pe.pInput, "");
    value = xx_exec_string(state->pe.pInput, offset, maximum);
    if (value && (int64_t)xx_rt_strlen(value) == maximum) {
        xx_mem_free(value);
        /* Exhausting any heap extent without NUL is malformed, including
         * short heaps. Never replace a truncated string by a valid empty one. */
        state->pe.pInput->failed = true;
        return xx_exec_copy_string(state->pe.pInput, "");
    }
    return value;
}

static int query_begin(xx_dotnet_inspection *state)
{
    if (!state || !state->pe.pInput || !state->cli.bValid) return 0;
    state->pe.pInput->read_work = 0;
    state->pe.pInput->failed = false;
    return 1;
}

/* Reads a heap/table index of 2 or 4 bytes. */
static uint32_t md_index(xx_dotnet_inspection *pPE, int64_t nOffset, int nSize)
{
    if (nSize == 4) {
        return xx_exec_u32(pPE->pe.pInput, nOffset, false);
    }

    return xx_exec_u16(pPE->pe.pInput, nOffset, false);
}

/* Reads a string from the #Strings heap (caller frees). */
static char *md_string(xx_dotnet_inspection *pPE, uint32_t nIndex)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;

    if ((pCli->nStringsOffset <= 0) || ((int64_t)nIndex >= pCli->nStringsSize)) {
        return xx_exec_copy_string(pPE->pe.pInput, "");
    }

    return heap_string(pPE, pCli->nStringsOffset + (int64_t)nIndex, pCli->nStringsSize - (int64_t)nIndex);
}

static int64_t md_row_offset(xx_dotnet_inspection *pPE, int nTable, uint32_t nRow)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;

    if (!pCli->bValid || (nRow >= pCli->pRows[nTable]) || (pCli->pTableOffset[nTable] == 0)) {
        return -1;
    }

    return pCli->pTableOffset[nTable] + (int64_t)pCli->pElementSize[nTable] * (int64_t)nRow;
}

typedef struct {
    uint32_t nTypeName;
    uint32_t nTypeNamespace;
    uint32_t nFieldList;
    uint32_t nMethodList;
} MDTypeDef;

static int md_typedef(xx_dotnet_inspection *pPE, uint32_t nRow, MDTypeDef *pOut)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = md_row_offset(pPE, XX_DOTNET_MDT_TypeDef, nRow);

    xx_rt_memset(pOut, 0, sizeof(*pOut));

    if (nOffset == -1) {
        return 0;
    }

    nOffset += 4; /* Flags */
    pOut->nTypeName = md_index(pPE, nOffset, pCli->nStringIndexSize);
    nOffset += pCli->nStringIndexSize;
    pOut->nTypeNamespace = md_index(pPE, nOffset, pCli->nStringIndexSize);
    nOffset += pCli->nStringIndexSize;
    nOffset += pCli->nTypeDefOrRefSize; /* Extends */
    pOut->nFieldList = md_index(pPE, nOffset, pCli->nFieldListSize);
    nOffset += pCli->nFieldListSize;
    pOut->nMethodList = md_index(pPE, nOffset, pCli->nMethodListSize);

    return 1;
}

static uint32_t md_methoddef_name(xx_dotnet_inspection *pPE, uint32_t nRow)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = md_row_offset(pPE, XX_DOTNET_MDT_MethodDef, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset + 4 + 2 + 2, pCli->nStringIndexSize);
}

static uint32_t md_methodptr(xx_dotnet_inspection *pPE, uint32_t nRow)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = md_row_offset(pPE, XX_DOTNET_MDT_MethodPtr, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset, pCli->pIndexSize[XX_DOTNET_MDT_MethodDef]);
}

static uint32_t md_field_name(xx_dotnet_inspection *pPE, uint32_t nRow)
{
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = md_row_offset(pPE, XX_DOTNET_MDT_Field, nRow);

    if (nOffset == -1) {
        return 0;
    }

    return md_index(pPE, nOffset + 2, pCli->nStringIndexSize);
}

static int64_t md_row_count(uint32_t rows)
{
    return (int64_t)rows;
}

/* Locates a TypeDef row by namespace and name; returns -1 when absent.
 * An empty argument means "do not compare this component", matching the
 * reference implementation.                                                */
static int64_t md_find_typedef(xx_dotnet_inspection *pPE, const char *pNamespace, const char *pTypeName, MDTypeDef *pOut)
{
    int64_t nCount = md_row_count(pPE->cli.pRows[XX_DOTNET_MDT_TypeDef]);
    int64_t i = 0;

    for (i = 0; i < nCount && !pPE->pe.pInput->failed && !xx_pd_is_stopped(pPE->pe.pInput->pd); i++) {
        MDTypeDef record;
        char *pName = NULL;
        char *pNs = NULL;
        int bMatch = 0;

        if (!md_typedef(pPE, (uint32_t)i, &record)) {
            break;
        }

        pName = (pTypeName[0]) ? md_string(pPE, record.nTypeName) : xx_exec_copy_string(pPE->pe.pInput, "");
        pNs = (pNamespace[0]) ? md_string(pPE, record.nTypeNamespace) : xx_exec_copy_string(pPE->pe.pInput, "");

        bMatch = pNs && pName && ((xx_rt_strcmp(pNamespace, pNs) == 0) && (xx_rt_strcmp(pTypeName, pName) == 0));

        xx_mem_free(pName);
        xx_mem_free(pNs);

        if (bMatch && !pPE->pe.pInput->failed) {
            *pOut = record;

            return i;
        }
    }

    return -1;
}

int xx_dotnet_inspect_net_type_present(xx_dotnet_inspection *pPE, const char *pNamespace, const char *pTypeName)
{
    if (!pPE || !pNamespace || !pTypeName) return 0;
    if (!query_begin(pPE)) return 0;
    MDTypeDef record;

    if (!pPE->cli.bValid) {
        return 0;
    }

    return (md_find_typedef(pPE, pNamespace, pTypeName, &record) != -1) ? 1 : 0;
}

int xx_dotnet_inspect_net_method_present(xx_dotnet_inspection *pPE, const char *pNamespace, const char *pTypeName, const char *pMethodName)
{
    if (!pPE || !pNamespace || !pTypeName || !pMethodName) return 0;
    if (!query_begin(pPE)) return 0;
    MDTypeDef record;
    int64_t nRow = 0;
    uint32_t nTypeCount = 0;
    int64_t nMethodCount = 0;
    int64_t j = 0;

    if (!pPE->cli.bValid) {
        return 0;
    }

    nTypeCount = pPE->cli.pRows[XX_DOTNET_MDT_TypeDef];

    nRow = md_find_typedef(pPE, pNamespace, pTypeName, &record);

    if (nRow == -1) {
        return 0;
    }

    if ((uint32_t)nRow < nTypeCount - 1) {
        MDTypeDef next;

        if (md_typedef(pPE, (uint32_t)nRow + 1, &next)) {
            nMethodCount = (int64_t)next.nMethodList - (int64_t)record.nMethodList;
        }
    } else {
        uint32_t count = pPE->cli.pRows[XX_DOTNET_MDT_MethodPtr] ? pPE->cli.pRows[XX_DOTNET_MDT_MethodPtr] : pPE->cli.pRows[XX_DOTNET_MDT_MethodDef];
        nMethodCount = (int64_t)count + 1 - (int64_t)record.nMethodList;
    }

    {
        uint32_t count = pPE->cli.pRows[XX_DOTNET_MDT_MethodPtr] ? pPE->cli.pRows[XX_DOTNET_MDT_MethodPtr] : pPE->cli.pRows[XX_DOTNET_MDT_MethodDef];
        if (!record.nMethodList || record.nMethodList > (uint64_t)count + 1 || nMethodCount < 0 || nMethodCount > (int64_t)count + 1 - record.nMethodList) return 0;
    }
    for (j = 0; j < nMethodCount && !pPE->pe.pInput->failed && !xx_pd_is_stopped(pPE->pe.pInput->pd); j++) {
        uint32_t nNameIndex = 0;
        char *pName = NULL;
        int bMatch = 0;

        if (record.nMethodList == 0) {
            break;
        }

        if (pPE->cli.pRows[XX_DOTNET_MDT_MethodPtr]) {
            uint32_t nMethod = md_methodptr(pPE, record.nMethodList + (uint32_t)j - 1);

            if ((nMethod == 0) || (nMethod > pPE->cli.pRows[XX_DOTNET_MDT_MethodDef])) {
                continue;
            }

            nNameIndex = md_methoddef_name(pPE, nMethod - 1);
        } else {
            nNameIndex = md_methoddef_name(pPE, record.nMethodList + (uint32_t)j - 1);
        }

        pName = md_string(pPE, nNameIndex);
        bMatch = pName && (xx_rt_strcmp(pMethodName, pName) == 0);
        xx_mem_free(pName);

        if (bMatch && !pPE->pe.pInput->failed) {
            return 1;
        }
    }

    return 0;
}

int xx_dotnet_inspect_net_field_present(xx_dotnet_inspection *pPE, const char *pNamespace, const char *pTypeName, const char *pFieldName)
{
    if (!pPE || !pNamespace || !pTypeName || !pFieldName) return 0;
    if (!query_begin(pPE)) return 0;
    MDTypeDef record;
    int64_t nRow = 0;
    uint32_t nTypeCount = 0;
    int64_t nFieldCount = 0;
    int64_t j = 0;

    if (!pPE->cli.bValid) {
        return 0;
    }

    nTypeCount = pPE->cli.pRows[XX_DOTNET_MDT_TypeDef];
    nRow = md_find_typedef(pPE, pNamespace, pTypeName, &record);

    if (nRow == -1) {
        return 0;
    }

    if ((uint32_t)nRow < nTypeCount - 1) {
        MDTypeDef next;

        if (md_typedef(pPE, (uint32_t)nRow + 1, &next)) {
            nFieldCount = (int64_t)next.nFieldList - (int64_t)record.nFieldList;
        }
    } else {
        uint32_t count = pPE->cli.pRows[XX_DOTNET_MDT_FieldPtr] ? pPE->cli.pRows[XX_DOTNET_MDT_FieldPtr] : pPE->cli.pRows[XX_DOTNET_MDT_Field];
        nFieldCount = (int64_t)count + 1 - (int64_t)record.nFieldList;
    }

    {
        uint32_t count = pPE->cli.pRows[XX_DOTNET_MDT_FieldPtr] ? pPE->cli.pRows[XX_DOTNET_MDT_FieldPtr] : pPE->cli.pRows[XX_DOTNET_MDT_Field];
        if (!record.nFieldList || record.nFieldList > (uint64_t)count + 1 || nFieldCount < 0 || nFieldCount > (int64_t)count + 1 - record.nFieldList) return 0;
    }
    for (j = 0; j < nFieldCount && !pPE->pe.pInput->failed && !xx_pd_is_stopped(pPE->pe.pInput->pd); j++) {
        char *pName = NULL;
        int bMatch = 0;

        if (record.nFieldList == 0) {
            break;
        }

        {
            uint32_t row = record.nFieldList + (uint32_t)j - 1;
            if (pPE->cli.pRows[XX_DOTNET_MDT_FieldPtr]) {
                int64_t off = md_row_offset(pPE, XX_DOTNET_MDT_FieldPtr, row);
                uint32_t field = off < 0 ? 0 : md_index(pPE, off, pPE->cli.pIndexSize[XX_DOTNET_MDT_Field]);
                if (!field || field > pPE->cli.pRows[XX_DOTNET_MDT_Field]) continue;
                row = field - 1;
            }
            pName = md_string(pPE, md_field_name(pPE, row));
        }
        bMatch = pName && (xx_rt_strcmp(pFieldName, pName) == 0);
        xx_mem_free(pName);

        if (bMatch && !pPE->pe.pInput->failed) {
            return 1;
        }
    }

    return 0;
}

int xx_dotnet_inspect_net_global_cctor_present(xx_dotnet_inspection *pPE)
{
    if (!pPE) return 0;
    return xx_dotnet_inspect_net_method_present(pPE, "", "<Module>", ".cctor");
}

char *xx_dotnet_inspect_net_module_name(xx_dotnet_inspection *pPE)
{
    if (!query_begin(pPE)) return xx_str_create("");
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = 0;

    if (!pCli->bValid) {
        return xx_exec_copy_string(pPE->pe.pInput, "");
    }

    nOffset = md_row_offset(pPE, XX_DOTNET_MDT_Module, 0);

    if (nOffset == -1) {
        return xx_exec_copy_string(pPE->pe.pInput, "");
    }

    return md_string(pPE, md_index(pPE, nOffset + 2, pCli->nStringIndexSize));
}

char *xx_dotnet_inspect_net_assembly_name(xx_dotnet_inspection *pPE)
{
    if (!query_begin(pPE)) return xx_str_create("");
    xx_dotnet_inspect_cli *pCli = &pPE->cli;
    int64_t nOffset = 0;

    if (!pCli->bValid) {
        return xx_exec_copy_string(pPE->pe.pInput, "");
    }

    nOffset = md_row_offset(pPE, XX_DOTNET_MDT_Assembly, 0);

    if (nOffset == -1) {
        return xx_exec_copy_string(pPE->pe.pInput, "");
    }

    /* HashAlgId(4) Major(2) Minor(2) Build(2) Revision(2) Flags(4) PublicKey */
    nOffset += 4 + 2 + 2 + 2 + 2 + 4 + pCli->nBlobIndexSize;

    return md_string(pPE, md_index(pPE, nOffset, pCli->nStringIndexSize));
}

static int64_t dotnet_rva_range(xx_dotnet_inspection *state, uint32_t rva, uint32_t size)
{
    uint64_t address;
    int64_t offset;
    if (!rva || !size || (uint64_t)rva + size > UINT64_C(0x100000000)) return -1;
    address = xx_memory_map_relative_address_to_address(&state->pe.map, rva);
    if (address == XX_INVALID_ADDRESS || !xx_memory_map_is_physical_address_range(&state->pe.map, address, size)) return -1;
    offset = xx_memory_map_relative_address_to_offset(&state->pe.map, rva);
    if (offset < 0 || offset > state->pe.pInput->size || size > (uint64_t)(state->pe.pInput->size - offset)) return -1;
    return offset;
}

#if XX_DOTNET_HAVE_NEON
/* Narrow consecutive ASCII UTF-16LE units in 16- and 8-unit vectors. A small
 * stack batch amortizes buffer calls without changing the Unicode fallback. */
static size_t append_ascii_neon(xx_buf_t *buffer, const uint8_t *bytes, size_t start, size_t limit)
{
    uint8_t ascii[64];
    size_t i = start;
    size_t count = 0;
    const uint8x16_t high_bit = vdupq_n_u8(UINT8_C(0x80));
    while (i + 16U <= limit && count + 16U <= sizeof(ascii)) {
        uint8x16x2_t values = vld2q_u8(bytes + i * 2U);
        uint8x16_t non_ascii = vorrq_u8(values.val[1], vandq_u8(values.val[0], high_bit));
        if (vmaxvq_u8(non_ascii) != 0) break;
        vst1q_u8(ascii + count, values.val[0]);
        i += 16U;
        count += 16U;
    }
    if (i + 8U <= limit && count + 8U <= sizeof(ascii)) {
        uint8x8x2_t values = vld2_u8(bytes + i * 2U);
        uint8x8_t non_ascii = vorr_u8(values.val[1], vand_u8(values.val[0], vdup_n_u8(UINT8_C(0x80))));
        if (vmaxv_u8(non_ascii) == 0) {
            vst1_u8(ascii + count, values.val[0]);
            i += 8U;
            count += 8U;
        }
    }
    if (count && !xx_buf_append(buffer, ascii, count)) return start;
    return i;
}
#endif

#if XX_DOTNET_HAVE_X86_SIMD
/* Batch ASCII UTF-16LE narrowing while keeping AVX2 instructions in their own
 * runtime-dispatched translation unit. SSE2 also handles an 8-unit tail when
 * its independent global switch remains enabled. */
static size_t append_ascii_x86(xx_buf_t *buffer, const uint8_t *bytes, size_t start, size_t limit, bool use_sse2, bool use_avx2)
{
    uint8_t ascii[64];
    size_t i = start;
    size_t count = 0;
    size_t converted;

    if (use_avx2 && limit - i >= 16U && sizeof(ascii) - count >= 16U) {
        converted = xx_dotnet_us_ascii_avx2(bytes + i * 2U, limit - i, ascii, sizeof(ascii));
        i += converted;
        count += converted;
    }
    if (use_sse2 && limit - i >= 8U && sizeof(ascii) - count >= 8U) {
        converted = xx_dotnet_us_ascii_sse2(bytes + i * 2U, limit - i, ascii + count, sizeof(ascii) - count);
        i += converted;
        count += converted;
    }
    if (count && !xx_buf_append(buffer, ascii, count)) return start;
    return i;
}
#endif

/* #US payloads are exact-length little-endian UTF-16 followed by one flag
 * byte. Decode bytes already fetched by parse_heaps so a file-backed input
 * does not seek and read once for every 16-bit unit. */
static char *decode_user_string(xx_executable_input *input, const uint8_t *bytes, size_t units, bool use_neon, bool use_sse2, bool use_avx2)
{
    xx_buf_t buffer;
    size_t i = 0;
    xx_buf_init(&buffer);
    while (i < units && !input->failed) {
        if (!(i & 0xfffU) && xx_pd_is_stopped(input->pd)) {
            xx_buf_free(&buffer);
            return NULL;
        }
#if XX_DOTNET_HAVE_NEON
        if (use_neon && units - i >= 8U) {
            size_t until_cancel_check = 0x1000U - (i & 0xfffU);
            size_t limit = units - i < until_cancel_check ? units : i + until_cancel_check;
            size_t next = append_ascii_neon(&buffer, bytes, i, limit);
            if (!xx_buf_ok(&buffer)) {
                input->failed = true;
                break;
            }
            if (next != i) {
                i = next;
                continue;
            }
        }
#else
        (void)use_neon;
#endif
#if XX_DOTNET_HAVE_X86_SIMD
        if (use_avx2 || use_sse2) {
            size_t until_cancel_check = 0x1000U - (i & 0xfffU);
            size_t limit = units - i < until_cancel_check ? units : i + until_cancel_check;
            if ((use_avx2 && limit - i >= 16U) || (use_sse2 && limit - i >= 8U)) {
                size_t next = append_ascii_x86(&buffer, bytes, i, limit, use_sse2, use_avx2);
                if (!xx_buf_ok(&buffer)) {
                    input->failed = true;
                    break;
                }
                if (next != i) {
                    i = next;
                    continue;
                }
            }
        }
#else
        (void)use_sse2;
        (void)use_avx2;
#endif
        uint32_t code = (uint32_t)bytes[i * 2] | ((uint32_t)bytes[i * 2 + 1] << 8);
        ++i;
        if (code >= 0xd800 && code <= 0xdbff) {
            uint32_t low = i < units ? (uint32_t)bytes[i * 2] | ((uint32_t)bytes[i * 2 + 1] << 8) : 0;
            if (low >= 0xdc00 && low <= 0xdfff) {
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                ++i;
            } else {
                code = 0xfffd;
            }
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            code = 0xfffd;
        }
        if (!xx_exec_append_codepoint(&buffer, code)) input->failed = true;
    }
    if (!xx_buf_ok(&buffer) || !xx_exec_account_string(input, buffer.size)) input->failed = true;
    if (input->failed) {
        xx_buf_free(&buffer);
        return NULL;
    }
    {
        char *result = xx_buf_detach(&buffer, NULL);
        if (!result) input->failed = true;
        return result;
    }
}

static void parse_heaps(xx_dotnet_inspection *state)
{
    xx_executable_input *input = state->pe.pInput;
    xx_dotnet_inspect_cli *cli = &state->cli;
    xx_list_s strings;
    uint8_t *user_string_bytes = NULL;
    size_t user_string_capacity = 0;
    bool use_neon = false;
    bool use_sse2 = false;
    bool use_avx2 = false;
    int64_t pos = 0;
#if XX_DOTNET_HAVE_NEON
    use_neon = xx_is_neon_enabled();
#endif
#if XX_DOTNET_HAVE_X86_SIMD
    use_sse2 = xx_is_sse2_enabled();
    use_avx2 = xx_is_avx2_enabled();
#endif
    xx_list_init(&strings, sizeof(char *), NULL);
    while (pos < cli->nStringsSize && strings.count < 100000 && !input->failed && !xx_pd_is_stopped(input->pd)) {
        char *item = heap_string(state, cli->nStringsOffset + pos, cli->nStringsSize - pos);
        size_t size;
        if (!item) {
            input->failed = true;
            break;
        }
        size = xx_rt_strlen(item);
        if (size) {
            if (!xx_list_append(&strings, &item)) {
                xx_mem_free(item);
                input->failed = true;
                break;
            }
        } else xx_mem_free(item);
        pos += (int64_t)size + 1;
    }
    state->ppNetAnsiStrings = (char **)strings.data;
    state->nNetAnsiCount = (int)strings.count;
    if (strings.count >= 100000 && pos < cli->nStringsSize) input->failed = true;
    strings.data = NULL;
    strings.count = strings.capacity = 0;
    xx_list_cleanup(&strings);

    /* XCLIAssembly::getUnicodeStrings walks #US with a one-byte length (0x80
     * read as 0) and takes length / 2 UTF-16 units after it; it stops at a
     * length that leaves the heap instead of failing. Follow it, so that a
     * malformed or long (multi-byte length) entry gives the reference's
     * strings rather than rejecting the whole assembly. The reference may
     * read one byte past the heap, its QByteArray terminator, taken as 0. */
    xx_list_init(&strings, sizeof(char *), NULL);
    pos = 1;
    while (cli->nUSSize > 1 && pos < cli->nUSSize && strings.count < 100000 && !input->failed && !xx_pd_is_stopped(input->pd)) {
        uint32_t length = xx_exec_u8(input, cli->nUSOffset + pos);
        size_t payload_size;
        size_t available;
        char *item;
        if (length == 0x80U) length = 0U;
        if ((int64_t)length > cli->nUSSize - pos) break;
        ++pos;
        if (pos > cli->nUSSize) break;
        payload_size = (size_t)(length / 2U) * 2U;
        if (payload_size > user_string_capacity) {
            uint8_t *resized = (uint8_t *)xx_mem_realloc(user_string_bytes, payload_size);
            if (!resized) {
                input->failed = true;
                break;
            }
            user_string_bytes = resized;
            user_string_capacity = payload_size;
        }
        available = (int64_t)payload_size <= cli->nUSSize - pos ? payload_size : (size_t)(cli->nUSSize - pos);
        if (payload_size > available) xx_rt_memset(user_string_bytes + available, 0, payload_size - available);
        if ((available && !xx_exec_read(input, cli->nUSOffset + pos, user_string_bytes, available)) ||
            !(item = decode_user_string(input, user_string_bytes, payload_size / 2U, use_neon, use_sse2, use_avx2))) {
            input->failed = true;
            break;
        }
        if (!*item) {
            xx_mem_free(item);
        } else if (!xx_list_append(&strings, &item)) {
            xx_mem_free(item);
            input->failed = true;
            break;
        }
        pos += length;
    }
    xx_mem_free(user_string_bytes);
    state->ppNetUnicodeStrings = (char **)strings.data;
    state->nNetUnicodeCount = (int)strings.count;
    strings.data = NULL;
    strings.count = strings.capacity = 0;
    xx_list_cleanup(&strings);
}

static int parse_net(xx_dotnet_inspection *state)
{
    xx_executable_input *input = state->pe.pInput;
    xx_dotnet_inspect_cli *cli = &state->cli;
    uint32_t cor_size, meta_rva, meta_size, version_length, entry;
    uint16_t streams, i;
    dotnet_stream_extent stream_extents[DOTNET_MAX_STREAMS];
    int64_t cor, meta, end, pos, first_stream;
    int tables = 0, strings = 0, us = 0;
    cor = dotnet_rva_range(state, state->pe.pDirRVA[XX_DOTNET_INSPECT_DIR_COMHEADER], 72);
    if (cor < 0 || state->pe.pDirSize[XX_DOTNET_INSPECT_DIR_COMHEADER] < 72) return 0;
    cor_size = xx_exec_u32(input, cor, false);
    if (cor_size < 72 || cor_size > state->pe.pDirSize[XX_DOTNET_INSPECT_DIR_COMHEADER] ||
        dotnet_rva_range(state, state->pe.pDirRVA[XX_DOTNET_INSPECT_DIR_COMHEADER], cor_size) < 0)
        return 0;
    meta_rva = xx_exec_u32(input, cor + 8, false);
    meta_size = xx_exec_u32(input, cor + 12, false);
    meta = dotnet_rva_range(state, meta_rva, meta_size);
    if (meta < 0 || meta_size < 20 || xx_exec_u32(input, meta, false) != UINT32_C(0x424A5342)) return 0;
    end = meta + meta_size;
    version_length = xx_exec_u32(input, meta + 12, false);
    if (version_length > meta_size - 20 || version_length > 0x10000U) return 0;
    pos = meta + 16 + (((int64_t)version_length + 3) & ~INT64_C(3));
    if (end - pos < 4) return 0;
    state->pNetVersion = version_length ? heap_string(state, meta + 16, version_length) : xx_exec_copy_string(input, "");
    if (!state->pNetVersion) {
        input->failed = true;
        return 0;
    }
    if (version_length && !state->pNetVersion[0] && xx_exec_u8(input, meta + 16)) return 0;
    streams = xx_exec_u16(input, pos + 2, false);
    pos += 4;
    if (!streams || streams > DOTNET_MAX_STREAMS || streams > (uint64_t)(end - pos) / 12) return 0;
    first_stream = end;
    for (i = 0; i < streams && !input->failed && !xx_pd_is_stopped(input->pd); ++i) {
        uint32_t off, size;
        char name[33];
        size_t j;
        uint16_t previous;
        int64_t header_size, start;
        if (end - pos < 12) return 0;
        off = xx_exec_u32(input, pos, false);
        size = xx_exec_u32(input, pos + 4, false);
        for (j = 0; j < sizeof(name) && pos + 8 + (int64_t)j < end; ++j) {
            name[j] = (char)xx_exec_u8(input, pos + 8 + (int64_t)j);
            if (!name[j]) break;
        }
        if (j == sizeof(name) || pos + 8 + (int64_t)j >= end) return 0;
        header_size = 8 + (((int64_t)j + 4) & ~INT64_C(3));
        if (header_size > end - pos || off > meta_size || size > meta_size - off) return 0;
        start = meta + off;
        for (previous = 0; previous < i; ++previous) {
            const dotnet_stream_extent *extent = &stream_extents[previous];
            if (size && extent->size && (uint64_t)off < (uint64_t)extent->offset + extent->size && (uint64_t)extent->offset < (uint64_t)off + size) return 0;
        }
        stream_extents[i].offset = off;
        stream_extents[i].size = size;
        if (size && start < first_stream) first_stream = start;
        /* XCLIAssembly::getCliInfo accepts repeated stream names. It keeps
         * the first #Strings/#US heaps and the last tables/#Blob/#GUID heaps. */
        if (!xx_rt_strcmp(name, "#~") || !xx_rt_strcmp(name, "#-")) {
            tables = 1;
            cli->nTablesOffset = start;
            cli->nTablesSize = size;
        } else if (!xx_rt_strcmp(name, "#Strings") && !strings) {
            strings = 1;
            cli->nStringsOffset = start;
            cli->nStringsSize = size;
        } else if (!xx_rt_strcmp(name, "#US") && !us) {
            us = 1;
            cli->nUSOffset = start;
            cli->nUSSize = size;
        } else if (!xx_rt_strcmp(name, "#Blob")) {
            cli->nBlobOffset = start;
            cli->nBlobSize = size;
        } else if (!xx_rt_strcmp(name, "#GUID")) {
            cli->nGuidOffset = start;
            cli->nGuidSize = size;
        }
        pos += header_size;
    }
    if (input->failed || xx_pd_is_stopped(input->pd) || pos > first_stream || !tables || cli->nTablesSize < 24) return 0;
    cli->nCliOffset = cor;
    cli->nMetaOffset = meta;
    cli->nMetaSize = meta_size;
    cli->nFlags = xx_exec_u32(input, cor + 16, false);
    entry = xx_exec_u32(input, cor + 20, false);
    if (cli->nFlags & 0x10U) cli->nEntryPointRVA = entry;
    else cli->nEntryPointToken = entry;
    parse_net_tables(state);
    if (!cli->bValid) return 0;
    if (cli->bValid && (entry & 0xFF000000U) == 0x06000000U && !(cli->nFlags & 0x10U)) {
        uint32_t row = entry & 0xFFFFFFU;
        if (row && row <= cli->pRows[MD(MethodDef)]) {
            int64_t method = md_row_offset(state, MD(MethodDef), row - 1);
            if (method >= 0) cli->nEntryPointRVA = xx_exec_u32(input, method, false);
        }
    }
    state->bIsNet = 1;
    parse_heaps(state);
    return !input->failed && !xx_pd_is_stopped(input->pd);
}

void xx_dotnet_inspect_free(xx_dotnet_inspection *state)
{
    int i;
    if (!state) return;
    for (i = 0; i < state->nNetAnsiCount; ++i) xx_mem_free(state->ppNetAnsiStrings[i]);
    xx_mem_free(state->ppNetAnsiStrings);
    for (i = 0; i < state->nNetUnicodeCount; ++i) xx_mem_free(state->ppNetUnicodeStrings[i]);
    xx_mem_free(state->ppNetUnicodeStrings);
    xx_mem_free(state->pNetVersion);
    xx_pe_inspect_free(&state->pe);
    xx_rt_memset(state, 0, sizeof(*state));
}

int xx_dotnet_inspect_parse(xx_dotnet_inspection *state, xx_dotnet *reader, xx_pd_struct *pd)
{
    xx_executable_input *input;
    int64_t saved;
    if (!state) return 0;
    xx_rt_memset(state, 0, sizeof(*state));
    if (!reader || !reader->pe.format.device) return 0;
    saved = xx_io_tell(reader->pe.format.device);
    /* Use the base parser explicitly; derived validation calls this routine
     * after PE base information is available. */
    if (!reader->pe.format.base_info_handled && !xx_pe_handle_base_info(&reader->pe.format, pd)) goto failed;
    if (!xx_pe_inspect_parse(&state->pe, &reader->pe, pd)) goto failed;
    input = state->pe.pInput;
    input->pd = pd;
    input->parsing = true;
    if (!parse_net(state)) goto failed;
    state->pe.map.file_type = XX_FILE_TYPE_DOTNET;
    if ((state->cli.nFlags & 0x11U) == 1U) state->pe.map.arch = XX_ARCH_DOTNET;
    reader->pe.format.file_type = XX_FILE_TYPE_DOTNET;
    reader->pe.format.arch = state->pe.map.arch;
    xx_format_invalidate_memory_map(&reader->pe.format);
    if (!xx_format_get_memory_map(&reader->pe.format, XX_MEMORY_MAP_MODE_UNKNOWN, pd)) goto failed;
    if (saved >= 0 && xx_io_seek64(reader->pe.format.device, saved, XX_RT_SEEK_SET) != 0) goto failed;
    input->pd = NULL;
    input->parsing = false;
    input->read_work = 0;
    return 1;
failed:
    reader->pe.format.file_type = XX_FILE_TYPE_DOTNET;
    reader->pe.format.is_valid = false;
    reader->pe.format.base_info_handled = false;
    xx_format_invalidate_memory_map(&reader->pe.format);
    xx_dotnet_inspect_free(state);
    if (saved >= 0) xx_io_seek64(reader->pe.format.device, saved, XX_RT_SEEK_SET);
    return 0;
}

int xx_dotnet_inspect_analyze_from_device(xx_dotnet_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd)
{
    xx_dotnet reader;
    int result;
    xx_dotnet_init(&reader, device, base);
    result = xx_dotnet_inspect_parse(state, &reader, pd);
    xx_dotnet_destroy(&reader);
    return result;
}
