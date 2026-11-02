/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_die_engine_api_pe.c - PE and .NET script API dispatch and inspection helpers. */

#include "xx_die_engine_api_internal.h"
#include "xxfclib/formats/executable/pe/xx_pe_unwind_validate.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/global/xx_global.h"
#include "../regex/xx_regex_die.h"
#include "../formats/xx_executable_inspect_internal.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_reader.h"
#include "xxfclib/formats/executable/dotnet/xx_dotnet_cil.h"

static const char *section_name(DieEngine *pEngine, int nNumber, char *pBuffer)
{
    int i;
    size_t nLength = 0;
    pBuffer[0] = 0;
    if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pEngine->pe.nSectionCount)) {
        return pBuffer;
    }

    /* Check for COFF long section name: starts with '/' followed by decimal offset */
    if (pEngine->pe.pSections[nNumber].sName[0] == '/' &&
        pEngine->pe.pSections[nNumber].sName[1] >= '0' &&
        pEngine->pe.pSections[nNumber].sName[1] <= '9') {
        int64_t nOffset = (int64_t)pEngine->pe.nPointerToSymbolTable + (int64_t)pEngine->pe.nNumberOfSymbols * 18;
        if (nOffset > 0) {
            uint32_t nStrTableSize = xx_exec_u32(pEngine->pe.pInput, nOffset, false);
            if (nStrTableSize >= 4) {
                int64_t nStrOffset = 0;
                for (i = 1; i < 8 && pEngine->pe.pSections[nNumber].sName[i] >= '0' && pEngine->pe.pSections[nNumber].sName[i] <= '9'; i++) {
                    nStrOffset = nStrOffset * 10 + (pEngine->pe.pSections[nNumber].sName[i] - '0');
                }
                if (nStrOffset >= 4 && nStrOffset < (int64_t)nStrTableSize) {
                    char temp[256];
                    size_t to_read = (size_t)(nStrTableSize - nStrOffset);
                    if (to_read > sizeof(temp) - 1) to_read = sizeof(temp) - 1;
                    if (xx_exec_read(pEngine->pe.pInput, nOffset + nStrOffset, temp, to_read)) {
                        temp[to_read] = '\0';
                        size_t str_len = xx_rt_strlen(temp);
                        if (str_len > 0) {
                            xx_rt_memcpy(pBuffer, temp, str_len + 1);
                            return pBuffer;
                        }
                    }
                }
            }
        }
    }

    /* xx_pe_inspection reads the eight-byte field as Latin-1, while JS strings and the
     * console output use UTF-8. A raw high byte is not a UTF-8 character. */
    for (i = 0; (i < 8) && pEngine->pe.pSections[nNumber].sName[i]; i++) {
        unsigned char nChar = (unsigned char)pEngine->pe.pSections[nNumber].sName[i];
        if (nChar >= 0x80) {
            pBuffer[nLength++] = (char)(0xC0 | (nChar >> 6));
            pBuffer[nLength++] = (char)(0x80 | (nChar & 0x3F));
        } else {
            pBuffer[nLength++] = (char)nChar;
        }
    }
    pBuffer[nLength] = 0;
    return pBuffer;
}

static int pe_directory_section(DieEngine *pEngine, int nDirectory)
{
    const xx_memory_record *pRecord;
    if (!pEngine->bHasPE) {
        return -1;
    }

    if (pEngine->pe.pDirRVA[nDirectory] == 0) {
        return -1;
    }

    pRecord = xx_memory_map_record_by_address(&pEngine->pe.map, pEngine->pe.nImageBase + pEngine->pe.pDirRVA[nDirectory]);
    return pRecord && pRecord->file_part_number > 0 ? pRecord->file_part_number - 1 : 0;
}

/* PE image types, mirroring xx_pe_inspection::getType() / xx_pe_inspection::typeIdToString(). */
typedef enum {
    PETYPE_APPLICATION = 0,
    PETYPE_XBOX_APPLICATION,
    PETYPE_EFI_APPLICATION,
    PETYPE_GUI,
    PETYPE_CE_GUI,
    PETYPE_CONSOLE,
    PETYPE_DLL,
    PETYPE_DRIVER,
    PETYPE_NATIVE,
    PETYPE_EFI_RUNTIMEDRIVER,
    PETYPE_EFI_BOOTSERVICEDRIVER
} PEType;

static int pe_imports_ntdll(xx_pe_inspection *pPE)
{
    int i = 0;

    for (i = 0; i < pPE->nImportCount; i++) {
        if (cd_stricmp_ascii(pPE->pImports[i].pName, "ntdll.dll") == 0) {
            return 1;
        }
    }

    return 0;
}

static PEType pe_type(xx_pe_inspection *pPE)
{
    PEType result = PETYPE_APPLICATION;

    switch (pPE->nSubsystem) {
        case 1:
        case 8: result = pe_imports_ntdll(pPE) ? PETYPE_NATIVE : PETYPE_DRIVER; break;
        case 3:
        case 5:
        case 7: result = PETYPE_CONSOLE; break;
        case 2: result = PETYPE_GUI; break;
        case 9: result = PETYPE_CE_GUI; break;
        case 14: result = PETYPE_XBOX_APPLICATION; break;
        case 10: result = PETYPE_EFI_APPLICATION; break;
        case 11: result = PETYPE_EFI_BOOTSERVICEDRIVER; break;
        case 12: result = PETYPE_EFI_RUNTIMEDRIVER; break;
        default: break;
    }

    if ((result != PETYPE_DRIVER) && (pPE->nCharacteristics & 0x2000)) {
        result = PETYPE_DLL;
    }

    return result;
}

static const char *pe_type_name(PEType type)
{
    switch (type) {
        case PETYPE_APPLICATION: return "Application";
        case PETYPE_XBOX_APPLICATION: return "XBOX Application";
        case PETYPE_EFI_APPLICATION: return "EFI Application";
        case PETYPE_GUI: return "GUI";
        case PETYPE_CE_GUI: return "CE GUI";
        case PETYPE_CONSOLE: return "Console";
        case PETYPE_DLL: return "DLL";
        case PETYPE_DRIVER: return "Driver";
        case PETYPE_NATIVE: return "Native";
        case PETYPE_EFI_RUNTIMEDRIVER: return "EFI Runtime driver";
        case PETYPE_EFI_BOOTSERVICEDRIVER: return "EFI Boot service driver";
        default: break;
    }

    return "Unknown";
}

static const char *version_value(DieEngine *pEngine, const char *pKey)
{
    int i = 0;

    if (!pEngine->bHasPE) {
        return "";
    }

    for (i = 0; i < pEngine->pe.nVersionCount; i++) {
        if (x_strcmp(pEngine->pe.pVersionRecords[i].pKey, pKey) == 0) {
            return pEngine->pe.pVersionRecords[i].pValue;
        }
    }

    return "";
}

static cd_u64 image_file_header_value(xx_pe_inspection *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Machine") == 0) return pPE->nMachine;
    if (x_strcmp(pKey, "NumberOfSections") == 0) return pPE->nNumberOfSections;
    if (x_strcmp(pKey, "TimeDateStamp") == 0) return pPE->nTimeDateStamp;
    if (x_strcmp(pKey, "PointerToSymbolTable") == 0) return pPE->nPointerToSymbolTable;
    if (x_strcmp(pKey, "NumberOfSymbols") == 0) return pPE->nNumberOfSymbols;
    if (x_strcmp(pKey, "SizeOfOptionalHeader") == 0) return pPE->nSizeOfOptionalHeader;
    if (x_strcmp(pKey, "Characteristics") == 0) return pPE->nCharacteristics;

    return 0;
}

static cd_u64 image_optional_header_value(xx_pe_inspection *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Magic") == 0) return pPE->nMagic;
    if (x_strcmp(pKey, "MajorLinkerVersion") == 0) return pPE->nMajorLinkerVersion;
    if (x_strcmp(pKey, "MinorLinkerVersion") == 0) return pPE->nMinorLinkerVersion;
    if (x_strcmp(pKey, "SizeOfCode") == 0) return pPE->nSizeOfCode;
    if (x_strcmp(pKey, "SizeOfInitializedData") == 0) return pPE->nSizeOfInitializedData;
    if (x_strcmp(pKey, "SizeOfUninitializedData") == 0) return pPE->nSizeOfUninitializedData;
    if (x_strcmp(pKey, "AddressOfEntryPoint") == 0) return pPE->nAddressOfEntryPoint;
    if (x_strcmp(pKey, "BaseOfCode") == 0) return pPE->nBaseOfCode;
    if (x_strcmp(pKey, "BaseOfData") == 0) return pPE->nBaseOfData;
    if (x_strcmp(pKey, "ImageBase") == 0) return pPE->nImageBase;
    if (x_strcmp(pKey, "SectionAlignment") == 0) return pPE->nSectionAlignment;
    if (x_strcmp(pKey, "FileAlignment") == 0) return pPE->nFileAlignment;
    if (x_strcmp(pKey, "MajorOperatingSystemVersion") == 0) return pPE->nMajorOperatingSystemVersion;
    if (x_strcmp(pKey, "MinorOperatingSystemVersion") == 0) return pPE->nMinorOperatingSystemVersion;
    if (x_strcmp(pKey, "MajorImageVersion") == 0) return pPE->nMajorImageVersion;
    if (x_strcmp(pKey, "MinorImageVersion") == 0) return pPE->nMinorImageVersion;
    if (x_strcmp(pKey, "MajorSubsystemVersion") == 0) return pPE->nMajorSubsystemVersion;
    if (x_strcmp(pKey, "MinorSubsystemVersion") == 0) return pPE->nMinorSubsystemVersion;
    if (x_strcmp(pKey, "Win32VersionValue") == 0) return pPE->nWin32VersionValue;
    if (x_strcmp(pKey, "SizeOfImage") == 0) return pPE->nSizeOfImage;
    if (x_strcmp(pKey, "SizeOfHeaders") == 0) return pPE->nSizeOfHeaders;
    if (x_strcmp(pKey, "CheckSum") == 0) return pPE->nCheckSum;
    if (x_strcmp(pKey, "Subsystem") == 0) return pPE->nSubsystem;
    if (x_strcmp(pKey, "DllCharacteristics") == 0) return pPE->nDllCharacteristics;
    if (x_strcmp(pKey, "SizeOfStackReserve") == 0) return pPE->nSizeOfStackReserve;
    if (x_strcmp(pKey, "SizeOfStackCommit") == 0) return pPE->nSizeOfStackCommit;
    if (x_strcmp(pKey, "SizeOfHeapReserve") == 0) return pPE->nSizeOfHeapReserve;
    if (x_strcmp(pKey, "SizeOfHeapCommit") == 0) return pPE->nSizeOfHeapCommit;
    if (x_strcmp(pKey, "LoaderFlags") == 0) return pPE->nLoaderFlags;
    if (x_strcmp(pKey, "NumberOfRvaAndSizes") == 0) return pPE->nNumberOfRvaAndSizes;

    return 0;
}

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
    xx_dotnet_inspection *net = &engine->dotnet; xx_dotnet_method_body body;
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

bool die_engine_api_dispatch_pe(DieEngine *pEngine, JSCtx *pCtx, ApiId id, int nArgc, JSVal *pArgv, JSVal *pResult)
{
    xx_pe_inspection *pPE = &pEngine->pe;
    xx_dotnet_inspection *pDotNet = &pEngine->dotnet;
    xx_memory_map *pMap = pEngine->pMap;
    xx_io_device *pFile = pEngine->device;

    switch (id) {
        case A_getNumberOfSections:
            if (pEngine->bHasElf) {
                *pResult = js_num((double)pEngine->elf.nShnum);
                return true;
            }

            if (pEngine->bHasMach) {
                *pResult = js_num((double)pEngine->mach.nSectionCount);
                return true;
            }

            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSectionCount : 0));
            return true;

        case A_getSectionName: {
            char sName[256];
            *pResult = js_str(pCtx, section_name(pEngine, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0), sName));
            return true;
        }

        case A_getSectionVirtualSize:
        case A_getSectionVirtualAddress:
        case A_getSectionFileSize:
        case A_getSectionFileOffset:
        case A_getSectionCharacteristics: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if (pEngine->bHasElf) {
                switch (id) {
                    case A_getSectionFileSize: *pResult = js_num((double)xx_elf_inspect_section_size(&pEngine->elf, nNumber)); return true;
                    case A_getSectionFileOffset: *pResult = js_num((double)xx_elf_inspect_section_offset(&pEngine->elf, nNumber)); return true;
                    default: *pResult = js_num(0); return true;
                }
            }

            if (pEngine->bHasMach) {
                switch (id) {
                    case A_getSectionFileSize: *pResult = js_num((double)xx_macho_inspect_section_size(&pEngine->mach, nNumber)); return true;
                    case A_getSectionFileOffset: *pResult = js_num((double)xx_macho_inspect_section_offset(&pEngine->mach, nNumber)); return true;
                    default: *pResult = js_num(0); return true;
                }
            }

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nSectionCount)) {
                *pResult = js_num(0);
                return true;
            }

            switch (id) {
                case A_getSectionVirtualSize: *pResult = js_num((double)pPE->pSections[nNumber].nVirtualSize); return true;
                case A_getSectionVirtualAddress: *pResult = js_num((double)pPE->pSections[nNumber].nVirtualAddress); return true;
                case A_getSectionFileSize: *pResult = js_num((double)pPE->pSections[nNumber].nSizeOfRawData); return true;
                case A_getSectionFileOffset: *pResult = js_num((double)pPE->pSections[nNumber].nPointerToRawData); return true;
                default: *pResult = js_num((double)pPE->pSections[nNumber].nCharacteristics); return true;
            }
        }

        case A_getNumberOfResources:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nResourceCount : 0));
            return true;

        case A_isSectionNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasElf) {
                bResult = xx_elf_inspect_section_present(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                bResult = xx_macho_inspect_section_present(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    if (x_strcmp(section_name(pEngine, i, sName), pName) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getSectionNumber: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int nResult = -1;

            if (pEngine->bHasElf) {
                nResult = xx_elf_inspect_section_number(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                nResult = xx_macho_inspect_section_number(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    if (x_strcmp(section_name(pEngine, i, sName), pName) == 0) {
                        nResult = i;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_getSectionNameCollision: {
            char *pString1 = arg_string(pCtx, nArgc, pArgv, 0);
            char *pString2 = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            char sRoot1[256] = "";
            char sRoot2[256] = "";

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    char sName[256];
                    const char *pName = section_name(pEngine, i, sName);
                    const char *pMatch = x_strstr(pName, pString1);

                    /* XBinary::getStringCollision retains the prefix before
                     * the first delimiter, then stops at the first second
                     * delimiter after a nonempty first prefix was found. */
                    if (pMatch) {
                        size_t nLength = (size_t)(pMatch - pName);
                        if (nLength > sizeof(sRoot1) - 1) nLength = sizeof(sRoot1) - 1;

                        x_memcpy(sRoot1, pName, nLength);
                        sRoot1[nLength] = 0;
                    }

                    pMatch = x_strstr(pName, pString2);
                    if (sRoot1[0] && pMatch) {
                        size_t nLength = (size_t)(pMatch - pName);
                        if (nLength > sizeof(sRoot2) - 1) nLength = sizeof(sRoot2) - 1;

                        x_memcpy(sRoot2, pName, nLength);
                        sRoot2[nLength] = 0;
                        break;
                    }
                }
            }

            {
                JSVal res = js_str(pCtx, (sRoot1[0] && x_strcmp(sRoot1, sRoot2) == 0) ? sRoot1 : "");

                cd_free(pString1);
                cd_free(pString2);

                *pResult = res;
                return true;
            }
        }

        case A_isNET:
            *pResult = js_bool(pEngine->bHasDotNet && pDotNet->bIsNet);
            return true;
        case A_isPE32:
            *pResult = js_bool(pEngine->bHasPE && (!pPE->bIs64));
            return true;
        case A_isPEPlus:
            *pResult = js_bool(pEngine->bHasPE && pPE->bIs64);
            return true;

        case A_getGeneralOptions: {
            char sBuf[64];

            if (pEngine->bHasElf) {
                *pResult = js_str(pCtx, pEngine->elf.sGeneralOptions);
                return true;
            }

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%s%s", pe_type_name(pe_type(pPE)), pPE->bIs64 ? "64" : "32");

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_getResourceIdByNumber:
        case A_getResourceNameByNumber:
        case A_getResourceOffsetByNumber:
        case A_getResourceSizeByNumber:
        case A_getResourceTypeByNumber: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nResourceCount)) {
                if (id == A_getResourceNameByNumber) {
                    *pResult = js_str(pCtx, "");
                    return true;
                }

                if (id == A_getResourceOffsetByNumber) {
                    *pResult = js_num(-1);
                    return true;
                }

                *pResult = js_num(0);
                return true;
            }

            switch (id) {
                case A_getResourceIdByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nNameId); return true;
                case A_getResourceNameByNumber:
                    if (pPE->pResources[nNumber].pName) {
                        *pResult = js_str(pCtx, pPE->pResources[nNumber].pName);
                        return true;
                    }

                    *pResult = js_str(pCtx, pPE->pResources[nNumber].pTypeName ? pPE->pResources[nNumber].pTypeName : "");
                    return true;
                case A_getResourceOffsetByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nOffset); return true;
                case A_getResourceSizeByNumber: *pResult = js_num((double)pPE->pResources[nNumber].nSize); return true;
                default: *pResult = js_num((double)pPE->pResources[nNumber].nTypeId); return true;
            }
        }

        case A_getResourceNameOffset:
        case A_isResourceNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nResult = -1;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pName && (x_strcmp(pPE->pResources[i].pName, pName) == 0)) {
                        nResult = pPE->pResources[i].nOffset;
                        break;
                    }
                }
            }

            cd_free(pName);

            if (id == A_isResourceNamePresent) {
                *pResult = js_bool(nResult != -1);
                return true;
            }

            *pResult = js_num((double)nResult);
            return true;
        }

        case A_isResourceGroupNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = 0;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pTypeName && (x_strcmp(pPE->pResources[i].pTypeName, pName) == 0)) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isResourceGroupIdPresent: {
            cd_u32 nId = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if ((pPE->pResources[i].pTypeName == NULL) && (pPE->pResources[i].nTypeId == nId)) {
                        *pResult = js_bool(1);
                        return true;
                    }
                }
            }

            *pResult = js_bool(0);
            return true;
        }

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

        case A_getNumberOfImports:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportCount : 0));
            return true;

        case A_getImportLibraryName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, pPE->pImports[nNumber].pName);
            return true;
        }

        case A_getNumberOfImportThunks: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                *pResult = js_num(0);
                return true;
            }

            *pResult = js_num((double)pPE->pImports[nNumber].nFunctionCount);
            return true;
        }

        case A_getImportFunctionName: {
            int nImport = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int nFunction = (int)arg_i64(pCtx, nArgc, pArgv, 1, 0);

            if ((!pEngine->bHasPE) || (nImport < 0) || (nImport >= pPE->nImportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            if ((nFunction < 0) || (nFunction >= pPE->pImports[nImport].nFunctionCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, pPE->pImports[nImport].ppFunctions[nFunction]);
            return true;
        }

        case A_isLibraryPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bCheckCase = arg_bool(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasElf) {
                bResult = xx_elf_inspect_library_present(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                bResult = xx_macho_inspect_library_present(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    int bMatch = bCheckCase ? (x_strcmp(pPE->pImports[i].pName, pName) == 0) : (cd_stricmp_ascii(pPE->pImports[i].pName, pName) == 0);

                    if (bMatch) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isLibraryFunctionPresent: {
            char *pLibrary = arg_string(pCtx, nArgc, pArgv, 0);
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    if (cd_stricmp_ascii(pPE->pImports[i].pName, pLibrary) != 0) {
                        continue;
                    }

                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (cd_stricmp_ascii(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pLibrary);
            cd_free(pFunction);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_isFunctionPresent: {
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (x_strcmp(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pFunction);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getImportSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_IMPORT));
            return true;
        case A_getExportSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_EXPORT));
            return true;
        case A_getResourceSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_RESOURCE));
            return true;
        case A_getRelocsSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_BASERELOC));
            return true;
        case A_getTLSSection:
            *pResult = js_num((double)pe_directory_section(pEngine, XX_PE_INSPECT_DIR_TLS));
            return true;

        case A_getEntryPointSection: {
            const xx_memory_record *pRecord;
            if ((!pEngine->bHasPE) || (pPE->nAddressOfEntryPoint == 0)) {
                *pResult = js_num(-1);
                return true;
            }

            pRecord = xx_memory_map_record_by_address(&pPE->map, pPE->nEntryPointAddress);
            /* xx_pe_inspection preserves the header/default memory-record number zero;
             * only positive section record numbers are decremented. */
            *pResult = js_num((double)(pRecord && pRecord->file_part_number > 0 ? pRecord->file_part_number - 1 : 0));
            return true;
        }

        case A_getMajorLinkerVersion:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nMajorLinkerVersion : 0));
            return true;
        case A_getMinorLinkerVersion:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nMinorLinkerVersion : 0));
            return true;

        case A_getManifest:
            *pResult = js_str(pCtx, (pEngine->bHasPE && pPE->pManifest) ? pPE->pManifest : "");
            return true;

        case A_getVersionStringInfo: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_str(pCtx, version_value(pEngine, pKey));

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getCompilerVersion: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)pPE->nMajorLinkerVersion, (unsigned)pPE->nMinorLinkerVersion);

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_isConsole:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_CONSOLE));
            return true;

        case A_isSignedFile: {
            cd_i64 nOffset = 0;
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_bool(0);
                return true;
            }

            nOffset = (cd_i64)pPE->pDirRVA[XX_PE_INSPECT_DIR_SECURITY];
            nSize = (cd_i64)pPE->pDirSize[XX_PE_INSPECT_DIR_SECURITY];

            *pResult = js_bool((nSize > 0) && (nOffset > 0) && (nOffset < xx_io_total_size(pFile)) && (nSize <= xx_io_total_size(pFile) - nOffset));
            return true;
        }
        case A_isDll:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DLL));
            return true;
        case A_isDriver:
            *pResult = js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DRIVER));
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

        case A_getSizeOfCode:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSizeOfCode : 0));
            return true;
        case A_getSizeOfUninitializedData:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nSizeOfUninitializedData : 0));
            return true;

        case A_getPEFileVersion: {
            char *pFileName = arg_string(pCtx, nArgc, pArgv, 0);
            xx_io_device *source = xx_io_file_open(pFileName, "rb");
            xx_io_device *other = source ? xx_io_buffer_open_auto(source, 0, xx_get_file_memory_limit(), true) : NULL;
            JSVal res = js_str(pCtx, "");

            if (other) {
                xx_pe_inspection otherPE = {0};

                if (xx_pe_inspect_analyze_from_device(&otherPE, other, 0, NULL)) {
                    int i = 0;

                    for (i = 0; i < otherPE.nVersionCount; i++) {
                        if (x_strcmp(otherPE.pVersionRecords[i].pKey, "FileVersion") == 0) {
                            js_release(pCtx, res);
                            res = js_str(pCtx, otherPE.pVersionRecords[i].pValue);
                            break;
                        }
                    }
                }
                xx_pe_inspect_free(&otherPE);

                xx_io_close(other);
            } else if (source) {
                xx_io_close(source);
            }

            cd_free(pFileName);

            *pResult = res;
            return true;
        }

        case A_getFileVersion:
            *pResult = js_str(pCtx, version_value(pEngine, "FileVersion"));
            return true;

        case A_getFileVersionMS: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)(pPE->nFileVersionMS >> 16), (unsigned)(pPE->nFileVersionMS & 0xFFFF));

            *pResult = js_str(pCtx, sBuf);
            return true;
        }

        case A_calculateSizeOfHeaders: {
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_num(0);
                return true;
            }

            nSize = pPE->nLfanew + 4 + 20 + pPE->nSizeOfOptionalHeader + (cd_i64)pPE->nSectionCount * 40;

            if (pPE->nFileAlignment) {
                nSize = ((nSize + pPE->nFileAlignment - 1) / pPE->nFileAlignment) * pPE->nFileAlignment;
            }

            *pResult = js_num((double)nSize);
            return true;
        }

        case A_isExportFunctionPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE && pName && pName[0] && pPE->ppExportFunctions) {
                for (i = 0; i < pPE->nExportCount; i++) {
                    if (pPE->ppExportFunctions[i] && (x_strcmp(pPE->ppExportFunctions[i], pName) == 0)) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            *pResult = js_bool(bResult);
            return true;
        }

        case A_getNumberOfExportFunctions:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nExportCount : 0));
            return true;

        case A_getExportFunctionName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nExportCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, (pPE->ppExportFunctions && pPE->ppExportFunctions[nNumber]) ? pPE->ppExportFunctions[nNumber] : "");
            return true;
        }

        case A_getNextNamedExportFunctionIndex: {
            int64_t nPrevious = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            int i = 0;

            if ((!pEngine->bHasPE) || (!pPE->ppExportFunctions) || (nPrevious >= (int64_t)pPE->nExportCount - 1)) {
                *pResult = js_num(-1);
                return true;
            }

            if (nPrevious < -1) nPrevious = -1;

            for (i = (int)(nPrevious + 1); i < pPE->nExportCount; i++) {
                if (pPE->ppExportFunctions[i] && pPE->ppExportFunctions[i][0]) {
                    *pResult = js_num((double)i);
                    return true;
                }
            }

            *pResult = js_num(-1);
            return true;
        }

        case A_isExportPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_EXPORT] != 0));
            return true;
        case A_isTLSPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_TLS] != 0));
            return true;
        case A_isImportPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_IMPORT] != 0));
            return true;
        case A_isResourcesPresent:
            *pResult = js_bool(pEngine->bHasPE && (pPE->pDirRVA[XX_PE_INSPECT_DIR_RESOURCE] != 0));
            return true;

        case A_getImportHash32:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportHash32 : 0));
            return true;
        case A_getImportHash64:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nImportHash64 : 0));
            return true;

        case A_isImportPositionHashPresent: {
            cd_i64 nIndex = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            cd_u32 nHash = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;

            if (!pEngine->bHasPE) {
                *pResult = js_bool(0);
                return true;
            }

            if (nIndex == -1) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    if (pPE->pImports[i].nPositionHash == nHash) {
                        *pResult = js_bool(1);
                        return true;
                    }
                }

                *pResult = js_bool(0);
                return true;
            }

            if ((nIndex >= 0) && (nIndex < pPE->nImportCount)) {
                *pResult = js_bool(pPE->pImports[nIndex].nPositionHash == nHash);
                return true;
            }

            *pResult = js_bool(0);
            return true;
        }

        case A_getImageFileHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_num(pEngine->bHasPE ? (double)image_file_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getImageOptionalHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal res = js_num(pEngine->bHasPE ? (double)image_optional_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            *pResult = res;
            return true;
        }

        case A_getNumberOfDebugDataRecords:
            *pResult = js_num((double)(pEngine->bHasPE ? pPE->nDebugCount : 0));
            return true;

        case A_getDebugDataType: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                *pResult = js_str(pCtx, "");
                return true;
            }

            *pResult = js_str(pCtx, xx_pe_inspect_debug_type_name(pPE->pDebugRecords[nNumber].nType));
            return true;
        }

        case A_getDebugDataOffset:
        case A_getDebugDataSize: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                *pResult = js_num(-1);
                return true;
            }

            if (id == A_getDebugDataOffset) {
                *pResult = js_num((double)pPE->pDebugRecords[nNumber].nOffset);
                return true;
            }

            *pResult = js_num((double)pPE->pDebugRecords[nNumber].nSize);
            return true;
        }

        default:
            break;
    }

    return false;
}
