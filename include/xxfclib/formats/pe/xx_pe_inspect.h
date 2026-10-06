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

/* xpe.h - Portable Executable parser. */

#ifndef XXFCLIB_PE_INSPECT_H
#define XXFCLIB_PE_INSPECT_H

#include "xxfclib/formats/pe/xx_pe.h"
#include "xxfclib/formats/xx_executable_inspect.h"

#ifdef __cplusplus
extern "C" {
#endif


#define XX_PE_INSPECT_DIR_EXPORT 0
#define XX_PE_INSPECT_DIR_IMPORT 1
#define XX_PE_INSPECT_DIR_RESOURCE 2
#define XX_PE_INSPECT_DIR_EXCEPTION 3
#define XX_PE_INSPECT_DIR_SECURITY 4
#define XX_PE_INSPECT_DIR_BASERELOC 5
#define XX_PE_INSPECT_DIR_DEBUG 6
#define XX_PE_INSPECT_DIR_TLS 9
#define XX_PE_INSPECT_DIR_LOADCONFIG 10
#define XX_PE_INSPECT_DIR_IAT 12
#define XX_PE_INSPECT_DIR_DELAYIMPORT 13
#define XX_PE_INSPECT_DIR_COMHEADER 14

/* Import-walk budgets, mirroring xx_pe_inspection::getImports. */
#define XX_PE_INSPECT_MAX_POSITIONS_PER_LIBRARY 16384
#define XX_PE_INSPECT_MAX_IMPORT_POSITIONS 65536

typedef struct {
    char sName[16];
    uint32_t nVirtualSize;
    uint32_t nVirtualAddress;
    uint32_t nSizeOfRawData;
    uint32_t nPointerToRawData;
    uint32_t nCharacteristics;
    /* The raw extent after clamping to the file, which is what the memory
     * map and the script API's per-section search both work on. A section
     * starting past the end has nMappedSize 0. */
    int64_t nMappedOffset;
    int64_t nMappedSize;
} xx_pe_inspect_section;

typedef struct {
    char *pName;
    char **ppFunctions;
    int nFunctionCount;
    uint32_t nPositionHash;
} xx_pe_inspect_import;

typedef struct {
    uint32_t nTypeId;
    char *pTypeName;
    uint32_t nNameId;
    char *pName;
    uint32_t nLangId;
    int64_t nOffset;
    int64_t nSize;
} xx_pe_inspect_resource;

typedef struct {
    uint32_t nType;
    int64_t nOffset;
    int64_t nSize;
} xx_pe_inspect_debug_record;

typedef struct {
    uint16_t nId;
    uint16_t nVersion;
    uint32_t nCount;
} xx_pe_inspect_rich_record;

typedef struct {
    char *pKey;   /* e.g. "FileVersion" */
    char *pValue; /* e.g. "3.13.3.0"    */
} xx_pe_inspect_version_record;

/* ------------------------------------------------------- .NET metadata --- */

/* Metadata table indices (ECMA-335 II.22). */
#define XX_PE_MDT_Module        0x00
#define XX_PE_MDT_TypeRef       0x01
#define XX_PE_MDT_TypeDef       0x02
#define XX_PE_MDT_Field         0x04
#define XX_PE_MDT_MethodPtr     0x05
#define XX_PE_MDT_MethodDef     0x06
#define XX_PE_MDT_ParamPtr      0x07
#define XX_PE_MDT_Param         0x08
#define XX_PE_MDT_InterfaceImpl 0x09
#define XX_PE_MDT_MemberRef     0x0A
#define XX_PE_MDT_Constant      0x0B
#define XX_PE_MDT_CustomAttribute 0x0C
#define XX_PE_MDT_FieldMarshal  0x0D
#define XX_PE_MDT_DeclSecurity  0x0E
#define XX_PE_MDT_ClassLayout   0x0F
#define XX_PE_MDT_FieldLayout   0x10
#define XX_PE_MDT_StandAloneSig 0x11
#define XX_PE_MDT_EventMap      0x12
#define XX_PE_MDT_EventPtr      0x13
#define XX_PE_MDT_Event         0x14
#define XX_PE_MDT_PropertyMap   0x15
#define XX_PE_MDT_PropertyPtr   0x16
#define XX_PE_MDT_Property      0x17
#define XX_PE_MDT_MethodSemantics 0x18
#define XX_PE_MDT_MethodImpl    0x19
#define XX_PE_MDT_ModuleRef     0x1A
#define XX_PE_MDT_TypeSpec      0x1B
#define XX_PE_MDT_ImplMap       0x1C
#define XX_PE_MDT_FieldRVA      0x1D
#define XX_PE_MDT_ENCLog        0x1E
#define XX_PE_MDT_ENCMap        0x1F
#define XX_PE_MDT_Assembly      0x20
#define XX_PE_MDT_AssemblyProcessor 0x21
#define XX_PE_MDT_AssemblyOS    0x22
#define XX_PE_MDT_AssemblyRef   0x23
#define XX_PE_MDT_AssemblyRefProcessor 0x24
#define XX_PE_MDT_AssemblyRefOS 0x25
#define XX_PE_MDT_File          0x26
#define XX_PE_MDT_ExportedType  0x27
#define XX_PE_MDT_ManifestResource 0x28
#define XX_PE_MDT_NestedClass   0x29
#define XX_PE_MDT_GenericParam  0x2A
#define XX_PE_MDT_MethodSpec    0x2B
#define XX_PE_MDT_GenericParamConstraint 0x2C

typedef struct {
    int bValid;

    int64_t nMetaOffset;   /* metadata root                     */
    int64_t nTablesOffset; /* "#~" / "#-" stream                */
    int64_t nTablesSize;
    int64_t nStringsOffset;
    int64_t nStringsSize;
    int64_t nBlobOffset;
    int64_t nBlobSize;
    int64_t nGuidOffset;
    int64_t nGuidSize;
    int64_t nUSOffset;
    int64_t nUSSize;

    uint32_t nEntryPointRVA;

    uint32_t pRows[64];         /* row counts                        */
    int pElementSize[64];     /* bytes per row                     */
    int64_t pTableOffset[64];  /* absolute file offset of each table */
    int pIndexSize[64];       /* 2 or 4 for a simple table index   */

    int nStringIndexSize;
    int nGuidIndexSize;
    int nBlobIndexSize;
    int nResolutionScopeSize;
    int nTypeDefOrRefSize;
    int nMemberRefParentSize;
    int nHasConstantSize;
    int nHasCustomAttributeSize;
    int nCustomAttributeTypeSize;
    int nHasFieldMarshalSize;
    int nHasDeclSecuritySize;
    int nHasSemanticsSize;
    int nMethodDefOrRefSize;
    int nMemberForwardedSize;
} xx_pe_inspect_cli;

typedef struct {
    xx_executable_input *pInput;
    xx_memory_map map;

    int bValid;
    int bIs64;
    int64_t nLfanew;

    /* IMAGE_FILE_HEADER */
    uint16_t nMachine;
    uint16_t nNumberOfSections;
    uint32_t nTimeDateStamp;
    uint32_t nPointerToSymbolTable;
    uint32_t nNumberOfSymbols;
    uint16_t nSizeOfOptionalHeader;
    uint16_t nCharacteristics;

    /* IMAGE_OPTIONAL_HEADER */
    uint16_t nMagic;
    uint8_t nMajorLinkerVersion;
    uint8_t nMinorLinkerVersion;
    uint32_t nSizeOfCode;
    uint32_t nSizeOfInitializedData;
    uint32_t nSizeOfUninitializedData;
    uint32_t nAddressOfEntryPoint;
    uint32_t nBaseOfCode;
    uint32_t nBaseOfData;
    uint64_t nImageBase;
    uint32_t nSectionAlignment;
    uint32_t nFileAlignment;
    uint16_t nMajorOperatingSystemVersion;
    uint16_t nMinorOperatingSystemVersion;
    uint16_t nMajorImageVersion;
    uint16_t nMinorImageVersion;
    uint16_t nMajorSubsystemVersion;
    uint16_t nMinorSubsystemVersion;
    uint32_t nWin32VersionValue;
    uint32_t nSizeOfImage;
    uint32_t nSizeOfHeaders;
    uint32_t nCheckSum;
    uint16_t nSubsystem;
    uint16_t nDllCharacteristics;
    uint64_t nSizeOfStackReserve;
    uint64_t nSizeOfStackCommit;
    uint64_t nSizeOfHeapReserve;
    uint64_t nSizeOfHeapCommit;
    uint32_t nLoaderFlags;
    uint32_t nNumberOfRvaAndSizes;

    uint32_t pDirRVA[16];
    uint32_t pDirSize[16];

    xx_pe_inspect_section *pSections;
    int nSectionCount;

    xx_pe_inspect_import *pImports;
    int nImportCount;

    char **ppExportFunctions;
    int nExportCount;

    xx_pe_inspect_resource *pResources;
    int nResourceCount;

    xx_pe_inspect_debug_record *pDebugRecords;
    int nDebugCount;

    xx_pe_inspect_rich_record *pRichRecords;
    int nRichCount;
    int bRichPresent;

    xx_pe_inspect_version_record *pVersionRecords;
    int nVersionCount;
    uint32_t nFileVersionMS;
    uint32_t nFileVersionLS;

    char *pManifest;

    /* .NET */
    int bIsNet;
    char *pNetVersion;
    char **ppNetAnsiStrings;
    int nNetAnsiCount;
    char **ppNetUnicodeStrings;
    int nNetUnicodeCount;
    xx_pe_inspect_cli cli;

    int64_t nEntryPointOffset;
    uint64_t nEntryPointAddress;
    int64_t nOverlayOffset;
    int64_t nOverlaySize;

    uint32_t nImportHash32;
    uint64_t nImportHash64;
} xx_pe_inspection;

/* Independent native inspection for callers that do not need the full reader
 * vtable. Borrows device; state owns a read-only view. */
XXFC_API int xx_pe_inspect_analyze_from_device(xx_pe_inspection *state,
    xx_io_device *device, int64_t base, xx_pd_struct *pd);

XXFC_API int xx_pe_inspect_parse(xx_pe_inspection *pPE, xx_pe *reader, xx_pd_struct *pd);
XXFC_API void xx_pe_inspect_free(xx_pe_inspection *pPE);

XXFC_API const char *xx_pe_inspect_debug_type_name(uint32_t nType);
XXFC_API int xx_pe_inspect_section_number_by_rva(xx_pe_inspection *pPE, uint32_t nRVA);
XXFC_API int64_t xx_pe_inspect_rva_to_offset(xx_pe_inspection *pPE, uint32_t nRVA);

/* .NET metadata queries. All return neutral values when the file has no
 * usable CLI metadata.                                                     */
XXFC_API int xx_pe_inspect_net_type_present(xx_pe_inspection *pPE, const char *pNamespace, const char *pTypeName);
XXFC_API int xx_pe_inspect_net_method_present(xx_pe_inspection *pPE, const char *pNamespace, const char *pTypeName, const char *pMethodName);
XXFC_API int xx_pe_inspect_net_field_present(xx_pe_inspection *pPE, const char *pNamespace, const char *pTypeName, const char *pFieldName);
XXFC_API int xx_pe_inspect_net_global_cctor_present(xx_pe_inspection *pPE);
/* Both return a newly allocated string (possibly empty). */
XXFC_API char *xx_pe_inspect_net_module_name(xx_pe_inspection *pPE);
XXFC_API char *xx_pe_inspect_net_assembly_name(xx_pe_inspection *pPE);

#ifdef __cplusplus
}
#endif

#endif /* XX_PE_INSPECT_H */
