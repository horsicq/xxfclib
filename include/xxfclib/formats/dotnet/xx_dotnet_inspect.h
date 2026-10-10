/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_DOTNET_INSPECT_H
#define XXFCLIB_DOTNET_INSPECT_H
#include "xxfclib/formats/dotnet/xx_dotnet.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Metadata table indices (ECMA-335 II.22). */
#define XX_DOTNET_INSPECT_DIR_COMHEADER 14
#define XX_DOTNET_MDT_Module 0x00
#define XX_DOTNET_MDT_TypeRef 0x01
#define XX_DOTNET_MDT_TypeDef 0x02
#define XX_DOTNET_MDT_FieldPtr 0x03
#define XX_DOTNET_MDT_Field 0x04
#define XX_DOTNET_MDT_MethodPtr 0x05
#define XX_DOTNET_MDT_MethodDef 0x06
#define XX_DOTNET_MDT_ParamPtr 0x07
#define XX_DOTNET_MDT_Param 0x08
#define XX_DOTNET_MDT_InterfaceImpl 0x09
#define XX_DOTNET_MDT_MemberRef 0x0A
#define XX_DOTNET_MDT_Constant 0x0B
#define XX_DOTNET_MDT_CustomAttribute 0x0C
#define XX_DOTNET_MDT_FieldMarshal 0x0D
#define XX_DOTNET_MDT_DeclSecurity 0x0E
#define XX_DOTNET_MDT_ClassLayout 0x0F
#define XX_DOTNET_MDT_FieldLayout 0x10
#define XX_DOTNET_MDT_StandAloneSig 0x11
#define XX_DOTNET_MDT_EventMap 0x12
#define XX_DOTNET_MDT_EventPtr 0x13
#define XX_DOTNET_MDT_Event 0x14
#define XX_DOTNET_MDT_PropertyMap 0x15
#define XX_DOTNET_MDT_PropertyPtr 0x16
#define XX_DOTNET_MDT_Property 0x17
#define XX_DOTNET_MDT_MethodSemantics 0x18
#define XX_DOTNET_MDT_MethodImpl 0x19
#define XX_DOTNET_MDT_ModuleRef 0x1A
#define XX_DOTNET_MDT_TypeSpec 0x1B
#define XX_DOTNET_MDT_ImplMap 0x1C
#define XX_DOTNET_MDT_FieldRVA 0x1D
#define XX_DOTNET_MDT_ENCLog 0x1E
#define XX_DOTNET_MDT_ENCMap 0x1F
#define XX_DOTNET_MDT_Assembly 0x20
#define XX_DOTNET_MDT_AssemblyProcessor 0x21
#define XX_DOTNET_MDT_AssemblyOS 0x22
#define XX_DOTNET_MDT_AssemblyRef 0x23
#define XX_DOTNET_MDT_AssemblyRefProcessor 0x24
#define XX_DOTNET_MDT_AssemblyRefOS 0x25
#define XX_DOTNET_MDT_File 0x26
#define XX_DOTNET_MDT_ExportedType 0x27
#define XX_DOTNET_MDT_ManifestResource 0x28
#define XX_DOTNET_MDT_NestedClass 0x29
#define XX_DOTNET_MDT_GenericParam 0x2A
#define XX_DOTNET_MDT_MethodSpec 0x2B
#define XX_DOTNET_MDT_GenericParamConstraint 0x2C

typedef struct {
    int bValid;

    int64_t nCliOffset;
    int64_t nMetaSize;
    uint32_t nFlags;
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
    uint32_t nEntryPointToken; /* zero when COR header uses a native RVA */

    uint32_t pRows[64];       /* row counts                        */
    int pElementSize[64];     /* bytes per row                     */
    int64_t pTableOffset[64]; /* table offsets relative to format base */
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
    int nImplementationSize;
    int nTypeOrMethodDefSize;
    int nFieldListSize;
    int nMethodListSize;
} xx_dotnet_inspect_cli;

typedef struct xx_dotnet_inspection {
    xx_pe_inspection pe; /* PE state and borrowed-device view; first member. */
    int bIsNet;
    char *pNetVersion;
    char **ppNetAnsiStrings;
    int nNetAnsiCount;
    char **ppNetUnicodeStrings;
    int nNetUnicodeCount;
    xx_dotnet_inspect_cli cli;
} xx_dotnet_inspection;

/* Zero-initialize states. Free before reuse. The parent device must outlive
 * the inspection. Offsets are relative to the supplied format base. */
XXFC_API int xx_dotnet_inspect_analyze_from_device(xx_dotnet_inspection *state, xx_io_device *device, int64_t base, xx_pd_struct *pd);
XXFC_API int xx_dotnet_inspect_parse(xx_dotnet_inspection *state, xx_dotnet *reader, xx_pd_struct *pd);
XXFC_API void xx_dotnet_inspect_free(xx_dotnet_inspection *state);
/* Empty namespace/type arguments skip that component. Returned strings are
 * independently allocated and must be released with xx_mem_free. */
XXFC_API int xx_dotnet_inspect_net_type_present(xx_dotnet_inspection *, const char *, const char *);
XXFC_API int xx_dotnet_inspect_net_method_present(xx_dotnet_inspection *, const char *, const char *, const char *);
XXFC_API int xx_dotnet_inspect_net_field_present(xx_dotnet_inspection *, const char *, const char *, const char *);
XXFC_API int xx_dotnet_inspect_net_global_cctor_present(xx_dotnet_inspection *);
XXFC_API char *xx_dotnet_inspect_net_module_name(xx_dotnet_inspection *);
XXFC_API char *xx_dotnet_inspect_net_assembly_name(xx_dotnet_inspection *);
#ifdef __cplusplus
}
#endif
#endif
