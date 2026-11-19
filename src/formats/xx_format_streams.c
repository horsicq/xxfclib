/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Optional stream providers are independent of Abstractformat. This registry
 * performs no I/O and borrows immutable, process-wide provider tables.
 */
#include "xxfclib/formats/xx_format.h"

/* Non-CMake builds retain the complete library's native providers by default.
 * Focused CMake profiles set these private flags from their reader closure. */
#ifndef XXFC_STREAM_HAS_7ZIP
#define XXFC_STREAM_HAS_7ZIP 1
#endif
#ifndef XXFC_STREAM_HAS_AR
#define XXFC_STREAM_HAS_AR 1
#endif
#ifndef XXFC_STREAM_HAS_BZ2
#define XXFC_STREAM_HAS_BZ2 1
#endif
#ifndef XXFC_STREAM_HAS_GZ
#define XXFC_STREAM_HAS_GZ 1
#endif
#ifndef XXFC_STREAM_HAS_RAR
#define XXFC_STREAM_HAS_RAR 1
#endif
#ifndef XXFC_STREAM_HAS_TAR
#define XXFC_STREAM_HAS_TAR 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_BZ2
#define XXFC_STREAM_HAS_TAR_BZ2 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_GZ
#define XXFC_STREAM_HAS_TAR_GZ 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_LZ4
#define XXFC_STREAM_HAS_TAR_LZ4 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_XZ
#define XXFC_STREAM_HAS_TAR_XZ 1
#endif
#ifndef XXFC_STREAM_HAS_TAR_ZSTD
#define XXFC_STREAM_HAS_TAR_ZSTD 1
#endif
#ifndef XXFC_STREAM_HAS_XORARCHIVE
#define XXFC_STREAM_HAS_XORARCHIVE 1
#endif
#ifndef XXFC_STREAM_HAS_XZ
#define XXFC_STREAM_HAS_XZ 1
#endif
#ifndef XXFC_STREAM_HAS_ZIP
#define XXFC_STREAM_HAS_ZIP 1
#endif
#ifndef XXFC_STREAM_HAS_ELF
#define XXFC_STREAM_HAS_ELF 1
#endif
#ifndef XXFC_STREAM_HAS_LE
#define XXFC_STREAM_HAS_LE 1
#endif
#ifndef XXFC_STREAM_HAS_LX
#define XXFC_STREAM_HAS_LX 1
#endif
#ifndef XXFC_STREAM_HAS_MACHO
#define XXFC_STREAM_HAS_MACHO 1
#endif
#ifndef XXFC_STREAM_HAS_NE
#define XXFC_STREAM_HAS_NE 1
#endif
#ifndef XXFC_STREAM_HAS_OMF
#define XXFC_STREAM_HAS_OMF 1
#endif
#ifndef XXFC_STREAM_HAS_PE
#define XXFC_STREAM_HAS_PE 1
#endif
#ifndef XXFC_STREAM_HAS_PEF
#define XXFC_STREAM_HAS_PEF 1
#endif
#ifndef XXFC_STREAM_HAS_XTAC
#define XXFC_STREAM_HAS_XTAC 1
#endif
#ifndef XXFC_STREAM_HAS_VXWORKS
#define XXFC_STREAM_HAS_VXWORKS 1
#endif
#ifndef XXFC_STREAM_HAS_XXDS
#define XXFC_STREAM_HAS_XXDS 0
#endif
#if XXFC_ENABLE_STREAM_DATASTRUCTS && XXFC_STREAM_HAS_XXDS
#include "xxfclib/datastructs/xx_xxds.h"
#endif

#if XXFC_STREAM_HAS_PE && (XXFC_ENABLE_STREAM_IMPORT || XXFC_ENABLE_STREAM_EXPORT || XXFC_ENABLE_STREAM_RESOURCES || XXFC_ENABLE_STREAM_METADATA || XXFC_ENABLE_STREAM_SYMBOLS)
extern bool xx_pe_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif
#if XXFC_STREAM_HAS_ELF && (XXFC_ENABLE_STREAM_RESOURCES || XXFC_ENABLE_STREAM_SYMBOLS)
extern bool xx_elf_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
#endif


#if XXFC_ENABLE_STREAM_IMPORT
#if XXFC_STREAM_HAS_LE
extern const StreamImport *xx_le_get_stream_import(void);
#endif
#if XXFC_STREAM_HAS_LX
extern const StreamImport *xx_lx_get_stream_import(void);
#endif
#if XXFC_STREAM_HAS_NE
extern const StreamImport *xx_ne_get_stream_import(void);
#endif
#if XXFC_STREAM_HAS_PE
extern const StreamImport *xx_pe_get_stream_import(void);
#endif
#if XXFC_STREAM_HAS_PEF
extern const StreamImport *xx_pef_get_stream_import(void);
#endif
#endif

#if XXFC_ENABLE_STREAM_EXPORT
#if XXFC_STREAM_HAS_LE
extern const StreamExport *xx_le_get_stream_export(void);
#endif
#if XXFC_STREAM_HAS_LX
extern const StreamExport *xx_lx_get_stream_export(void);
#endif
#if XXFC_STREAM_HAS_NE
extern const StreamExport *xx_ne_get_stream_export(void);
#endif
#if XXFC_STREAM_HAS_PE
extern const StreamExport *xx_pe_get_stream_export(void);
#endif
#endif

#if XXFC_ENABLE_STREAM_RESOURCES
#if XXFC_STREAM_HAS_LE
extern const StreamResources *xx_le_get_stream_resources(void);
#endif
#if XXFC_STREAM_HAS_LX
extern const StreamResources *xx_lx_get_stream_resources(void);
#endif
#if XXFC_STREAM_HAS_NE
extern const StreamResources *xx_ne_get_stream_resources(void);
#endif
#if XXFC_STREAM_HAS_PE
extern const StreamResources *xx_pe_get_stream_resources(void);
#endif
#if XXFC_STREAM_HAS_ELF
extern const StreamResources *xx_elf_get_stream_resources(void);
#endif
#endif

#if XXFC_ENABLE_STREAM_METADATA
#if XXFC_STREAM_HAS_VXWORKS
extern const StreamMetadata *xx_vxworks_get_stream_metadata(void);
#endif
#if XXFC_STREAM_HAS_PE
extern const StreamMetadata *xx_pe_get_stream_metadata(void);
#endif
#endif

#if XXFC_ENABLE_STREAM_SYMBOLS
#if XXFC_STREAM_HAS_PE
extern const StreamSymbols *xx_pe_get_stream_symbols(void);
#endif
#if XXFC_STREAM_HAS_ELF
extern const StreamSymbols *xx_elf_get_stream_symbols(void);
#endif
#if XXFC_STREAM_HAS_OMF
extern const StreamSymbols *xx_omf_get_stream_symbols(void);
#endif
#if XXFC_STREAM_HAS_XTAC
extern const StreamSymbols *xx_xtac_get_stream_symbols(void);
#endif
#endif

const StreamDataStructs *xx_format_get_stream_datastructs(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_DATASTRUCTS
    if (!format) return NULL;
#if XXFC_STREAM_HAS_XXDS
    {
        /* A DataStructs script (src/datastructs/<fmt>.xxds) serves the format;
         * there is no per-format C provider. */
        const StreamDataStructs *scripted = xx_xxds_get_stream_datastructs(format);
        if (scripted) return scripted;
    }
#endif
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

const StreamImport *xx_format_get_stream_import(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_IMPORT
    if (!format) return NULL;
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return xx_pe_get_stream_import();
#endif
    switch (format->file_type) {
#if XXFC_STREAM_HAS_LE
    case XX_FILE_TYPE_LE:
        return xx_le_get_stream_import();
#endif
#if XXFC_STREAM_HAS_LX
    case XX_FILE_TYPE_LX:
        return xx_lx_get_stream_import();
#endif
#if XXFC_STREAM_HAS_NE
    case XX_FILE_TYPE_NE:
        return xx_ne_get_stream_import();
#endif
#if XXFC_STREAM_HAS_PE
    case XX_FILE_TYPE_PE32:
    case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_DOTNET:
        return xx_pe_get_stream_import();
#endif
#if XXFC_STREAM_HAS_PEF
    case XX_FILE_TYPE_PEF:
        return xx_pef_get_stream_import();
#endif
    default:
        break;
    }
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

const StreamExport *xx_format_get_stream_export(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_EXPORT
    if (!format) return NULL;
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return xx_pe_get_stream_export();
#endif
    switch (format->file_type) {
#if XXFC_STREAM_HAS_LE
    case XX_FILE_TYPE_LE:
        return xx_le_get_stream_export();
#endif
#if XXFC_STREAM_HAS_LX
    case XX_FILE_TYPE_LX:
        return xx_lx_get_stream_export();
#endif
#if XXFC_STREAM_HAS_NE
    case XX_FILE_TYPE_NE:
        return xx_ne_get_stream_export();
#endif
#if XXFC_STREAM_HAS_PE
    case XX_FILE_TYPE_PE32:
    case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_DOTNET:
        return xx_pe_get_stream_export();
#endif
    default:
        break;
    }
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

const StreamResources *xx_format_get_stream_resources(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_RESOURCES
    if (!format) return NULL;
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return xx_pe_get_stream_resources();
#endif
#if XXFC_STREAM_HAS_ELF
    if (format->handle_base_info == xx_elf_handle_base_info) return xx_elf_get_stream_resources();
#endif
    switch (format->file_type) {
#if XXFC_STREAM_HAS_LE
    case XX_FILE_TYPE_LE:
        return xx_le_get_stream_resources();
#endif
#if XXFC_STREAM_HAS_LX
    case XX_FILE_TYPE_LX:
        return xx_lx_get_stream_resources();
#endif
#if XXFC_STREAM_HAS_NE
    case XX_FILE_TYPE_NE:
        return xx_ne_get_stream_resources();
#endif
#if XXFC_STREAM_HAS_PE
    case XX_FILE_TYPE_PE32:
    case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_DOTNET:
        return xx_pe_get_stream_resources();
#endif
#if XXFC_STREAM_HAS_ELF
    case XX_FILE_TYPE_ELF32:
    case XX_FILE_TYPE_ELF64:
        return xx_elf_get_stream_resources();
#endif
    default:
        break;
    }
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

const StreamMetadata *xx_format_get_stream_metadata(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_METADATA
    if (!format) return NULL;
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return xx_pe_get_stream_metadata();
#endif
    switch (format->file_type) {
#if XXFC_STREAM_HAS_VXWORKS
    case XX_FILE_TYPE_VXWORKS:
        return xx_vxworks_get_stream_metadata();
#endif
#if XXFC_STREAM_HAS_PE
    case XX_FILE_TYPE_PE32:
    case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_DOTNET:
        return xx_pe_get_stream_metadata();
#endif
    default:
        break;
    }
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

const StreamSymbols *xx_format_get_stream_symbols(Abstractformat *format)
{
#if XXFC_ENABLE_STREAM_SYMBOLS
    if (!format) return NULL;
#if XXFC_STREAM_HAS_PE
    if (format->handle_base_info == xx_pe_handle_base_info) return xx_pe_get_stream_symbols();
#endif
#if XXFC_STREAM_HAS_ELF
    if (format->handle_base_info == xx_elf_handle_base_info) return xx_elf_get_stream_symbols();
#endif
    switch (format->file_type) {
#if XXFC_STREAM_HAS_PE
    case XX_FILE_TYPE_PE32:
    case XX_FILE_TYPE_PE64:
    case XX_FILE_TYPE_DOTNET:
        return xx_pe_get_stream_symbols();
#endif
#if XXFC_STREAM_HAS_ELF
    case XX_FILE_TYPE_ELF32:
    case XX_FILE_TYPE_ELF64:
        return xx_elf_get_stream_symbols();
#endif
#if XXFC_STREAM_HAS_OMF
    case XX_FILE_TYPE_OMF:
        return xx_omf_get_stream_symbols();
#endif
#if XXFC_STREAM_HAS_XTAC
    case XX_FILE_TYPE_XTAC:
        return xx_xtac_get_stream_symbols();
#endif
    default:
        break;
    }
    return NULL;
#else
    (void)format;
    return NULL;
#endif
}

bool xx_format_has_data_structs(Abstractformat *format)
{
    const StreamDataStructs *stream = xx_format_get_stream_datastructs(format);
    return stream && stream->create_data_structs_reading && stream->data_struct_move_to_next;
}
