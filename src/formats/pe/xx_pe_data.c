/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_pe_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#include <limits.h>

#define PE_DATA_MAX_ITEMS UINT64_C(262144)
#define PE_DATA_MAX_TABLE_ENTRIES UINT64_C(1048576)
#define PE_DATA_MAX_LINKED_ENTRIES UINT64_C(65536)
#define PE_DATA_MAX_RESOURCE_DEPTH 32U
#define PE_DATA_MAX_RESOURCE_NODES 65536U
#define PE_DATA_MAX_STRING UINT64_C(4096)
#define PE_DATA_MAX_DISPLAY UINT64_C(128)

#define PE_DOS_HEADER_SIZE UINT64_C(64)
#define PE_FILE_HEADER_SIZE UINT64_C(20)
#define PE_SECTION_HEADER_SIZE UINT64_C(40)
#define PE_OPTIONAL32_SIZE UINT64_C(96)
#define PE_OPTIONAL64_SIZE UINT64_C(112)
#define PE_DIRECTORY_ENTRY_SIZE UINT64_C(8)
#define PE_SYMBOL_SIZE UINT64_C(18)
#define PE_RELOCATION_SIZE UINT64_C(10)
#define PE_LINE_NUMBER_SIZE UINT64_C(6)

static int pe_display_from_ascii(wchar_t *destination, size_t capacity,
                                 const char *source) {
    size_t index = 0U;
    if (!destination || capacity == 0U || !source) return -1;
    while (source[index] != '\0' && index + 1U < capacity) {
        destination[index] = (wchar_t)(unsigned char)source[index];
        ++index;
    }
    destination[index] = L'\0';
    return source[index] == '\0' ? (int)index : -1;
}

static void pe_display_i64(wchar_t *destination, size_t capacity,
                           int64_t value) {
    char ascii[64];
    int length = xx_rt_snprintf(ascii, sizeof(ascii), "%lld",
                                (long long)value);
    if (length < 0 || (size_t)length >= sizeof(ascii) ||
        pe_display_from_ascii(destination, capacity, ascii) < 0)
        destination[0] = L'\0';
}

static size_t pe_display_append_hex_byte(wchar_t *display, size_t capacity,
                                         size_t at, uint8_t value) {
    static const wchar_t digits[] = L"0123456789ABCDEF";
    size_t needed = at == 0U ? 2U : 3U;
    if (!display || at >= capacity || needed >= capacity - at) return at;
    if (at != 0U) display[at++] = L' ';
    display[at++] = digits[value >> 4U];
    display[at++] = digits[value & 0x0fU];
    display[at] = L'\0';
    return at;
}

#define PE_DIR_EXPORT 0U
#define PE_DIR_IMPORT 1U
#define PE_DIR_RESOURCE 2U
#define PE_DIR_EXCEPTION 3U
#define PE_DIR_SECURITY 4U
#define PE_DIR_BASERELOC 5U
#define PE_DIR_DEBUG 6U
#define PE_DIR_ARCHITECTURE 7U
#define PE_DIR_GLOBALPTR 8U
#define PE_DIR_TLS 9U
#define PE_DIR_LOAD_CONFIG 10U
#define PE_DIR_BOUND_IMPORT 11U
#define PE_DIR_IAT 12U
#define PE_DIR_DELAY_IMPORT 13U
#define PE_DIR_COM_DESCRIPTOR 14U
#define PE_DIR_RESERVED 15U

#define PE_MACHINE_I386 UINT16_C(0x014c)
#define PE_MACHINE_R3000 UINT16_C(0x0162)
#define PE_MACHINE_R4000 UINT16_C(0x0166)
#define PE_MACHINE_R10000 UINT16_C(0x0168)
#define PE_MACHINE_WCEMIPSV2 UINT16_C(0x0169)
#define PE_MACHINE_ALPHA UINT16_C(0x0184)
#define PE_MACHINE_ALPHA64 UINT16_C(0x0284)
#define PE_MACHINE_ARM UINT16_C(0x01c0)
#define PE_MACHINE_THUMB UINT16_C(0x01c2)
#define PE_MACHINE_ARMNT UINT16_C(0x01c4)
#define PE_MACHINE_IA64 UINT16_C(0x0200)
#define PE_MACHINE_AMD64 UINT16_C(0x8664)
#define PE_MACHINE_ARM64 UINT16_C(0xaa64)

#define PE_FIELD(name_, type_, offset_, size_, property_) \
    {L##name_, L##type_, offset_, size_, property_}
#define PE_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))

typedef struct pe_data_stream_s {
    xx_data_struct *items;
    size_t count;
    size_t capacity;
} pe_data_stream;

typedef struct pe_record_stream_s {
    xx_data_struct_field_desc *fields;
    size_t count;
    bool unicode_string;
} pe_record_stream;

typedef struct pe_resource_walk_s {
    uint32_t root_rva;
    uint32_t directory_size;
    uint32_t visited[PE_DATA_MAX_RESOURCE_NODES];
    size_t visited_count;
} pe_resource_walk;

static const char *const pe_data_names[] = {
    "UNKNOWN",
    "IMAGE_DOS_HEADER", "IMAGE_NT_SIGNATURE", "IMAGE_FILE_HEADER",
    "IMAGE_OPTIONAL_HEADER32", "IMAGE_DATA_DIRECTORY", "IMAGE_SECTION_HEADER",
    "IMAGE_OPTIONAL_HEADER64", "DOS_STUB", "IMAGE_SYMBOL",
    "IMAGE_AUX_SYMBOL", "IMAGE_AUX_SYMBOL_FUNCTION_DEFINITION",
    "IMAGE_AUX_SYMBOL_BF_EF", "IMAGE_AUX_SYMBOL_WEAK_EXTERNAL",
    "IMAGE_AUX_SYMBOL_FILE", "IMAGE_AUX_SYMBOL_SECTION_DEFINITION",
    "IMAGE_AUX_SYMBOL_CLR_TOKEN", "COFF_STRING_TABLE_HEADER", "COFF_STRING",
    "IMAGE_RELOCATION", "IMAGE_LINENUMBER", "IMAGE_EXPORT_DIRECTORY",
    "EXPORT_ADDRESS_TABLE", "EXPORT_NAME_POINTER_TABLE", "EXPORT_ORDINAL_TABLE",
    "EXPORT_FORWARDER", "IMAGE_IMPORT_DESCRIPTOR", "IMAGE_THUNK_DATA32",
    "IMAGE_THUNK_DATA64", "IMAGE_IMPORT_BY_NAME", "ASCII_STRING",
    "IMAGE_RESOURCE_DIRECTORY", "IMAGE_RESOURCE_DIRECTORY_ENTRY",
    "IMAGE_RESOURCE_DIR_STRING_U", "IMAGE_RESOURCE_DATA_ENTRY",
    "RESOURCE_PAYLOAD", "RUNTIME_FUNCTION_X64", "RUNTIME_FUNCTION_IA64",
    "RUNTIME_FUNCTION_ARM", "RUNTIME_FUNCTION_ARM64", "RUNTIME_FUNCTION_MIPS",
    "RUNTIME_FUNCTION_ALPHA32", "RUNTIME_FUNCTION_ALPHA64", "RUNTIME_FUNCTION_CE",
    "UNWIND_INFO", "UNWIND_CODE", "UNWIND_HANDLER", "UNWIND_CHAINED_FUNCTION",
    "ARM64_XDATA_HEADER", "ARM64_XDATA_EXTENDED", "ARM64_EPILOG_SCOPE",
    "WIN_CERTIFICATE", "IMAGE_BASE_RELOCATION", "BASE_RELOCATION_ENTRY",
    "IMAGE_DEBUG_DIRECTORY", "IMAGE_COFF_SYMBOLS_HEADER", "CODEVIEW_RSDS",
    "CODEVIEW_NB10", "IMAGE_DEBUG_MISC", "FPO_DATA", "OMAP_ENTRY",
    "VC_FEATURE", "POGO_HEADER", "POGO_ENTRY", "EMBEDDED_PORTABLE_PDB",
    "REPRO_HASH_HEADER", "DEBUG_GUID", "EX_DLL_CHARACTERISTICS", "DEBUG_RAW",
    "IMAGE_ARCHITECTURE_HEADER", "IMAGE_ARCHITECTURE_ENTRY",
    "IMAGE_TLS_DIRECTORY32", "IMAGE_TLS_DIRECTORY64", "TLS_CALLBACK32",
    "TLS_CALLBACK64", "TLS_TEMPLATE", "IMAGE_LOAD_CONFIG_DIRECTORY32",
    "IMAGE_LOAD_CONFIG_DIRECTORY64", "IMAGE_LOAD_CONFIG_CODE_INTEGRITY",
    "SE_HANDLER_ENTRY", "GUARD_FUNCTION_ENTRY", "GUARD_ADDRESS_TAKEN_IAT_ENTRY",
    "GUARD_LONG_JUMP_ENTRY", "GUARD_EH_CONTINUATION_ENTRY",
    "IMAGE_DYNAMIC_RELOCATION_TABLE", "IMAGE_DYNAMIC_RELOCATION32",
    "IMAGE_DYNAMIC_RELOCATION64", "IMAGE_DYNAMIC_RELOCATION32_V2",
    "IMAGE_DYNAMIC_RELOCATION64_V2", "DYNAMIC_RELOCATION_PROLOGUE",
    "DYNAMIC_RELOCATION_EPILOGUE", "DYNAMIC_RELOCATION_IMPORT_CONTROL",
    "DYNAMIC_RELOCATION_ARM64_IMPORT_CONTROL", "DYNAMIC_RELOCATION_INDIR",
    "DYNAMIC_RELOCATION_SWITCHTABLE", "FUNCTION_OVERRIDE_HEADER",
    "FUNCTION_OVERRIDE_ENTRY", "BDD_INFO", "BDD_NODE", "IMAGE_HOT_PATCH_INFO",
    "IMAGE_HOT_PATCH_BASE", "IMAGE_HOT_PATCH_MACHINE", "IMAGE_HOT_PATCH_HASHES",
    "IMAGE_ENCLAVE_CONFIG32", "IMAGE_ENCLAVE_CONFIG64", "IMAGE_ENCLAVE_IMPORT",
    "IMAGE_VOLATILE_METADATA", "IMAGE_BOUND_IMPORT_DESCRIPTOR",
    "IMAGE_BOUND_FORWARDER_REF", "IMAGE_DELAYLOAD_DESCRIPTOR", "IAT32", "IAT64",
    "IMAGE_COR20_HEADER", "STORAGESIGNATURE", "STORAGEHEADER", "STORAGESTREAM",
    "IMAGE_COR_VTABLEFIXUP", "CLR_RAW", "RESERVED_DIRECTORY_RAW",
    "ARM32_XDATA_HEADER", "ARM32_EPILOG_SCOPE",
    "DYNAMIC_RELOCATION_ARM64X", "ARM64X_FIXUP", "ARM64X_DELTA",
    "PDB_CHECKSUM", "R2R_PERFMAP", "CHPE_METADATA", "ARM64EC_METADATA",
    "CHPE_CODE_RANGE_ENTRY", "CHPE_CODE_RANGE_TO_ENTRY_POINT",
    "CHPE_REDIRECTION_ENTRY", "VOLATILE_ACCESS", "VOLATILE_INFO_RANGE",
    "ARM32_XDATA_EXTENDED", "IMAGE_FUNCTION_ENTRY",
    "IMAGE_FUNCTION_ENTRY64", "IMAGE_POLICY_METADATA",
    "IMAGE_POLICY_ENTRY32", "IMAGE_POLICY_ENTRY64"
};

_Static_assert(PE_COUNT(pe_data_names) ==
                   (size_t)XX_PE_DATA_STRUCT_LAST + 1U,
               "PE data-structure name table is out of sync");

static bool pe_u64_add(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

static bool pe_u64_product(uint64_t left, uint64_t right,
                           uint64_t *result) {
    if (!result || (right != 0U && left > UINT64_MAX / right)) return false;
    *result = left * right;
    return true;
}

static bool pe_align_up(uint64_t value, uint64_t alignment,
                        uint64_t *result) {
    uint64_t mask;
    if (!result || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U)
        return false;
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask) return false;
    *result = (value + mask) & ~mask;
    return true;
}

static bool pe_absolute_range(const xx_pe *pe, uint64_t relative,
                              uint64_t size, int64_t *absolute) {
    int64_t total;
    uint64_t available;
    if (!pe || !pe->format.device || pe->format.base_address < 0 ||
        relative > (uint64_t)(INT64_MAX - pe->format.base_address))
        return false;
    total = xx_io_total_size(pe->format.device);
    if (total < pe->format.base_address) return false;
    available = (uint64_t)(total - pe->format.base_address);
    if (relative > available || size > available - relative) return false;
    if (absolute)
        *absolute = pe->format.base_address + (int64_t)relative;
    return true;
}

static bool pe_device_range(const xx_pe *pe, int64_t absolute,
                            uint64_t size) {
    int64_t total;
    if (!pe || !pe->format.device || absolute < 0 || size > INT64_MAX)
        return false;
    total = xx_io_total_size(pe->format.device);
    return absolute <= total && (int64_t)size <= total - absolute;
}

static bool pe_rva_range(const xx_pe *pe, const xx_memory_map *map,
                         uint64_t rva, uint64_t size, int64_t *absolute) {
    uint64_t address;
    int64_t offset;
    if (!pe || !map || rva > INT64_MAX || size == 0U || size > INT64_MAX)
        return false;
    address = xx_memory_map_relative_address_to_address(map, (int64_t)rva);
    if (address == XX_INVALID_ADDRESS ||
        !xx_memory_map_is_physical_address_range(map, address, (int64_t)size))
        return false;
    offset = xx_memory_map_relative_address_to_offset(map, (int64_t)rva);
    if (offset < 0 || !pe_device_range(pe, offset, size)) return false;
    if (absolute) *absolute = offset;
    return true;
}

static bool pe_va_range(const xx_pe *pe, const xx_memory_map *map,
                        uint64_t va, uint64_t size, int64_t *absolute) {
    int64_t offset;
    if (!pe || !map || va == 0U || size == 0U || size > INT64_MAX ||
        !xx_memory_map_is_physical_address_range(map, va, (int64_t)size))
        return false;
    offset = xx_memory_map_address_to_offset(map, va);
    if (offset < 0 || !pe_device_range(pe, offset, size)) return false;
    if (absolute) *absolute = offset;
    return true;
}

static uint32_t pe_u32(const xx_pe *pe, int64_t offset) {
    return xx_io_get_u32(pe->format.device, offset, false);
}

static uint64_t pe_pointer(const xx_pe *pe, int64_t offset) {
    return pe->optional_magic == XX_PE_MAGIC_64
               ? xx_io_get_u64(pe->format.device, offset, false)
               : pe_u32(pe, offset);
}

static void pe_data_stream_free(void *pointer) {
    pe_data_stream *stream = (pe_data_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool pe_has_exact(const pe_data_stream *stream, uint32_t id,
                         int64_t offset, uint64_t size) {
    size_t index;
    if (!stream || size > INT64_MAX) return false;
    for (index = 0U; index < stream->count; ++index) {
        const xx_data_struct *item = &stream->items[index];
        if (item->id == id && item->offset == offset &&
            item->total_size == (int64_t)size)
            return true;
    }
    return false;
}

static bool pe_append_absolute(xx_pe *pe, pe_data_stream *stream,
                               uint32_t id, int64_t offset,
                               uint64_t entry_size, uint64_t total_size,
                               uint64_t count,
                               xx_data_struct_type_t type) {
    xx_data_struct *grown;
    xx_data_struct *item;
    uint64_t address;
    size_t capacity;
    if (!pe || !stream || stream->count >= PE_DATA_MAX_ITEMS ||
        entry_size > INT64_MAX || total_size > INT64_MAX ||
        !pe_device_range(pe, offset, total_size))
        return false;
    if (pe_has_exact(stream, id, offset, total_size)) return true;
    if (stream->count == stream->capacity) {
        capacity = stream->capacity ? stream->capacity * 2U : 64U;
        if (capacity < stream->capacity || capacity > PE_DATA_MAX_ITEMS)
            capacity = (size_t)PE_DATA_MAX_ITEMS;
        grown = (xx_data_struct *)xx_mem_realloc(
            stream->items, capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    item = &stream->items[stream->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = offset;
    address = xx_format_offset_to_address(&pe->format, offset, NULL);
    item->address = address != XX_INVALID_ADDRESS && address <= INT64_MAX
                        ? (int64_t)address : -1;
    item->entry_size = (int64_t)entry_size;
    item->total_size = (int64_t)total_size;
    item->count = count;
    item->type = type;
    return true;
}

static bool pe_append_relative(xx_pe *pe, pe_data_stream *stream,
                               uint32_t id, uint64_t relative,
                               uint64_t entry_size, uint64_t total_size,
                               uint64_t count,
                               xx_data_struct_type_t type) {
    int64_t absolute;
    return pe_absolute_range(pe, relative, total_size, &absolute) &&
           pe_append_absolute(pe, stream, id, absolute, entry_size,
                              total_size, count, type);
}

static bool pe_append_rva(xx_pe *pe, pe_data_stream *stream,
                          const xx_memory_map *map, uint32_t id,
                          uint64_t rva, uint64_t entry_size,
                          uint64_t total_size, uint64_t count,
                          xx_data_struct_type_t type) {
    int64_t absolute;
    return pe_rva_range(pe, map, rva, total_size, &absolute) &&
           pe_append_absolute(pe, stream, id, absolute, entry_size,
                              total_size, count, type);
}

static bool pe_append_raw_absolute(xx_pe *pe, pe_data_stream *stream,
                                   uint32_t id, int64_t offset,
                                   uint64_t size) {
    if (size == 0U) return true;
    return pe_append_absolute(pe, stream, id, offset, 1U, size, size,
                              XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static bool pe_append_raw_relative(xx_pe *pe, pe_data_stream *stream,
                                   uint32_t id, uint64_t relative,
                                   uint64_t size) {
    int64_t absolute;
    if (size == 0U) return true;
    return pe_absolute_range(pe, relative, size, &absolute) &&
           pe_append_raw_absolute(pe, stream, id, absolute, size);
}

static bool pe_append_raw_rva(xx_pe *pe, pe_data_stream *stream,
                              const xx_memory_map *map, uint32_t id,
                              uint64_t rva, uint64_t size) {
    int64_t absolute;
    if (size == 0U) return true;
    return pe_rva_range(pe, map, rva, size, &absolute) &&
           pe_append_raw_absolute(pe, stream, id, absolute, size);
}

static uint64_t pe_cstring_size(const xx_pe *pe, int64_t offset,
                                uint64_t maximum) {
    uint64_t index;
    int64_t total;
    if (!pe || !pe->format.device || offset < 0) return 0U;
    total = xx_io_total_size(pe->format.device);
    if (offset >= total) return 0U;
    maximum = maximum > PE_DATA_MAX_STRING ? PE_DATA_MAX_STRING : maximum;
    if (maximum > (uint64_t)(total - offset)) maximum = (uint64_t)(total - offset);
    for (index = 0U; index < maximum; ++index) {
        if (xx_io_get_u8(pe->format.device, offset + (int64_t)index) == 0U)
            return index + 1U;
    }
    return 0U;
}

static bool pe_append_cstring_rva(xx_pe *pe, pe_data_stream *stream,
                                  const xx_memory_map *map, uint32_t id,
                                  uint32_t rva) {
    int64_t offset;
    uint64_t size;
    if (!rva || !pe_rva_range(pe, map, rva, 1U, &offset)) return true;
    size = pe_cstring_size(pe, offset, PE_DATA_MAX_STRING);
    return size == 0U ||
           pe_append_absolute(pe, stream, id, offset, 1U, size, size,
                              XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static const xx_data_struct_field_desc pe_dos_fields[] = {
    PE_FIELD("e_magic", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("e_cblp", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("e_cp", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("e_crlc", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("e_cparhdr", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("e_minalloc", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("e_maxalloc", "uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("e_ss", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_sp", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_csum", "uint16", 18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_ip", "uint16", 20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_cs", "uint16", 22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_lfarlc", "uint16", 24, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("e_ovno", "uint16", 26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("e_res", "uint16[4]", 28, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("e_oemid", "uint16", 36, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("e_oeminfo", "uint16", 38, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("e_res2", "uint16[10]", 40, 20, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("e_lfanew", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc pe_signature_fields[] = {
    PE_FIELD("Signature", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_file_fields[] = {
    PE_FIELD("Machine", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("NumberOfSections", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("PointerToSymbolTable", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("NumberOfSymbols", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("SizeOfOptionalHeader", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Characteristics", "uint16", 18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_optional32_fields[] = {
    PE_FIELD("Magic", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("MajorLinkerVersion", "uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorLinkerVersion", "uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("SizeOfCode", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfInitializedData", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfUninitializedData", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("AddressOfEntryPoint", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("BaseOfCode", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("BaseOfData", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ImageBase", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SectionAlignment", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FileAlignment", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MajorOperatingSystemVersion", "uint16", 40, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorOperatingSystemVersion", "uint16", 42, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MajorImageVersion", "uint16", 44, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorImageVersion", "uint16", 46, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MajorSubsystemVersion", "uint16", 48, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorSubsystemVersion", "uint16", 50, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Win32VersionValue", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("SizeOfImage", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeaders", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CheckSum", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Subsystem", "uint16", 68, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("DllCharacteristics", "uint16", 70, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("SizeOfStackReserve", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfStackCommit", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeapReserve", "uint32", 80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeapCommit", "uint32", 84, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("LoaderFlags", "uint32", 88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("NumberOfRvaAndSizes", "uint32", 92, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_optional64_fields[] = {
    PE_FIELD("Magic", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("MajorLinkerVersion", "uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorLinkerVersion", "uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("SizeOfCode", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfInitializedData", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfUninitializedData", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("AddressOfEntryPoint", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("BaseOfCode", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ImageBase", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SectionAlignment", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FileAlignment", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MajorOperatingSystemVersion", "uint16", 40, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorOperatingSystemVersion", "uint16", 42, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MajorImageVersion", "uint16", 44, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorImageVersion", "uint16", 46, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MajorSubsystemVersion", "uint16", 48, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorSubsystemVersion", "uint16", 50, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Win32VersionValue", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("SizeOfImage", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeaders", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CheckSum", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Subsystem", "uint16", 68, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("DllCharacteristics", "uint16", 70, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("SizeOfStackReserve", "uint64", 72, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfStackCommit", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeapReserve", "uint64", 88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SizeOfHeapCommit", "uint64", 96, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("LoaderFlags", "uint32", 104, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("NumberOfRvaAndSizes", "uint32", 108, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_directory_rva_fields[] = {
    PE_FIELD("VirtualAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_directory_file_fields[] = {
    PE_FIELD("FileOffset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_section_fields[] = {
    PE_FIELD("Name", "char[8]", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    PE_FIELD("VirtualSize", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VirtualAddress", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SizeOfRawData", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("PointerToRawData", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("PointerToRelocations", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("PointerToLinenumbers", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("NumberOfRelocations", "uint16", 32, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("NumberOfLinenumbers", "uint16", 34, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("Characteristics", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_symbol_fields[] = {
    PE_FIELD("Name", "char[8]", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    PE_FIELD("Value", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("SectionNumber", "int16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Type", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("StorageClass", "uint8", 16, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("NumberOfAuxSymbols", "uint8", 17, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_aux_generic_fields[] = {
    PE_FIELD("Data", "uint8[18]", 0, 18, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_aux_function_fields[] = {
    PE_FIELD("TagIndex", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("TotalSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("PointerToLinenumber", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("PointerToNextFunction", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Unused", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_aux_bfef_fields[] = {
    PE_FIELD("Unused1", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("Linenumber", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Unused2", "uint8[6]", 6, 6, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("PointerToNextFunction", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Unused3", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_aux_weak_fields[] = {
    PE_FIELD("TagIndex", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Characteristics", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Reserved", "uint8[10]", 8, 10, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_aux_file_fields[] = {
    PE_FIELD("FileName", "char[18]", 0, 18, XX_DATA_STRUCT_RECORD_PROPERTY_STRING)
};

static const xx_data_struct_field_desc pe_aux_section_fields[] = {
    PE_FIELD("Length", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("NumberOfRelocations", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("NumberOfLinenumbers", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("CheckSum", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Number", "int16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Selection", "uint8", 14, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved", "uint8", 15, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("HighNumber", "int16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_aux_clr_fields[] = {
    PE_FIELD("AuxType", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("SymbolTableIndex", "uint32", 2, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved2", "uint8[12]", 6, 12, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_string_table_header_fields[] = {
    PE_FIELD("TotalSize", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_relocation_fields[] = {
    PE_FIELD("VirtualAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SymbolTableIndex", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Type", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_line_fields[] = {
    PE_FIELD("SymbolTableIndexOrVirtualAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("Linenumber", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_export_fields[] = {
    PE_FIELD("Characteristics", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("MajorVersion", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Name", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Base", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("NumberOfFunctions", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("NumberOfNames", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("AddressOfFunctions", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfNames", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfNameOrdinals", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_rva_entry_fields[] = {
    PE_FIELD("Rva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_ordinal_fields[] = {
    PE_FIELD("OrdinalIndex", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_import_fields[] = {
    PE_FIELD("OriginalFirstThunk", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("ForwarderChain", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Name", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("FirstThunk", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_thunk32_fields[] = {
    PE_FIELD("AddressOfDataOrOrdinal", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_thunk64_fields[] = {
    PE_FIELD("AddressOfDataOrOrdinal", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_resource_dir_fields[] = {
    PE_FIELD("Characteristics", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("MajorVersion", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("NumberOfNamedEntries", "uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("NumberOfIdEntries", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_resource_entry_fields[] = {
    PE_FIELD("NameOrId", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("OffsetToDataOrDirectory", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_resource_data_fields[] = {
    PE_FIELD("OffsetToData", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CodePage", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_runtime_x64_fields[] = {
    PE_FIELD("BeginAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndAddress", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("UnwindInfoAddress", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_runtime_arm_fields[] = {
    PE_FIELD("BeginAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("UnwindData", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_runtime_mips_fields[] = {
    PE_FIELD("BeginAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndAddress", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExceptionHandler", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("HandlerData", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("PrologEndAddress", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_runtime_alpha64_fields[] = {
    PE_FIELD("BeginAddress", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndAddress", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExceptionHandler", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("HandlerData", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("PrologEndAddress", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_unwind_info_fields[] = {
    PE_FIELD("VersionAndFlags", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("SizeOfProlog", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CountOfCodes", "uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("FrameRegisterAndOffset", "uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_unwind_code_fields[] = {
    PE_FIELD("CodeOffset", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("UnwindOpAndInfo", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_arm_xdata_fields[] = {
    PE_FIELD("Header", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_win_certificate_fields[] = {
    PE_FIELD("dwLength", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("wRevision", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("wCertificateType", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_base_reloc_fields[] = {
    PE_FIELD("VirtualAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SizeOfBlock", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_base_reloc_entry_fields[] = {
    PE_FIELD("TypeAndOffset", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_debug_dir_fields[] = {
    PE_FIELD("Characteristics", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("MajorVersion", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Type", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("SizeOfData", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("AddressOfRawData", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("PointerToRawData", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc pe_debug_coff_fields[] = {
    PE_FIELD("NumberOfSymbols", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("LvaToFirstSymbol", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("NumberOfLinenumbers", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("LvaToFirstLinenumber", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("RvaToFirstByteOfCode", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("RvaToLastByteOfCode", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("RvaToFirstByteOfData", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("RvaToLastByteOfData", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_rsds_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Guid", "uint8[16]", 4, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Age", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_nb10_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Offset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("TimeDateStamp", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("Age", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_debug_misc_fields[] = {
    PE_FIELD("DataType", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Unicode", "uint8", 8, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Reserved", "uint8[3]", 9, 3, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_fpo_fields[] = {
    PE_FIELD("ulOffStart", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("cbProcSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("cdwLocals", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("cdwParams", "uint16", 12, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Attributes", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_omap_fields[] = {
    PE_FIELD("Rva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("RvaTo", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_vc_feature_fields[] = {
    PE_FIELD("PreVCPlusPlusCount", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("CAndCPlusPlusCount", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GSCount", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("SDLCount", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardNCount", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_pogo_header_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_pogo_entry_fields[] = {
    PE_FIELD("Rva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_mpdb_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("UncompressedSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_repro_fields[] = {
    PE_FIELD("HashLength", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_guid_fields[] = {
    PE_FIELD("Guid", "uint8[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_dword_flags_fields[] = {
    PE_FIELD("Flags", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_architecture_fields[] = {
    PE_FIELD("Flags", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("FirstEntryRVA", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_architecture_entry_fields[] = {
    PE_FIELD("FixupInstRVA", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("NewInst", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_tls32_fields[] = {
    PE_FIELD("StartAddressOfRawData", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndAddressOfRawData", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfIndex", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfCallBacks", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SizeOfZeroFill", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Characteristics", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_tls64_fields[] = {
    PE_FIELD("StartAddressOfRawData", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndAddressOfRawData", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfIndex", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AddressOfCallBacks", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SizeOfZeroFill", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Characteristics", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_pointer32_fields[] = {
    PE_FIELD("Address", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_pointer64_fields[] = {
    PE_FIELD("Address", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_load32_fields[] = {
    PE_FIELD("Size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("MajorVersion", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("GlobalFlagsClear", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("GlobalFlagsSet", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CriticalSectionDefaultTimeout", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("DeCommitFreeBlockThreshold", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("DeCommitTotalFreeThreshold", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("LockPrefixTable", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("MaximumAllocationSize", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VirtualMemoryThreshold", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ProcessHeapFlags", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("ProcessAffinityMask", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CSDVersion", "uint16", 52, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("DependentLoadFlags", "uint16", 54, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("EditList", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SecurityCookie", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SEHandlerTable", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SEHandlerCount", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardCFCheckFunctionPointer", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFDispatchFunctionPointer", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFFunctionTable", "uint32", 80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFFunctionCount", "uint32", 84, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardFlags", "uint32", 88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CodeIntegrity", "uint8[12]", 92, 12, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("GuardAddressTakenIatEntryTable", "uint32", 104, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardAddressTakenIatEntryCount", "uint32", 108, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardLongJumpTargetTable", "uint32", 112, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardLongJumpTargetCount", "uint32", 116, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("DynamicValueRelocTable", "uint32", 120, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CHPEMetadataPointer", "uint32", 124, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardRFFailureRoutine", "uint32", 128, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardRFFailureRoutineFunctionPointer", "uint32", 132, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("DynamicValueRelocTableOffset", "uint32", 136, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("DynamicValueRelocTableSection", "uint16", 140, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved2", "uint16", 142, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("GuardRFVerifyStackPointerFunctionPointer", "uint32", 144, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("HotPatchTableOffset", "uint32", 148, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Reserved3", "uint32", 152, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("EnclaveConfigurationPointer", "uint32", 156, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VolatileMetadataPointer", "uint32", 160, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardEHContinuationTable", "uint32", 164, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardEHContinuationCount", "uint32", 168, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardXFGCheckFunctionPointer", "uint32", 172, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardXFGDispatchFunctionPointer", "uint32", 176, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardXFGTableDispatchFunctionPointer", "uint32", 180, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CastGuardOsDeterminedFailureMode", "uint32", 184, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardMemcpyFunctionPointer", "uint32", 188, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("UmaFunctionPointers", "uint32", 192, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_load64_fields[] = {
    PE_FIELD("Size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("TimeDateStamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("MajorVersion", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("GlobalFlagsClear", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("GlobalFlagsSet", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CriticalSectionDefaultTimeout", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("DeCommitFreeBlockThreshold", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("DeCommitTotalFreeThreshold", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("LockPrefixTable", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("MaximumAllocationSize", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VirtualMemoryThreshold", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ProcessAffinityMask", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("ProcessHeapFlags", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CSDVersion", "uint16", 76, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("DependentLoadFlags", "uint16", 78, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("EditList", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SecurityCookie", "uint64", 88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SEHandlerTable", "uint64", 96, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SEHandlerCount", "uint64", 104, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardCFCheckFunctionPointer", "uint64", 112, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFDispatchFunctionPointer", "uint64", 120, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFFunctionTable", "uint64", 128, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardCFFunctionCount", "uint64", 136, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardFlags", "uint32", 144, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("CodeIntegrity", "uint8[12]", 148, 12, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("GuardAddressTakenIatEntryTable", "uint64", 160, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardAddressTakenIatEntryCount", "uint64", 168, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardLongJumpTargetTable", "uint64", 176, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardLongJumpTargetCount", "uint64", 184, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("DynamicValueRelocTable", "uint64", 192, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CHPEMetadataPointer", "uint64", 200, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardRFFailureRoutine", "uint64", 208, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardRFFailureRoutineFunctionPointer", "uint64", 216, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("DynamicValueRelocTableOffset", "uint32", 224, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("DynamicValueRelocTableSection", "uint16", 228, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved2", "uint16", 230, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("GuardRFVerifyStackPointerFunctionPointer", "uint64", 232, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("HotPatchTableOffset", "uint32", 240, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Reserved3", "uint32", 244, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("EnclaveConfigurationPointer", "uint64", 248, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VolatileMetadataPointer", "uint64", 256, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardEHContinuationTable", "uint64", 264, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardEHContinuationCount", "uint64", 272, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GuardXFGCheckFunctionPointer", "uint64", 280, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardXFGDispatchFunctionPointer", "uint64", 288, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardXFGTableDispatchFunctionPointer", "uint64", 296, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CastGuardOsDeterminedFailureMode", "uint64", 304, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("GuardMemcpyFunctionPointer", "uint64", 312, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("UmaFunctionPointers", "uint64", 320, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_code_integrity_fields[] = {
    PE_FIELD("Flags", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Catalog", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("CatalogOffset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Reserved", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_dynamic_table_fields[] = {
    PE_FIELD("Version", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_dynamic32_fields[] = {
    PE_FIELD("Symbol", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("BaseRelocSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_dynamic64_fields[] = {
    PE_FIELD("Symbol", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("BaseRelocSize", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_dynamic32_v2_fields[] = {
    PE_FIELD("HeaderSize", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FixupInfoSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Symbol", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("SymbolGroup", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Flags", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_dynamic64_v2_fields[] = {
    PE_FIELD("HeaderSize", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FixupInfoSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Symbol", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("SymbolGroup", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Flags", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_epilogue_fields[] = {
    PE_FIELD("EpilogueCount", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("EpilogueByteCount", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("BranchDescriptorElementSize", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("BranchDescriptorCount", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_function_override_fields[] = {
    PE_FIELD("FuncOverrideSize", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_function_override_entry_fields[] = {
    PE_FIELD("OriginalRva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("BDDOffset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("RvaSize", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("BaseRelocSize", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_bdd_info_fields[] = {
    PE_FIELD("Version", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("BDDSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_bdd_node_fields[] = {
    PE_FIELD("Left", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Right", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Value", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_hot_patch_info_fields[] = {
    PE_FIELD("Version", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("SequenceNumber", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("BaseImageList", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("BaseImageCount", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("BufferOffset", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("ExtraPatchSize", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MinSequenceNumber", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Flags", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_hot_patch_base_fields[] = {
    PE_FIELD("SequenceNumber", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Flags", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("OriginalTimeDateStamp", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("OriginalCheckSum", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("CodeIntegrityInfo", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("CodeIntegritySize", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("PatchTable", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("BufferOffset", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc pe_hot_patch_hashes_fields[] = {
    PE_FIELD("SHA256", "uint8[32]", 0, 32, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("SHA1", "uint8[20]", 32, 20, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_enclave32_fields[] = {
    PE_FIELD("Size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MinimumRequiredConfigSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("PolicyFlags", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("NumberOfImports", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("ImportList", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ImportEntrySize", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FamilyID", "uint8[16]", 24, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImageID", "uint8[16]", 40, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImageVersion", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("SecurityVersion", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("EnclaveSize", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("NumberOfThreads", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("EnclaveFlags", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_enclave64_fields[] = {
    PE_FIELD("Size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MinimumRequiredConfigSize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("PolicyFlags", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("NumberOfImports", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("ImportList", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ImportEntrySize", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("FamilyID", "uint8[16]", 24, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImageID", "uint8[16]", 40, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImageVersion", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("SecurityVersion", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("EnclaveSize", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("NumberOfThreads", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("EnclaveFlags", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_enclave_import_fields[] = {
    PE_FIELD("MatchType", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("MinimumSecurityVersion", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("UniqueOrAuthorID", "uint8[32]", 8, 32, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("FamilyID", "uint8[16]", 40, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImageID", "uint8[16]", 56, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ImportName", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Reserved", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_volatile_fields[] = {
    PE_FIELD("Size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Version", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("VolatileAccessTable", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VolatileAccessTableSize", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VolatileInfoRangeTable", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VolatileInfoRangeTableSize", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_volatile_range_fields[] = {
    PE_FIELD("Rva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_chpe_fields[] = {
    PE_FIELD("Version", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("CodeMap", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CodeMapCount", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("CodeRangesToEntryPoints", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("RedirectionMetadata", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("__os_arm64x_dispatch_call_no_redirect", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("__os_arm64x_dispatch_ret", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("__os_arm64x_dispatch_call", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("__os_arm64x_dispatch_icall", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("__os_arm64x_dispatch_icall_cfg", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AlternateEntryPoint", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AuxiliaryIAT", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CodeRangesToEntryPointsCount", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("RedirectionMetadataCount", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("GetX64InformationFunctionPointer", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("SetX64InformationFunctionPointer", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExtraRFETable", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExtraRFETableSize", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("__os_arm64x_dispatch_fptr", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AuxiliaryIATCopy", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AuxiliaryDelayloadIAT", "uint32", 80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("AuxiliaryDelayloadIATCopy", "uint32", 84, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("HybridImageInfoBitfield", "uint32", 88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_chpe_code_range_fields[] = {
    PE_FIELD("StartOffsetAndType", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_chpe_range_entry_fields[] = {
    PE_FIELD("StartRva", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndRva", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EntryPoint", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_chpe_redirect_fields[] = {
    PE_FIELD("Source", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Destination", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_bound_fields[] = {
    PE_FIELD("TimeDateStamp", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("OffsetModuleName", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("NumberOfModuleForwarderRefs", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_bound_forwarder_fields[] = {
    PE_FIELD("TimeDateStamp", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    PE_FIELD("OffsetModuleName", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Reserved", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_delay_fields[] = {
    PE_FIELD("Attributes", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("DllNameRVA", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("ModuleHandleRVA", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("ImportAddressTableRVA", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("ImportNameTableRVA", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("BoundImportAddressTableRVA", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("UnloadInformationTableRVA", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("TimeDateStamp", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP)
};

static const xx_data_struct_field_desc pe_cor20_fields[] = {
    PE_FIELD("cb", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("MajorRuntimeVersion", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorRuntimeVersion", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MetaData.VirtualAddress", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("MetaData.Size", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("Flags", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("EntryPointTokenOrRVA", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    PE_FIELD("Resources.VirtualAddress", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Resources.Size", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("StrongNameSignature.VirtualAddress", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("StrongNameSignature.Size", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("CodeManagerTable.VirtualAddress", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("CodeManagerTable.Size", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("VTableFixups.VirtualAddress", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("VTableFixups.Size", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ExportAddressTableJumps.VirtualAddress", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ExportAddressTableJumps.Size", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    PE_FIELD("ManagedNativeHeader.VirtualAddress", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("ManagedNativeHeader.Size", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_clr_root_fields[] = {
    PE_FIELD("Signature", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("MajorVersion", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("MinorVersion", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    PE_FIELD("Reserved", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("VersionLength", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_clr_storage_fields[] = {
    PE_FIELD("Flags", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Pad", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("Streams", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_clr_stream_fields[] = {
    PE_FIELD("Offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    PE_FIELD("Size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc pe_clr_vtable_fields[] = {
    PE_FIELD("RVA", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("Count", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("Type", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_arm64x_fixup_fields[] = {
    PE_FIELD("TypeOffsetArg", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_arm64x_delta_fields[] = {
    PE_FIELD("TypeOffsetSignScale", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    PE_FIELD("Magnitude", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_r2r_fields[] = {
    PE_FIELD("Magic", "char[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Signature", "uint8[16]", 4, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Version", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc pe_debug_function32_fields[] = {
    PE_FIELD("StartingAddress", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndingAddress", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndOfPrologue", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_debug_function64_fields[] = {
    PE_FIELD("StartingAddress", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndingAddress", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    PE_FIELD("EndOfPrologueOrUnwindInfoAddress", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_xdata_extended_fields[] = {
    PE_FIELD("ExtendedEpilogCount", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("ExtendedCodeWords", "uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    PE_FIELD("Reserved", "uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc pe_xdata_scope_fields[] = {
    PE_FIELD("Scope", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_unwind_handler_fields[] = {
    PE_FIELD("HandlerRVA", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc pe_prologue_fields[] = {
    PE_FIELD("PrologueByteCount", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc pe_word_flags_fields[] = {
    PE_FIELD("Value", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc pe_policy_metadata_fields[] = {
    PE_FIELD("Version", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("Reserved", "uint8[7]", 1, 7, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    PE_FIELD("ApplicationId", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc pe_policy_entry32_fields[] = {
    PE_FIELD("Type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("PolicyId", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ValueOrPointer", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc pe_policy_entry64_fields[] = {
    PE_FIELD("Type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("PolicyId", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    PE_FIELD("ValueOrPointer", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static bool pe_append_core(xx_pe *pe, pe_data_stream *stream,
                           xx_pd_struct *pd) {
    uint64_t optional_relative;
    uint64_t prefix_size;
    uint64_t directory_bytes;
    uint64_t section_relative;
    uint64_t section_bytes;
    uint64_t cursor;
    uint16_t section;
    if (xx_pd_is_stopped(pd)) return false;
    optional_relative = (uint64_t)pe->pe_offset + 24U;
    prefix_size = pe->optional_magic == XX_PE_MAGIC_64
                      ? PE_OPTIONAL64_SIZE : PE_OPTIONAL32_SIZE;
    if (!pe_append_relative(pe, stream, XX_PE_DATA_STRUCT_DOS_HEADER, 0U,
                            PE_DOS_HEADER_SIZE, PE_DOS_HEADER_SIZE, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        (pe->pe_offset > PE_DOS_HEADER_SIZE &&
         !pe_append_raw_relative(pe, stream, XX_PE_DATA_STRUCT_DOS_STUB,
                                 PE_DOS_HEADER_SIZE,
                                 pe->pe_offset - PE_DOS_HEADER_SIZE)) ||
        !pe_append_relative(pe, stream, XX_PE_DATA_STRUCT_NT_SIGNATURE,
                            pe->pe_offset, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        !pe_append_relative(pe, stream, XX_PE_DATA_STRUCT_FILE_HEADER,
                            (uint64_t)pe->pe_offset + 4U,
                            PE_FILE_HEADER_SIZE, PE_FILE_HEADER_SIZE, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        !pe_append_relative(
            pe, stream,
            pe->optional_magic == XX_PE_MAGIC_64
                ? XX_PE_DATA_STRUCT_OPTIONAL_HEADER64
                : XX_PE_DATA_STRUCT_OPTIONAL_HEADER32,
            optional_relative, prefix_size, prefix_size, 1U,
            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    directory_bytes = (uint64_t)pe->number_of_data_directories *
                      PE_DIRECTORY_ENTRY_SIZE;
    for (section = 0U; section < pe->number_of_data_directories; ++section) {
        if (xx_pd_is_stopped(pd) ||
            !pe_append_relative(pe, stream, XX_PE_DATA_STRUCT_DATA_DIRECTORY,
                                optional_relative + prefix_size +
                                    (uint64_t)section * PE_DIRECTORY_ENTRY_SIZE,
                                PE_DIRECTORY_ENTRY_SIZE,
                                PE_DIRECTORY_ENTRY_SIZE, 1U,
                                XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
    }
    if ((uint64_t)pe->optional_header_size > prefix_size + directory_bytes &&
        !pe_append_raw_relative(
            pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
            optional_relative + prefix_size + directory_bytes,
            (uint64_t)pe->optional_header_size - prefix_size - directory_bytes))
        return false;
    section_relative = optional_relative + pe->optional_header_size;
    if (!pe_u64_product(pe->number_of_sections, PE_SECTION_HEADER_SIZE,
                        &section_bytes) ||
        !pe_append_relative(pe, stream, XX_PE_DATA_STRUCT_SECTION_HEADER,
                            section_relative, PE_SECTION_HEADER_SIZE,
                            section_bytes, pe->number_of_sections,
                            XX_DATA_STRUCT_TYPE_ENTRY))
        return false;

    /* Expose alignment holes outside section payloads without labeling the
       payloads themselves as unparsed. */
    cursor = section_relative + section_bytes;
    while (true) {
        uint64_t next = UINT64_MAX;
        uint64_t next_end = 0U;
        for (section = 0U; section < pe->number_of_sections; ++section) {
            const xx_pe_section *item = &pe->sections[section];
            uint64_t end;
            if (!item->raw_size || item->raw_offset < cursor ||
                item->raw_offset >= next ||
                !pe_u64_add(item->raw_offset, item->raw_size, &end))
                continue;
            next = item->raw_offset;
            next_end = end;
        }
        if (next == UINT64_MAX) break;
        if (next > cursor &&
            !pe_append_raw_relative(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                    cursor, next - cursor))
            return false;
        cursor = next_end;
        for (section = 0U; section < pe->number_of_sections; ++section) {
            const xx_pe_section *item = &pe->sections[section];
            uint64_t end;
            if (item->raw_size && item->raw_offset <= cursor &&
                pe_u64_add(item->raw_offset, item->raw_size, &end) &&
                end > cursor)
                cursor = end;
        }
    }
    return true;
}

static uint32_t pe_aux_id(uint8_t storage, uint16_t type,
                          int16_t section_number, uint32_t value) {
    if (storage == 2U && (type & UINT16_C(0x30)) == UINT16_C(0x20))
        return XX_PE_DATA_STRUCT_COFF_AUX_FUNCTION_DEFINITION;
    if (storage == 101U)
        return XX_PE_DATA_STRUCT_COFF_AUX_BF_EF;
    if (storage == 105U)
        return XX_PE_DATA_STRUCT_COFF_AUX_WEAK_EXTERNAL;
    if (storage == 103U)
        return XX_PE_DATA_STRUCT_COFF_AUX_FILE;
    if (storage == 107U)
        return XX_PE_DATA_STRUCT_COFF_AUX_CLR_TOKEN;
    if (storage == 3U && section_number > 0 && value == 0U)
        return XX_PE_DATA_STRUCT_COFF_AUX_SECTION_DEFINITION;
    return XX_PE_DATA_STRUCT_COFF_AUX_SYMBOL;
}

static bool pe_append_coff(xx_pe *pe, pe_data_stream *stream,
                           xx_pd_struct *pd) {
    uint64_t file_relative = (uint64_t)pe->pe_offset + 4U;
    int64_t file_offset;
    uint32_t symbol_pointer;
    uint32_t symbol_count;
    uint64_t symbols_size;
    uint64_t index = 0U;
    uint16_t section;
    if (!pe_absolute_range(pe, file_relative, PE_FILE_HEADER_SIZE,
                           &file_offset))
        return true;
    symbol_pointer = pe_u32(pe, file_offset + 8);
    symbol_count = pe_u32(pe, file_offset + 12);
    if (symbol_pointer && symbol_count &&
        symbol_count <= PE_DATA_MAX_TABLE_ENTRIES &&
        pe_u64_product(symbol_count, PE_SYMBOL_SIZE, &symbols_size) &&
        pe_absolute_range(pe, symbol_pointer, symbols_size, NULL)) {
        while (index < symbol_count) {
            int64_t symbol_offset;
            uint8_t aux_count;
            uint64_t available_aux;
            uint32_t aux_id;
            if (xx_pd_is_stopped(pd) ||
                !pe_absolute_range(pe,
                                   (uint64_t)symbol_pointer +
                                       index * PE_SYMBOL_SIZE,
                                   PE_SYMBOL_SIZE, &symbol_offset) ||
                !pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_COFF_SYMBOL,
                                    symbol_offset, PE_SYMBOL_SIZE,
                                    PE_SYMBOL_SIZE, 1U,
                                    XX_DATA_STRUCT_TYPE_ENTRY))
                return false;
            aux_count = xx_io_get_u8(pe->format.device, symbol_offset + 17);
            available_aux = symbol_count - index - 1U;
            if (aux_count > available_aux) aux_count = (uint8_t)available_aux;
            aux_id = pe_aux_id(
                xx_io_get_u8(pe->format.device, symbol_offset + 16),
                xx_io_get_u16(pe->format.device, symbol_offset + 14, false),
                xx_io_get_i16(pe->format.device, symbol_offset + 12, false),
                pe_u32(pe, symbol_offset + 8));
            if (aux_count &&
                !pe_append_relative(pe, stream, aux_id,
                                    (uint64_t)symbol_pointer +
                                        (index + 1U) * PE_SYMBOL_SIZE,
                                    PE_SYMBOL_SIZE,
                                    (uint64_t)aux_count * PE_SYMBOL_SIZE,
                                    aux_count, XX_DATA_STRUCT_TYPE_ENTRY))
                return false;
            index += 1U + aux_count;
        }
        if ((uint64_t)symbol_pointer <= UINT64_MAX - symbols_size) {
            uint64_t string_relative = (uint64_t)symbol_pointer + symbols_size;
            int64_t string_offset;
            if (pe_absolute_range(pe, string_relative, 4U, &string_offset)) {
                uint32_t table_size = pe_u32(pe, string_offset);
                if (table_size >= 4U && table_size <= PE_DATA_MAX_TABLE_ENTRIES * 16U &&
                    pe_absolute_range(pe, string_relative, table_size, NULL)) {
                    uint64_t cursor = 4U;
                    if (!pe_append_relative(
                            pe, stream,
                            XX_PE_DATA_STRUCT_COFF_STRING_TABLE_HEADER,
                            string_relative, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
                        return false;
                    while (cursor < table_size) {
                        uint64_t length = pe_cstring_size(
                            pe, string_offset + (int64_t)cursor,
                            table_size - cursor);
                        if (!length) {
                            if (!pe_append_raw_relative(
                                    pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                    string_relative + cursor,
                                    table_size - cursor))
                                return false;
                            break;
                        }
                        if (!pe_append_relative(
                                pe, stream, XX_PE_DATA_STRUCT_COFF_STRING,
                                string_relative + cursor, 1U, length, length,
                                XX_DATA_STRUCT_TYPE_RAW_DATA))
                            return false;
                        cursor += length;
                    }
                }
            }
        }
    }

    for (section = 0U; section < pe->number_of_sections; ++section) {
        int64_t header_offset;
        uint32_t reloc_pointer;
        uint32_t line_pointer;
        uint32_t reloc_count;
        uint32_t line_count;
        uint32_t section_characteristics;
        uint64_t bytes;
        uint64_t header_relative = (uint64_t)pe->pe_offset + 24U +
                                   pe->optional_header_size +
                                   (uint64_t)section * PE_SECTION_HEADER_SIZE;
        if (!pe_absolute_range(pe, header_relative, PE_SECTION_HEADER_SIZE,
                               &header_offset))
            return false;
        reloc_pointer = pe_u32(pe, header_offset + 24);
        line_pointer = pe_u32(pe, header_offset + 28);
        reloc_count = xx_io_get_u16(pe->format.device, header_offset + 32,
                                    false);
        line_count = xx_io_get_u16(pe->format.device, header_offset + 34,
                                   false);
        section_characteristics = pe_u32(pe, header_offset + 36);
        if (reloc_pointer && reloc_count == UINT16_MAX &&
            (section_characteristics & UINT32_C(0x01000000)) != 0U &&
            pe_absolute_range(pe, reloc_pointer, PE_RELOCATION_SIZE,
                              NULL)) {
            uint32_t stored_count;
            uint32_t actual_count;
            int64_t marker_offset;
            (void)pe_absolute_range(pe, reloc_pointer, PE_RELOCATION_SIZE,
                                    &marker_offset);
            stored_count = pe_u32(pe, marker_offset);
            actual_count = stored_count ? stored_count - 1U : 0U;
            if (stored_count >= 1U &&
                stored_count <= PE_DATA_MAX_TABLE_ENTRIES &&
                pe_u64_product(stored_count, PE_RELOCATION_SIZE, &bytes) &&
                pe_absolute_range(pe, reloc_pointer, bytes, NULL)) {
                if (!pe_append_relative(
                        pe, stream, XX_PE_DATA_STRUCT_COFF_RELOCATION,
                        reloc_pointer, PE_RELOCATION_SIZE,
                        PE_RELOCATION_SIZE, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY) ||
                    (actual_count && !pe_append_relative(
                        pe, stream, XX_PE_DATA_STRUCT_COFF_RELOCATION,
                        (uint64_t)reloc_pointer + PE_RELOCATION_SIZE,
                        PE_RELOCATION_SIZE,
                        (uint64_t)actual_count * PE_RELOCATION_SIZE,
                        actual_count, XX_DATA_STRUCT_TYPE_ENTRY)))
                    return false;
                reloc_count = 0U;
            }
        }
        if (reloc_pointer && reloc_count &&
            pe_u64_product(reloc_count, PE_RELOCATION_SIZE, &bytes) &&
            pe_absolute_range(pe, reloc_pointer, bytes, NULL) &&
            !pe_append_relative(pe, stream,
                                XX_PE_DATA_STRUCT_COFF_RELOCATION,
                                reloc_pointer, PE_RELOCATION_SIZE, bytes,
                                reloc_count, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        if (line_pointer && line_count &&
            pe_u64_product(line_count, PE_LINE_NUMBER_SIZE, &bytes) &&
            pe_absolute_range(pe, line_pointer, bytes, NULL) &&
            !pe_append_relative(pe, stream,
                                XX_PE_DATA_STRUCT_COFF_LINE_NUMBER,
                                line_pointer, PE_LINE_NUMBER_SIZE, bytes,
                                line_count, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
    }
    return true;
}

static bool pe_cstring_equals(const xx_pe *pe, int64_t offset,
                              uint64_t maximum, const char *expected) {
    size_t length;
    size_t index;
    if (!pe || !expected || offset < 0) return false;
    length = xx_rt_strlen(expected);
    if ((uint64_t)length + 1U > maximum ||
        !pe_device_range(pe, offset, (uint64_t)length + 1U))
        return false;
    for (index = 0U; index < length; ++index)
        if (xx_io_get_u8(pe->format.device, offset + (int64_t)index) !=
            (uint8_t)expected[index])
            return false;
    return xx_io_get_u8(pe->format.device,
                        offset + (int64_t)length) == 0U;
}

static bool pe_append_policy_metadata(xx_pe *pe, pe_data_stream *stream,
                                      const xx_memory_map *map,
                                      uint32_t rva, xx_pd_struct *pd) {
    const xx_memory_record *record;
    int64_t offset;
    uint64_t available;
    uint64_t stride = 16U;
    uint64_t count = 0U;
    bool terminated = false;
    if (!rva || !pe_rva_range(pe, map, rva, 16U, &offset)) return true;
    record = xx_memory_map_record_by_relative_address(map, rva);
    if (!record || record->offset < 0 || record->size <= 0 ||
        (record->file_part & XX_FILE_PART_SECTION) == 0U ||
        record->file_part_number <= 0 ||
        (uint32_t)record->file_part_number > pe->number_of_sections ||
        xx_rt_strcmp(pe->sections[record->file_part_number - 1].name,
                     ".tPolicy") != 0 ||
        offset < record->offset || offset >= record->offset + record->size)
        return true;
    available = (uint64_t)(record->offset + record->size - offset);
    if (available > 16U + PE_DATA_MAX_LINKED_ENTRIES * stride)
        available = 16U + PE_DATA_MAX_LINKED_ENTRIES * stride;
    if (xx_io_get_u8(pe->format.device, offset) != 1U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, available);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_POLICY_METADATA,
                            offset, 16U, 16U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    while (count < PE_DATA_MAX_LINKED_ENTRIES &&
           16U + (count + 1U) * stride <= available) {
        int64_t entry = offset + 16 + (int64_t)(count * stride);
        if (xx_pd_is_stopped(pd)) return false;
        ++count;
        if (pe_u32(pe, entry) == 0U && pe_u32(pe, entry + 4) == 0U &&
            xx_io_get_u64(pe->format.device, entry + 8, false) == 0U) {
            terminated = true;
            break;
        }
    }
    if (!terminated)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset + 16, available - 16U);
    if (count && !pe_append_absolute(
                     pe, stream,
                     pe->optional_magic == XX_PE_MAGIC_64
                         ? XX_PE_DATA_STRUCT_POLICY_ENTRY64
                         : XX_PE_DATA_STRUCT_POLICY_ENTRY32,
                     offset + 16, stride, count * stride, count,
                     XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index + 1U < count; ++index) {
        int64_t entry = offset + 16 + (int64_t)(index * stride);
        uint32_t type = pe_u32(pe, entry);
        uint64_t value;
        int64_t string_offset;
        uint64_t string_size;
        if (xx_pd_is_stopped(pd)) return false;
        /* Type 10 is a NUL-terminated ANSI VA.  Type 11 is UTF-16 and is
           already represented by the fixed entry until a public UTF-16
           string data-structure id exists. */
        if (type != 10U) continue;
        value = xx_io_get_u64(pe->format.device, entry + 8, false);
        if (!value || !pe_va_range(pe, map, value, 1U, &string_offset))
            continue;
        string_size = pe_cstring_size(pe, string_offset,
                                      PE_DATA_MAX_STRING);
        if (string_size && !pe_append_absolute(
                               pe, stream,
                               XX_PE_DATA_STRUCT_ASCII_STRING,
                               string_offset, 1U, string_size,
                               string_size,
                               XX_DATA_STRUCT_TYPE_RAW_DATA))
            return false;
    }
    return true;
}

static bool pe_append_export(xx_pe *pe, pe_data_stream *stream,
                             const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_EXPORT];
    uint32_t size = pe->data_directory_size[PE_DIR_EXPORT];
    int64_t offset;
    uint32_t function_count;
    uint32_t name_count;
    uint32_t function_rva;
    uint32_t names_rva;
    uint32_t ordinals_rva;
    uint64_t bytes;
    uint32_t index;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (size < 40U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_EXPORT_DIRECTORY, offset,
                            40U, 40U, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    function_count = pe_u32(pe, offset + 20);
    name_count = pe_u32(pe, offset + 24);
    function_rva = pe_u32(pe, offset + 28);
    names_rva = pe_u32(pe, offset + 32);
    ordinals_rva = pe_u32(pe, offset + 36);
    if (!pe_append_cstring_rva(pe, stream, map,
                               XX_PE_DATA_STRUCT_ASCII_STRING,
                               pe_u32(pe, offset + 12)))
        return false;
    if (function_count > PE_DATA_MAX_TABLE_ENTRIES ||
        name_count > PE_DATA_MAX_TABLE_ENTRIES)
        return pe_append_raw_absolute(pe, stream,
                                      XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                                      offset + 40, size - 40U);
    if (function_count) {
        bool function_table_valid =
            pe_u64_product(function_count, 4U, &bytes) &&
            pe_rva_range(pe, map, function_rva, bytes, NULL);
        if (function_table_valid &&
            !pe_append_rva(pe, stream, map,
                           XX_PE_DATA_STRUCT_EXPORT_ADDRESS_TABLE,
                           function_rva, 4U, bytes, function_count,
                           XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        for (index = 0U; function_table_valid && index < function_count;
             ++index) {
            int64_t table_offset;
            uint32_t value;
            if (xx_pd_is_stopped(pd)) return false;
            if (!pe_rva_range(pe, map, function_rva + (uint64_t)index * 4U,
                              4U, &table_offset))
                break;
            value = pe_u32(pe, table_offset);
            if (value >= rva && value < (uint64_t)rva + size &&
                !pe_append_cstring_rva(pe, stream, map,
                                       XX_PE_DATA_STRUCT_EXPORT_FORWARDER,
                                       value))
                return false;
        }
    }
    if (name_count) {
        int64_t name_table = -1;
        int64_t ordinal_table = -1;
        uint64_t name_bytes;
        uint64_t ordinal_bytes;
        bool names_valid = pe_u64_product(name_count, 4U, &name_bytes) &&
            pe_rva_range(pe, map, names_rva, name_bytes, &name_table);
        bool ordinals_valid = pe_u64_product(name_count, 2U,
                                             &ordinal_bytes) &&
            pe_rva_range(pe, map, ordinals_rva, ordinal_bytes,
                         &ordinal_table);
        if (names_valid &&
            !pe_append_rva(pe, stream, map,
                           XX_PE_DATA_STRUCT_EXPORT_NAME_POINTER_TABLE,
                           names_rva, 4U, name_bytes, name_count,
                           XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        if (ordinals_valid &&
            !pe_append_rva(pe, stream, map,
                           XX_PE_DATA_STRUCT_EXPORT_ORDINAL_TABLE,
                           ordinals_rva, 2U, ordinal_bytes, name_count,
                           XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        for (index = 0U; names_valid && index < name_count; ++index) {
            uint32_t export_name_rva = pe_u32(
                pe, name_table + (int64_t)index * 4);
            int64_t export_name_offset;
            if (xx_pd_is_stopped(pd) ||
                !pe_append_cstring_rva(pe, stream, map,
                                       XX_PE_DATA_STRUCT_ASCII_STRING,
                                       export_name_rva))
                return false;
            if (ordinals_valid &&
                pe_rva_range(pe, map, export_name_rva, 1U,
                             &export_name_offset) &&
                pe_cstring_equals(pe, export_name_offset,
                                  PE_DATA_MAX_STRING,
                                  "__ImagePolicyMetadata")) {
                uint16_t ordinal_index = xx_io_get_u16(
                    pe->format.device,
                    ordinal_table + (int64_t)index * 2, false);
                int64_t eat_entry;
                if (ordinal_index < function_count &&
                    pe_rva_range(pe, map,
                                 (uint64_t)function_rva +
                                     (uint64_t)ordinal_index * 4U,
                                 4U, &eat_entry)) {
                    uint32_t policy_rva = pe_u32(pe, eat_entry);
                    if ((policy_rva < rva ||
                         policy_rva >= (uint64_t)rva + size) &&
                        !pe_append_policy_metadata(
                            pe, stream, map, policy_rva, pd))
                        return false;
                }
            }
        }
    }
    return true;
}

static bool pe_location_range(const xx_pe *pe, const xx_memory_map *map,
                              uint64_t location, bool is_va, uint64_t size,
                              int64_t *offset) {
    return is_va ? pe_va_range(pe, map, location, size, offset)
                 : pe_rva_range(pe, map, location, size, offset);
}

static bool pe_append_location_string(xx_pe *pe, pe_data_stream *stream,
                                      const xx_memory_map *map, uint32_t id,
                                      uint64_t location, bool is_va) {
    int64_t offset;
    uint64_t length;
    if (!location || !pe_location_range(pe, map, location, is_va, 1U,
                                        &offset))
        return true;
    length = pe_cstring_size(pe, offset, PE_DATA_MAX_STRING);
    return !length || pe_append_absolute(pe, stream, id, offset, 1U, length,
                                         length,
                                         XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static bool pe_append_thunks(xx_pe *pe, pe_data_stream *stream,
                             const xx_memory_map *map, uint64_t lookup,
                             uint64_t iat, bool location_is_va,
                             bool values_are_va, xx_pd_struct *pd) {
    uint64_t stride = pe->optional_magic == XX_PE_MAGIC_64 ? 8U : 4U;
    uint64_t ordinal_mask = pe->optional_magic == XX_PE_MAGIC_64
                                ? UINT64_C(0x8000000000000000)
                                : UINT64_C(0x80000000);
    uint64_t count = 0U;
    int64_t lookup_offset;
    int64_t iat_offset = -1;
    uint64_t bytes;
    if (!lookup || !pe_location_range(pe, map, lookup, location_is_va,
                                      stride, &lookup_offset))
        return true;
    while (count < PE_DATA_MAX_LINKED_ENTRIES) {
        uint64_t value;
        int64_t current;
        if (xx_pd_is_stopped(pd) ||
            !pe_location_range(pe, map, lookup + count * stride,
                               location_is_va, stride, &current))
            break;
        value = stride == 8U ? xx_io_get_u64(pe->format.device, current, false)
                             : pe_u32(pe, current);
        if (!value) break;
        ++count;
    }
    if (!count || !pe_u64_product(count, stride, &bytes)) return true;
    if (!pe_append_absolute(
            pe, stream,
            stride == 8U ? XX_PE_DATA_STRUCT_THUNK64
                         : XX_PE_DATA_STRUCT_THUNK32,
            lookup_offset, stride, bytes, count, XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    if (iat && pe_location_range(pe, map, iat, location_is_va, bytes,
                                 &iat_offset) &&
        !pe_append_absolute(
            pe, stream,
            stride == 8U ? XX_PE_DATA_STRUCT_IAT64
                         : XX_PE_DATA_STRUCT_IAT32,
            iat_offset, stride, bytes, count, XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < count; ++index) {
        uint64_t value = stride == 8U
                             ? xx_io_get_u64(pe->format.device,
                                             lookup_offset +
                                                 (int64_t)(index * stride),
                                             false)
                             : pe_u32(pe, lookup_offset +
                                             (int64_t)(index * stride));
        int64_t name_offset;
        uint64_t name_length;
        if (xx_pd_is_stopped(pd)) return false;
        if (value & ordinal_mask) continue;
        if (!pe_location_range(pe, map, value, values_are_va, 3U,
                               &name_offset))
            continue;
        name_length = pe_cstring_size(pe, name_offset + 2,
                                      PE_DATA_MAX_STRING);
        if (name_length &&
            !pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_IMPORT_BY_NAME,
                                name_offset, 2U + name_length,
                                2U + name_length, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
    }
    return true;
}

static bool pe_append_import(xx_pe *pe, pe_data_stream *stream,
                             const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_IMPORT];
    uint32_t size = pe->data_directory_size[PE_DIR_IMPORT];
    int64_t offset;
    uint64_t count = 0U;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (size < 20U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    while (count < size / 20U && count < PE_DATA_MAX_LINKED_ENTRIES) {
        int64_t current = offset + (int64_t)(count * 20U);
        if (pe_u32(pe, current) == 0U && pe_u32(pe, current + 4) == 0U &&
            pe_u32(pe, current + 8) == 0U && pe_u32(pe, current + 12) == 0U &&
            pe_u32(pe, current + 16) == 0U)
            break;
        ++count;
    }
    if (count &&
        !pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_IMPORT_DESCRIPTOR, offset,
                            20U, count * 20U, count,
                            XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < count; ++index) {
        int64_t current = offset + (int64_t)(index * 20U);
        uint32_t original = pe_u32(pe, current);
        uint32_t name = pe_u32(pe, current + 12);
        uint32_t first = pe_u32(pe, current + 16);
        if (xx_pd_is_stopped(pd) ||
            !pe_append_cstring_rva(pe, stream, map,
                                   XX_PE_DATA_STRUCT_ASCII_STRING, name) ||
            !pe_append_thunks(pe, stream, map,
                              original ? original : first, first,
                              false, false, pd))
            return false;
    }
    if (count == 0U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    return true;
}

static bool pe_resource_seen(pe_resource_walk *walk, uint32_t relative) {
    size_t index;
    if (!walk) return true;
    for (index = 0U; index < walk->visited_count; ++index)
        if (walk->visited[index] == relative) return true;
    if (walk->visited_count >= PE_DATA_MAX_RESOURCE_NODES) return true;
    walk->visited[walk->visited_count++] = relative;
    return false;
}

static bool pe_append_resource_dir(xx_pe *pe, pe_data_stream *stream,
                                   const xx_memory_map *map,
                                   pe_resource_walk *walk,
                                   uint32_t relative, uint32_t depth,
                                   xx_pd_struct *pd) {
    int64_t offset;
    uint32_t entries;
    uint64_t entries_size;
    uint32_t index;
    if (!walk || depth > PE_DATA_MAX_RESOURCE_DEPTH ||
        pe_resource_seen(walk, relative) ||
        relative > walk->directory_size ||
        walk->directory_size - relative < 16U ||
        walk->root_rva > UINT32_MAX - relative ||
        !pe_rva_range(pe, map, (uint64_t)walk->root_rva + relative,
                      16U, &offset))
        return true;
    entries = (uint32_t)xx_io_get_u16(pe->format.device, offset + 12, false) +
              xx_io_get_u16(pe->format.device, offset + 14, false);
    if (entries > PE_DATA_MAX_TABLE_ENTRIES ||
        !pe_u64_product(entries, 8U, &entries_size) ||
        entries_size > walk->directory_size - relative - 16U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, walk->directory_size - relative);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_RESOURCE_DIRECTORY,
                            offset, 16U, 16U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        (entries && !pe_append_absolute(
                        pe, stream,
                        XX_PE_DATA_STRUCT_RESOURCE_DIRECTORY_ENTRY,
                        offset + 16, 8U, entries_size, entries,
                        XX_DATA_STRUCT_TYPE_ENTRY)))
        return false;
    for (index = 0U; index < entries; ++index) {
        int64_t entry_offset = offset + 16 + (int64_t)index * 8;
        uint32_t name = pe_u32(pe, entry_offset);
        uint32_t child = pe_u32(pe, entry_offset + 4);
        if (xx_pd_is_stopped(pd)) return false;
        if (name & UINT32_C(0x80000000)) {
            uint32_t name_relative = name & UINT32_C(0x7fffffff);
            int64_t name_offset;
            if (name_relative <= walk->directory_size - 2U &&
                pe_rva_range(pe, map,
                             (uint64_t)walk->root_rva + name_relative,
                             2U, &name_offset)) {
                uint16_t characters = xx_io_get_u16(pe->format.device,
                                                     name_offset, false);
                uint64_t name_size = 2U + (uint64_t)characters * 2U;
                if (name_size <= walk->directory_size - name_relative &&
                    pe_rva_range(pe, map,
                                 (uint64_t)walk->root_rva + name_relative,
                                 name_size, NULL) &&
                    !pe_append_absolute(pe, stream,
                                        XX_PE_DATA_STRUCT_RESOURCE_STRING,
                                        name_offset, name_size, name_size,
                                        1U, XX_DATA_STRUCT_TYPE_STRUCT))
                    return false;
            }
        }
        if (child & UINT32_C(0x80000000)) {
            if (!pe_append_resource_dir(pe, stream, map, walk,
                                        child & UINT32_C(0x7fffffff),
                                        depth + 1U, pd))
                return false;
        } else if (child <= walk->directory_size - 16U &&
                   walk->root_rva <= UINT32_MAX - child) {
            int64_t data_offset;
            if (pe_rva_range(pe, map,
                             (uint64_t)walk->root_rva + child,
                             16U, &data_offset)) {
                uint32_t data_rva = pe_u32(pe, data_offset);
                uint32_t data_size = pe_u32(pe, data_offset + 4);
                if (!pe_append_absolute(pe, stream,
                                        XX_PE_DATA_STRUCT_RESOURCE_DATA_ENTRY,
                                        data_offset, 16U, 16U, 1U,
                                        XX_DATA_STRUCT_TYPE_ENTRY) ||
                    (data_size &&
                     pe_rva_range(pe, map, data_rva, data_size, NULL) &&
                     !pe_append_raw_rva(pe, stream, map,
                                        XX_PE_DATA_STRUCT_RESOURCE_PAYLOAD,
                                        data_rva, data_size)))
                    return false;
            }
        }
    }
    return true;
}

static bool pe_append_resources(xx_pe *pe, pe_data_stream *stream,
                                const xx_memory_map *map, xx_pd_struct *pd) {
    pe_resource_walk *walk;
    uint32_t rva = pe->data_directory_rva[PE_DIR_RESOURCE];
    uint32_t size = pe->data_directory_size[PE_DIR_RESOURCE];
    bool result;
    if (!rva || !size) return true;
    {
        int64_t resource_offset;
        if (!pe_rva_range(pe, map, rva, size, &resource_offset)) return true;
        if (size < 16U)
            return pe_append_raw_absolute(
                pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                resource_offset, size);
    }
    walk = (pe_resource_walk *)xx_mem_calloc(1U, sizeof(*walk));
    if (!walk) return false;
    walk->root_rva = rva;
    walk->directory_size = size;
    result = pe_append_resource_dir(pe, stream, map, walk, 0U, 0U, pd);
    xx_mem_free(walk);
    return result;
}

static bool pe_append_x64_unwind(xx_pe *pe, pe_data_stream *stream,
                                 const xx_memory_map *map, uint32_t rva) {
    int64_t offset;
    uint8_t version_flags;
    uint8_t codes;
    uint64_t code_bytes;
    uint64_t tail_rva;
    if (!rva || !pe_rva_range(pe, map, rva, 4U, &offset)) return true;
    version_flags = xx_io_get_u8(pe->format.device, offset);
    if ((version_flags & 7U) == 0U) return true;
    codes = xx_io_get_u8(pe->format.device, offset + 2);
    code_bytes = (uint64_t)codes * 2U;
    if (!pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_UNWIND_INFO,
                            offset, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (!pe_rva_range(pe, map, rva, 4U + code_bytes, NULL)) return true;
    if (codes && !pe_append_rva(pe, stream, map,
                                XX_PE_DATA_STRUCT_UNWIND_CODE,
                                (uint64_t)rva + 4U, 2U, code_bytes,
                                codes, XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    tail_rva = (uint64_t)rva + 4U + ((uint64_t)(codes + 1U) & ~UINT64_C(1)) * 2U;
    if ((version_flags >> 3U) & 4U) {
        if (!pe_rva_range(pe, map, tail_rva, 12U, NULL)) return true;
        return pe_append_rva(pe, stream, map,
                             XX_PE_DATA_STRUCT_UNWIND_CHAINED_FUNCTION,
                             tail_rva, 12U, 12U, 1U,
                             XX_DATA_STRUCT_TYPE_STRUCT);
    }
    if ((version_flags >> 3U) & 3U) {
        if (!pe_rva_range(pe, map, tail_rva, 4U, NULL)) return true;
        return pe_append_rva(pe, stream, map,
                             XX_PE_DATA_STRUCT_UNWIND_HANDLER,
                             tail_rva, 4U, 4U, 1U,
                             XX_DATA_STRUCT_TYPE_STRUCT);
    }
    return true;
}

static bool pe_append_arm_xdata(xx_pe *pe, pe_data_stream *stream,
                                const xx_memory_map *map, uint32_t rva,
                                bool arm64) {
    int64_t offset;
    uint32_t header;
    uint32_t epilogs;
    uint32_t code_words;
    uint64_t cursor = 4U;
    bool has_handler;
    bool single_epilogue;
    uint32_t header_id = arm64 ? XX_PE_DATA_STRUCT_ARM64_XDATA_HEADER
                               : XX_PE_DATA_STRUCT_ARM32_XDATA_HEADER;
    uint32_t extended_id = arm64 ? XX_PE_DATA_STRUCT_ARM64_XDATA_EXTENDED
                                 : XX_PE_DATA_STRUCT_ARM32_XDATA_EXTENDED;
    uint32_t scope_id = arm64 ? XX_PE_DATA_STRUCT_ARM64_EPILOG_SCOPE
                              : XX_PE_DATA_STRUCT_ARM32_EPILOG_SCOPE;
    if (!rva || !pe_rva_range(pe, map, rva, 4U, &offset)) return true;
    header = pe_u32(pe, offset);
    if (((header >> 18U) & 3U) != 0U) return true;
    has_handler = ((header >> 20U) & 1U) != 0U;
    single_epilogue = ((header >> 21U) & 1U) != 0U;
    if (arm64) {
        epilogs = (header >> 22U) & 0x1fU;
        code_words = (header >> 27U) & 0x1fU;
    } else {
        epilogs = (header >> 23U) & 0x1fU;
        code_words = (header >> 28U) & 0x0fU;
    }
    if (!pe_append_absolute(pe, stream, header_id, offset, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (epilogs == 0U && code_words == 0U) {
        int64_t extended_offset;
        uint32_t extended;
        if (!pe_rva_range(pe, map, (uint64_t)rva + cursor, 4U,
                          &extended_offset))
            return true;
        extended = pe_u32(pe, extended_offset);
        epilogs = extended & UINT32_C(0xffff);
        code_words = (extended >> 16U) & UINT32_C(0xff);
        if (!pe_append_absolute(pe, stream, extended_id, extended_offset,
                                4U, 4U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        cursor += 4U;
    }
    if (!single_epilogue && epilogs) {
        uint64_t scope_bytes;
        if (epilogs > PE_DATA_MAX_LINKED_ENTRIES ||
            !pe_u64_product(epilogs, 4U, &scope_bytes) ||
            !pe_rva_range(pe, map, (uint64_t)rva + cursor,
                          scope_bytes, NULL))
            return true;
        if (!pe_append_rva(pe, stream, map, scope_id,
                           (uint64_t)rva + cursor, 4U, scope_bytes,
                           epilogs, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        cursor += scope_bytes;
    }
    if (code_words) {
        uint64_t code_bytes;
        if (!pe_u64_product(code_words, 4U, &code_bytes) ||
            !pe_rva_range(pe, map, (uint64_t)rva + cursor,
                          code_bytes, NULL))
            return true;
        if (!pe_append_raw_rva(pe, stream, map,
                               XX_DATA_STRUCT_ID_RAW_DATA,
                               (uint64_t)rva + cursor, code_bytes))
            return false;
        cursor += code_bytes;
    }
    if (has_handler &&
        pe_rva_range(pe, map, (uint64_t)rva + cursor, 4U, NULL) &&
        !pe_append_rva(pe, stream, map, XX_PE_DATA_STRUCT_UNWIND_HANDLER,
                       (uint64_t)rva + cursor, 4U, 4U, 1U,
                       XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    return true;
}

static bool pe_exception_kind(uint16_t machine, uint32_t *id,
                              uint64_t *entry_size) {
    if (!id || !entry_size) return false;
    switch (machine) {
        case PE_MACHINE_AMD64:
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_X64;
            *entry_size = 12U;
            return true;
        case PE_MACHINE_IA64:
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_IA64;
            *entry_size = 12U;
            return true;
        case PE_MACHINE_ARM64:
        case UINT16_C(0xa641):
        case UINT16_C(0xa64e):
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM64;
            *entry_size = 8U;
            return true;
        case PE_MACHINE_ARM:
        case PE_MACHINE_THUMB:
        case PE_MACHINE_ARMNT:
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM;
            *entry_size = 8U;
            return true;
        case PE_MACHINE_R3000:
        case PE_MACHINE_R4000:
        case PE_MACHINE_R10000:
        case PE_MACHINE_WCEMIPSV2:
        case UINT16_C(0x0266):
        case UINT16_C(0x0366):
        case UINT16_C(0x0466):
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_MIPS;
            *entry_size = 20U;
            return true;
        case PE_MACHINE_ALPHA:
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ALPHA32;
            *entry_size = 20U;
            return true;
        case PE_MACHINE_ALPHA64:
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ALPHA64;
            *entry_size = 40U;
            return true;
        case UINT16_C(0x01f0):
        case UINT16_C(0x01f1):
        case UINT16_C(0x01a2):
        case UINT16_C(0x01a3):
        case UINT16_C(0x01a6):
        case UINT16_C(0x01a8):
            *id = XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_CE;
            *entry_size = 8U;
            return true;
        default:
            return false;
    }
}

static bool pe_append_exception(xx_pe *pe, pe_data_stream *stream,
                                const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_EXCEPTION];
    uint32_t size = pe->data_directory_size[PE_DIR_EXCEPTION];
    uint32_t id;
    uint64_t entry_size;
    uint64_t count;
    uint64_t covered;
    int64_t offset;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (!pe_exception_kind(pe->machine, &id, &entry_size))
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    count = size / entry_size;
    covered = count * entry_size;
    if (count &&
        !pe_append_absolute(pe, stream, id, offset, entry_size, covered,
                            count, XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < count; ++index) {
        int64_t current = offset + (int64_t)(index * entry_size);
        uint32_t unwind;
        if (xx_pd_is_stopped(pd)) return false;
        if (id == XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_X64) {
            unwind = pe_u32(pe, current + 8);
            if (!pe_append_x64_unwind(pe, stream, map, unwind)) return false;
        } else if (id == XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM ||
                   id == XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM64) {
            unwind = pe_u32(pe, current + 4);
            if ((unwind & 3U) == 0U &&
                !pe_append_arm_xdata(pe, stream, map, unwind & ~UINT32_C(3),
                                     id == XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM64))
                return false;
        }
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)covered, size - covered);
}

static bool pe_append_security(xx_pe *pe, pe_data_stream *stream,
                               xx_pd_struct *pd) {
    uint32_t relative = pe->data_directory_rva[PE_DIR_SECURITY];
    uint32_t size = pe->data_directory_size[PE_DIR_SECURITY];
    int64_t offset;
    uint64_t cursor = 0U;
    if (!relative || !size) return true;
    if (!pe_absolute_range(pe, relative, size, &offset)) return true;
    while (cursor < size) {
        uint32_t length;
        uint64_t next;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) {
            return pe_append_raw_absolute(
                pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                offset + (int64_t)cursor, size - cursor);
        }
        length = pe_u32(pe, offset + (int64_t)cursor);
        if (length < 8U || length > size - cursor ||
            !pe_align_up((uint64_t)length, 8U, &next) ||
            next > size - cursor) {
            return pe_append_raw_absolute(
                pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                offset + (int64_t)cursor, size - cursor);
        }
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_WIN_CERTIFICATE,
                                offset + (int64_t)cursor, 8U, length, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT) ||
            !pe_append_raw_absolute(pe, stream,
                                    XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                                    offset + (int64_t)cursor + 8,
                                    length - 8U) ||
            !pe_append_raw_absolute(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                    offset + (int64_t)cursor + length,
                                    next - length))
            return false;
        cursor += next;
    }
    return true;
}

static bool pe_append_base_relocations(xx_pe *pe, pe_data_stream *stream,
                                       const xx_memory_map *map,
                                       xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_BASERELOC];
    uint32_t size = pe->data_directory_size[PE_DIR_BASERELOC];
    int64_t offset;
    uint64_t cursor = 0U;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    while (cursor < size) {
        uint32_t block_size;
        uint64_t entries;
        uint64_t entries_size;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) break;
        block_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if (block_size < 8U || block_size > size - cursor) break;
        entries_size = block_size - 8U;
        entries = entries_size / 2U;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_BASE_RELOCATION_BLOCK,
                                offset + (int64_t)cursor, 8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT) ||
            (entries && !pe_append_absolute(
                            pe, stream,
                            XX_PE_DATA_STRUCT_BASE_RELOCATION_ENTRY,
                            offset + (int64_t)cursor + 8, 2U,
                            entries * 2U, entries,
                            XX_DATA_STRUCT_TYPE_ENTRY)) ||
            !pe_append_raw_absolute(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                    offset + (int64_t)cursor + 8 +
                                        (int64_t)(entries * 2U),
                                    entries_size - entries * 2U))
            return false;
        cursor += block_size;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_debug_data_offset(xx_pe *pe, const xx_memory_map *map,
                                 int64_t directory_entry,
                                 uint32_t data_size, int64_t *result) {
    uint32_t pointer = pe_u32(pe, directory_entry + 24);
    uint32_t rva = pe_u32(pe, directory_entry + 20);
    if (!data_size || !result) return false;
    if (pointer && pe_absolute_range(pe, pointer, data_size, result))
        return true;
    return rva && pe_rva_range(pe, map, rva, data_size, result);
}

static bool pe_append_pogo(xx_pe *pe, pe_data_stream *stream,
                           int64_t offset, uint32_t size) {
    uint64_t cursor = 4U;
    if (size < 4U)
        return pe_append_raw_absolute(pe, stream,
                                      XX_PE_DATA_STRUCT_DEBUG_RAW,
                                      offset, size);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_DEBUG_POGO_HEADER,
                            offset, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    while (cursor < size) {
        uint64_t name_size;
        uint64_t next;
        if (size - cursor < 9U) break;
        name_size = pe_cstring_size(pe, offset + (int64_t)cursor + 8,
                                    size - cursor - 8U);
        if (!name_size ||
            !pe_align_up(cursor + 8U + name_size, 4U, &next) || next > size)
            break;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_DEBUG_POGO_ENTRY,
                                offset + (int64_t)cursor, 8U, next - cursor,
                                1U, XX_DATA_STRUCT_TYPE_ENTRY) ||
            !pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_ASCII_STRING,
                                offset + (int64_t)cursor + 8, 1U,
                                name_size, name_size,
                                XX_DATA_STRUCT_TYPE_RAW_DATA))
            return false;
        cursor = next;
    }
    return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                                  offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_debug_payload(xx_pe *pe, pe_data_stream *stream,
                                    int64_t offset, uint32_t size,
                                    uint32_t type) {
    uint32_t signature;
    uint64_t length;
    if (!size) return true;
    signature = size >= 4U ? pe_u32(pe, offset) : 0U;
    switch (type) {
        case 1U:
            if (size >= 32U)
                return pe_append_absolute(
                    pe, stream,
                    XX_PE_DATA_STRUCT_DEBUG_COFF_SYMBOLS_HEADER,
                    offset, 32U, 32U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 32, size - 32U);
            break;
        case 2U:
            if (signature == UINT32_C(0x53445352) && size >= 24U) {
                length = pe_cstring_size(pe, offset + 24, size - 24U);
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_CODEVIEW_RSDS,
                           offset, 24U, 24U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       (length ? (pe_append_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_ASCII_STRING,
                                      offset + 24, 1U, length, length,
                                      XX_DATA_STRUCT_TYPE_RAW_DATA) &&
                                  pe_append_raw_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_DEBUG_RAW,
                                      offset + 24 + (int64_t)length,
                                      size - 24U - length))
                               : pe_append_raw_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_DEBUG_RAW,
                                      offset + 24, size - 24U));
            }
            if (signature == UINT32_C(0x3031424e) && size >= 16U) {
                length = pe_cstring_size(pe, offset + 16, size - 16U);
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_CODEVIEW_NB10,
                           offset, 16U, 16U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       (length ? (pe_append_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_ASCII_STRING,
                                      offset + 16, 1U, length, length,
                                      XX_DATA_STRUCT_TYPE_RAW_DATA) &&
                                  pe_append_raw_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_DEBUG_RAW,
                                      offset + 16 + (int64_t)length,
                                      size - 16U - length))
                               : pe_append_raw_absolute(
                                      pe, stream,
                                      XX_PE_DATA_STRUCT_DEBUG_RAW,
                                      offset + 16, size - 16U));
            }
            break;
        case 3U:
            if (size >= 16U)
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_FPO,
                           offset, 16U, size - size % 16U, size / 16U,
                           XX_DATA_STRUCT_TYPE_ENTRY) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + size - size % 16U, size % 16U);
            break;
        case 4U:
            if (size >= 12U)
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_MISC,
                           offset, 12U, 12U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 12, size - 12U);
            break;
        case 5U: {
            uint64_t stride = pe->optional_magic == XX_PE_MAGIC_64 ? 24U : 12U;
            uint64_t count = size / stride;
            uint64_t covered = count * stride;
            if (count)
                return pe_append_absolute(
                           pe, stream,
                           stride == 24U
                               ? XX_PE_DATA_STRUCT_DEBUG_FUNCTION_ENTRY64
                               : XX_PE_DATA_STRUCT_DEBUG_FUNCTION_ENTRY32,
                           offset, stride, covered, count,
                           XX_DATA_STRUCT_TYPE_ENTRY) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + (int64_t)covered, size - covered);
            break;
        }
        case 7U:
        case 8U:
            if (size >= 8U)
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_OMAP,
                           offset, 8U, size - size % 8U, size / 8U,
                           XX_DATA_STRUCT_TYPE_ENTRY) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + size - size % 8U, size % 8U);
            break;
        case 11U:
            if (size >= 16U)
                return pe_append_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_GUID,
                           offset, 16U, 16U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 16, size - 16U);
            break;
        case 12U:
            if (size >= 20U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DEBUG_VC_FEATURE,
                           offset, 20U, 20U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 20, size - 20U);
            break;
        case 13U:
            return pe_append_pogo(pe, stream, offset, size);
        case 16U:
            if (size == 0U) return true;
            if (size >= 4U && pe_u32(pe, offset) <= size - 4U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DEBUG_REPRO_HASH_HEADER,
                           offset, 4U, 4U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 4, size - 4U);
            break;
        case 17U:
            if (signature == UINT32_C(0x4244504d) && size >= 8U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DEBUG_EMBEDDED_PORTABLE_PDB,
                           offset, 8U, 8U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 8, size - 8U);
            break;
        case 19U:
            length = pe_cstring_size(pe, offset, size);
            if (length && length < size)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DEBUG_PDB_CHECKSUM,
                           offset, 1U, length, length,
                           XX_DATA_STRUCT_TYPE_RAW_DATA) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + (int64_t)length, size - length);
            break;
        case 20U:
            if (size >= 4U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DEBUG_EX_DLL_CHARACTERISTICS,
                           offset, 4U, 4U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                           offset + 4, size - 4U);
            break;
        case 21U:
            if (size >= 25U && signature == UINT32_C(0x4d523252)) {
                length = pe_cstring_size(pe, offset + 24, size - 24U);
                if (length)
                    return pe_append_absolute(
                               pe, stream,
                               XX_PE_DATA_STRUCT_DEBUG_R2R_PERFMAP,
                               offset, 24U, 24U, 1U,
                               XX_DATA_STRUCT_TYPE_STRUCT) &&
                           pe_append_absolute(
                               pe, stream, XX_PE_DATA_STRUCT_ASCII_STRING,
                               offset + 24, 1U, length, length,
                               XX_DATA_STRUCT_TYPE_RAW_DATA) &&
                           pe_append_raw_absolute(
                               pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                               offset + 24 + (int64_t)length,
                               size - 24U - length);
            }
            break;
        default:
            break;
    }
    return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_DEBUG_RAW,
                                  offset, size);
}

static bool pe_append_debug(xx_pe *pe, pe_data_stream *stream,
                            const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_DEBUG];
    uint32_t size = pe->data_directory_size[PE_DIR_DEBUG];
    int64_t offset;
    uint64_t count;
    uint64_t covered;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    count = size / 28U;
    covered = count * 28U;
    if (count &&
        !pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_DEBUG_DIRECTORY,
                            offset, 28U, covered, count,
                            XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < count; ++index) {
        int64_t entry = offset + (int64_t)(index * 28U);
        int64_t data_offset;
        uint32_t type = pe_u32(pe, entry + 12);
        uint32_t data_size = pe_u32(pe, entry + 16);
        if (xx_pd_is_stopped(pd)) return false;
        if (data_size && pe_debug_data_offset(pe, map, entry, data_size,
                                              &data_offset) &&
            !pe_append_debug_payload(pe, stream, data_offset, data_size,
                                     type))
            return false;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)covered, size - covered);
}

static bool pe_append_architecture(xx_pe *pe, pe_data_stream *stream,
                                    const xx_memory_map *map,
                                    xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_ARCHITECTURE];
    uint32_t size = pe->data_directory_size[PE_DIR_ARCHITECTURE];
    int64_t offset;
    uint64_t header_end = 0U;
    uint64_t cursor;
    bool terminated = false;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (size < 8U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);

    /* Both the header list and every pointed-to entry list use an all-ones
       qword terminator.  Validate the header envelope before assigning any
       structure types, so a malformed list cannot consume the rest of the
       containing image as apparent headers. */
    for (cursor = 0U; cursor + 8U <= size; cursor += 8U) {
        if (xx_pd_is_stopped(pd)) return false;
        if (xx_io_get_u64(pe->format.device, offset + (int64_t)cursor,
                          false) == UINT64_MAX) {
            header_end = cursor;
            terminated = true;
            break;
        }
    }
    if (!terminated)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);

    for (cursor = 0U; cursor < header_end; cursor += 8U) {
        uint64_t header = xx_io_get_u64(
            pe->format.device, offset + (int64_t)cursor, false);
        uint32_t first_entry;
        uint64_t entry_relative;
        uint64_t entry_end = 0U;
        bool entry_terminated = false;
        if (xx_pd_is_stopped(pd)) return false;
        if (header == 0U) continue;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_ARCHITECTURE_HEADER,
                                offset + (int64_t)cursor,
                                8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        first_entry = (uint32_t)(header >> 32U);
        if (first_entry < rva ||
            (uint64_t)first_entry + 8U > (uint64_t)rva + size)
            continue;
        entry_relative = (uint64_t)first_entry - rva;
        for (entry_end = entry_relative;
             entry_end + 8U <= size &&
             (entry_end - entry_relative) / 8U <
                 PE_DATA_MAX_LINKED_ENTRIES;
             entry_end += 8U) {
            if (xx_pd_is_stopped(pd)) return false;
            if (xx_io_get_u64(pe->format.device,
                              offset + (int64_t)entry_end,
                              false) == UINT64_MAX) {
                entry_terminated = true;
                break;
            }
        }
        if (!entry_terminated) continue;
        for (uint64_t entry_cursor = entry_relative;
             entry_cursor < entry_end; entry_cursor += 8U) {
            uint64_t entry = xx_io_get_u64(
                pe->format.device, offset + (int64_t)entry_cursor, false);
            if (xx_pd_is_stopped(pd)) return false;
            if (entry != 0U &&
                !pe_append_absolute(pe, stream,
                                    XX_PE_DATA_STRUCT_ARCHITECTURE_ENTRY,
                                    offset + (int64_t)entry_cursor,
                                    8U, 8U, 1U,
                                    XX_DATA_STRUCT_TYPE_ENTRY))
                return false;
        }
    }
    return pe_append_raw_absolute(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                  offset + size - size % 8U,
                                  size % 8U);
}

static bool pe_append_tls(xx_pe *pe, pe_data_stream *stream,
                          const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_TLS];
    uint32_t size = pe->data_directory_size[PE_DIR_TLS];
    uint64_t fixed = pe->optional_magic == XX_PE_MAGIC_64 ? 40U : 24U;
    uint64_t stride = pe->optional_magic == XX_PE_MAGIC_64 ? 8U : 4U;
    int64_t offset;
    uint64_t start;
    uint64_t end;
    uint64_t callbacks;
    uint64_t count = 0U;
    int64_t callback_offset;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    if (size < fixed)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    if (!pe_append_absolute(
            pe, stream,
            fixed == 40U ? XX_PE_DATA_STRUCT_TLS_DIRECTORY64
                         : XX_PE_DATA_STRUCT_TLS_DIRECTORY32,
            offset, fixed, fixed, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        !pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset + (int64_t)fixed, size - fixed))
        return false;
    start = stride == 8U ? xx_io_get_u64(pe->format.device, offset, false)
                         : pe_u32(pe, offset);
    end = stride == 8U ? xx_io_get_u64(pe->format.device, offset + 8, false)
                       : pe_u32(pe, offset + 4);
    callbacks = stride == 8U
                    ? xx_io_get_u64(pe->format.device, offset + 24, false)
                    : pe_u32(pe, offset + 12);
    if (end > start && end - start <= INT64_MAX) {
        int64_t template_offset;
        if (pe_va_range(pe, map, start, end - start, &template_offset) &&
            !pe_append_raw_absolute(pe, stream,
                                    XX_PE_DATA_STRUCT_TLS_TEMPLATE,
                                    template_offset, end - start))
            return false;
    }
    if (!callbacks ||
        !pe_va_range(pe, map, callbacks, stride, &callback_offset))
        return true;
    while (count < PE_DATA_MAX_LINKED_ENTRIES) {
        int64_t current;
        uint64_t value;
        if (xx_pd_is_stopped(pd) ||
            !pe_va_range(pe, map, callbacks + count * stride, stride,
                         &current))
            break;
        value = stride == 8U
                    ? xx_io_get_u64(pe->format.device, current, false)
                    : pe_u32(pe, current);
        if (!value) break;
        ++count;
    }
    return !count || pe_append_absolute(
        pe, stream,
        stride == 8U ? XX_PE_DATA_STRUCT_TLS_CALLBACK64
                     : XX_PE_DATA_STRUCT_TLS_CALLBACK32,
        callback_offset, stride, count * stride, count,
        XX_DATA_STRUCT_TYPE_ENTRY);
}

static bool pe_append_va_table(xx_pe *pe, pe_data_stream *stream,
                               const xx_memory_map *map, uint32_t id,
                               uint64_t va, uint64_t count,
                               uint64_t stride) {
    int64_t offset;
    uint64_t bytes;
    if (!va || !count) return true;
    if (count > PE_DATA_MAX_TABLE_ENTRIES ||
        !pe_u64_product(count, stride, &bytes) ||
        !pe_va_range(pe, map, va, bytes, &offset))
        return true;
    return pe_append_absolute(pe, stream, id, offset, stride, bytes, count,
                              XX_DATA_STRUCT_TYPE_ENTRY);
}

static bool pe_append_rva_table(xx_pe *pe, pe_data_stream *stream,
                                const xx_memory_map *map, uint32_t id,
                                uint64_t rva, uint64_t count,
                                uint64_t stride) {
    int64_t offset;
    uint64_t bytes;
    if (!rva || !count) return true;
    if (count > PE_DATA_MAX_TABLE_ENTRIES ||
        !pe_u64_product(count, stride, &bytes) ||
        !pe_rva_range(pe, map, rva, bytes, &offset))
        return true;
    return pe_append_absolute(pe, stream, id, offset, stride, bytes, count,
                              XX_DATA_STRUCT_TYPE_ENTRY);
}

static bool pe_append_arm64x_fixups(xx_pe *pe, pe_data_stream *stream,
                                    int64_t offset, uint64_t size,
                                    xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    while (cursor < size) {
        uint32_t page_rva;
        uint32_t block_size;
        uint64_t block_cursor;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) break;
        page_rva = pe_u32(pe, offset + (int64_t)cursor);
        block_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if ((page_rva & UINT32_C(0xfff)) != 0U || block_size <= 8U ||
            (block_size & 3U) != 0U || block_size > size - cursor)
            break;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_BASE_RELOCATION_BLOCK,
                                offset + (int64_t)cursor, 8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        block_cursor = 8U;
        while (block_cursor < block_size) {
            int64_t word_offset = offset + (int64_t)cursor +
                                  (int64_t)block_cursor;
            uint16_t word;
            uint16_t type;
            uint16_t arg;
            uint64_t value_size;
            if (block_size - block_cursor < 2U) break;
            word = xx_io_get_u16(pe->format.device, word_offset, false);
            /* A zero ARM64X word is block-alignment padding, not a
               ZEROFILL relocation. */
            if (word == 0U) break;
            type = (word >> 12U) & 3U;
            arg = word >> 14U;
            if (type == 0U) {
                if (!pe_append_absolute(
                        pe, stream, XX_PE_DATA_STRUCT_ARM64X_FIXUP,
                        word_offset, 2U, 2U, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY))
                    return false;
                block_cursor += 2U;
            } else if (type == 1U && arg != 0U) {
                value_size = UINT64_C(1) << arg;
                if (value_size > block_size - block_cursor - 2U) break;
                if (!pe_append_absolute(
                        pe, stream, XX_PE_DATA_STRUCT_ARM64X_FIXUP,
                        word_offset, 2U, 2U, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY) ||
                    !pe_append_raw_absolute(
                        pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                        word_offset + 2, value_size))
                    return false;
                block_cursor += 2U + value_size;
            } else if (type == 2U) {
                if (block_size - block_cursor < 4U ||
                    !pe_append_absolute(
                        pe, stream, XX_PE_DATA_STRUCT_ARM64X_DELTA,
                        word_offset, 4U, 4U, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY))
                    break;
                block_cursor += 4U;
            } else {
                break;
            }
        }
        if (!pe_append_raw_absolute(
                pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                offset + (int64_t)cursor + (int64_t)block_cursor,
                block_size - block_cursor))
            return false;
        cursor += block_size;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_plain_base_reloc_blocks(xx_pe *pe,
                                              pe_data_stream *stream,
                                              int64_t offset,
                                              uint64_t size,
                                              xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    while (cursor < size) {
        uint32_t block_size;
        uint64_t entries;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) break;
        block_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if (block_size < 8U || block_size > size - cursor) break;
        entries = (block_size - 8U) / 2U;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_BASE_RELOCATION_BLOCK,
                                offset + (int64_t)cursor, 8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT) ||
            (entries && !pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_BASE_RELOCATION_ENTRY,
                offset + (int64_t)cursor + 8, 2U, entries * 2U, entries,
                XX_DATA_STRUCT_TYPE_ENTRY)))
            return false;
        cursor += block_size;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_grouped_dynamic_entries(
    xx_pe *pe, pe_data_stream *stream, int64_t offset, uint64_t size,
    uint32_t entry_id, uint64_t stride, xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    while (cursor < size) {
        uint32_t block_size;
        uint64_t body;
        uint64_t count;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) break;
        block_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if (block_size < 8U || block_size > size - cursor) break;
        body = block_size - 8U;
        count = body / stride;
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_BASE_RELOCATION_BLOCK,
                                offset + (int64_t)cursor, 8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT) ||
            (count && !pe_append_absolute(
                pe, stream, entry_id, offset + (int64_t)cursor + 8,
                stride, count * stride, count,
                XX_DATA_STRUCT_TYPE_ENTRY)) ||
            !pe_append_raw_absolute(
                pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                offset + (int64_t)cursor + 8 +
                    (int64_t)(count * stride),
                body - count * stride))
            return false;
        cursor += block_size;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_function_override(xx_pe *pe,
                                        pe_data_stream *stream,
                                        int64_t offset, uint64_t size,
                                        xx_pd_struct *pd) {
    uint32_t override_size;
    uint64_t cursor;
    if (size < 4U) return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, offset, size);
    override_size = pe_u32(pe, offset);
    if (override_size > size - 4U)
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, size);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_FUNCTION_OVERRIDE_HEADER,
                            offset, 4U, 4U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    cursor = 4U;
    while (cursor < 4U + override_size) {
        uint32_t rva_size;
        uint32_t reloc_size;
        uint64_t total;
        if (xx_pd_is_stopped(pd)) return false;
        if (4U + override_size - cursor < 16U) break;
        rva_size = pe_u32(pe, offset + (int64_t)cursor + 8);
        reloc_size = pe_u32(pe, offset + (int64_t)cursor + 12);
        if (!pe_u64_add(16U, rva_size, &total) ||
            !pe_u64_add(total, reloc_size, &total) ||
            total > 4U + override_size - cursor)
            break;
        if (!pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_FUNCTION_OVERRIDE_ENTRY,
                offset + (int64_t)cursor, 16U, 16U, 1U,
                XX_DATA_STRUCT_TYPE_ENTRY) ||
            !pe_append_raw_absolute(
                pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                offset + (int64_t)cursor + 16, total - 16U))
            return false;
        cursor += total;
    }
    if (!pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset + (int64_t)cursor, 4U + override_size - cursor))
        return false;
    cursor = 4U + override_size;
    if (size - cursor >= 8U) {
        uint32_t bdd_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        uint64_t node_bytes;
        if (bdd_size <= size - cursor - 8U &&
            !pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_BDD_INFO,
                                offset + (int64_t)cursor, 8U, 8U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        node_bytes = bdd_size - bdd_size % 8U;
        if (bdd_size <= size - cursor - 8U && node_bytes &&
            !pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_BDD_NODE,
                                offset + (int64_t)cursor + 8, 8U,
                                node_bytes, node_bytes / 8U,
                                XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        if (bdd_size <= size - cursor - 8U) cursor += 8U + bdd_size;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_dynamic_fixup_payload(xx_pe *pe,
                                            pe_data_stream *stream,
                                            uint64_t symbol,
                                            int64_t offset, uint64_t size,
                                            xx_pd_struct *pd) {
    switch (symbol) {
        case 1U:
            if (size >= 1U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_PROLOGUE,
                           offset, 1U, 1U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                           offset + 1, size - 1U);
            break;
        case 2U:
            if (size >= 8U)
                return pe_append_absolute(
                           pe, stream,
                           XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_EPILOGUE,
                           offset, 8U, 8U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT) &&
                       pe_append_raw_absolute(
                           pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                           offset + 8, size - 8U);
            break;
        case 3U:
            return pe_append_grouped_dynamic_entries(
                pe, stream, offset, size,
                XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_IMPORT_CONTROL,
                4U, pd);
        case 4U:
            return pe_append_grouped_dynamic_entries(
                pe, stream, offset, size,
                XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_INDIR, 2U, pd);
        case 5U:
            return pe_append_grouped_dynamic_entries(
                pe, stream, offset, size,
                XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_SWITCHTABLE,
                2U, pd);
        case 6U:
            return pe_append_arm64x_fixups(pe, stream, offset, size, pd);
        case 7U:
            return pe_append_function_override(pe, stream, offset, size, pd);
        case 8U:
            return pe_append_grouped_dynamic_entries(
                pe, stream, offset, size,
                XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_ARM64_IMPORT_CONTROL,
                4U, pd);
        default:
            return pe_append_plain_base_reloc_blocks(pe, stream, offset,
                                                      size, pd);
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, offset, size);
}

static bool pe_append_dynamic_relocations_at(xx_pe *pe,
                                             pe_data_stream *stream,
                                             int64_t offset,
                                             xx_pd_struct *pd) {
    uint32_t version;
    uint32_t size;
    uint64_t cursor = 0U;
    if (!pe_device_range(pe, offset, 8U)) return true;
    version = pe_u32(pe, offset);
    size = pe_u32(pe, offset + 4);
    if (size > INT64_MAX - 8 ||
        !pe_device_range(pe, offset, (uint64_t)size + 8U))
        return true;
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_TABLE,
                            offset, 8U, 8U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    while (cursor < size) {
        uint64_t symbol;
        uint32_t payload_size;
        uint64_t header_size;
        uint64_t total;
        uint32_t id;
        if (xx_pd_is_stopped(pd)) return false;
        if (version == 1U) {
            header_size = pe->optional_magic == XX_PE_MAGIC_64 ? 12U : 8U;
            if (size - cursor < header_size) break;
            symbol = pe->optional_magic == XX_PE_MAGIC_64
                         ? xx_io_get_u64(pe->format.device,
                                         offset + 8 + (int64_t)cursor, false)
                         : pe_u32(pe, offset + 8 + (int64_t)cursor);
            payload_size = pe_u32(pe, offset + 8 + (int64_t)cursor +
                                      (pe->optional_magic == XX_PE_MAGIC_64
                                           ? 8 : 4));
            id = symbol == 6U
                     ? XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_ARM64X
                     : (pe->optional_magic == XX_PE_MAGIC_64
                            ? XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION64
                            : XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION32);
        } else if (version == 2U) {
            header_size = pe->optional_magic == XX_PE_MAGIC_64 ? 24U : 20U;
            if (size - cursor < header_size) break;
            uint32_t declared_header = pe_u32(pe, offset + 8 + (int64_t)cursor);
            payload_size = pe_u32(pe, offset + 8 + (int64_t)cursor + 4);
            if (declared_header < header_size) break;
            header_size = declared_header;
            if (size - cursor < header_size) break;
            symbol = pe->optional_magic == XX_PE_MAGIC_64
                         ? xx_io_get_u64(pe->format.device,
                                         offset + 8 + (int64_t)cursor + 8, false)
                         : pe_u32(pe, offset + 8 + (int64_t)cursor + 8);
            id = symbol == 6U
                     ? XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_ARM64X
                     : (pe->optional_magic == XX_PE_MAGIC_64
                            ? XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION64_V2
                            : XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION32_V2);
        } else {
            break;
        }
        if (!pe_u64_add(header_size, payload_size, &total) ||
            total > size - cursor)
            break;
        if (!pe_append_absolute(pe, stream, id,
                                offset + 8 + (int64_t)cursor, header_size,
                                header_size, 1U,
                                XX_DATA_STRUCT_TYPE_ENTRY) ||
            !pe_append_dynamic_fixup_payload(
                pe, stream, symbol,
                offset + 8 + (int64_t)cursor + (int64_t)header_size,
                payload_size, pd))
            return false;
        cursor += total;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + 8 + (int64_t)cursor, size - cursor);
}

static bool pe_append_dynamic_relocations(xx_pe *pe,
                                          pe_data_stream *stream,
                                          const xx_memory_map *map,
                                          uint64_t va, xx_pd_struct *pd) {
    int64_t offset;
    if (!va || !pe_va_range(pe, map, va, 8U, &offset)) return true;
    return pe_append_dynamic_relocations_at(pe, stream, offset, pd);
}

static bool pe_load_has(uint64_t size, uint64_t offset, uint64_t width) {
    return offset <= size && width <= size - offset;
}

static uint64_t pe_load_pointer(const xx_pe *pe, int64_t offset) {
    return pe->optional_magic == XX_PE_MAGIC_64
               ? xx_io_get_u64(pe->format.device, offset, false)
               : pe_u32(pe, offset);
}

static bool pe_append_chpe_metadata(xx_pe *pe, pe_data_stream *stream,
                                    const xx_memory_map *map, uint64_t va) {
    int64_t offset;
    uint32_t version;
    uint64_t fixed;
    uint32_t code_map;
    uint32_t code_map_count;
    uint32_t ranges;
    uint32_t range_count;
    uint32_t redirects;
    uint32_t redirect_count;
    uint32_t id;
    if (!va || pe->optional_magic != XX_PE_MAGIC_64 ||
        (pe->machine != PE_MACHINE_AMD64 &&
                pe->machine != PE_MACHINE_ARM64 &&
                pe->machine != UINT16_C(0xa641) &&
                pe->machine != UINT16_C(0xa64e)) ||
        !pe_va_range(pe, map, va, 80U, &offset))
        return true;
    version = pe_u32(pe, offset);
    fixed = version == 2U ? 92U : 80U;
    if (!pe_va_range(pe, map, va, fixed, NULL)) return true;
    id = (pe->machine == PE_MACHINE_AMD64 ||
          pe->machine == UINT16_C(0xa641))
             ? XX_PE_DATA_STRUCT_ARM64EC_METADATA
             : XX_PE_DATA_STRUCT_CHPE_METADATA;
    if (!pe_append_absolute(pe, stream, id, offset, fixed, fixed, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (version != 1U && version != 2U) return true;
    code_map = pe_u32(pe, offset + 4);
    code_map_count = pe_u32(pe, offset + 8);
    ranges = pe_u32(pe, offset + 12);
    redirects = pe_u32(pe, offset + 16);
    range_count = pe_u32(pe, offset + 48);
    redirect_count = pe_u32(pe, offset + 52);
    return pe_append_rva_table(
               pe, stream, map, XX_PE_DATA_STRUCT_CHPE_CODE_RANGE_ENTRY,
               code_map, code_map_count, 8U) &&
           pe_append_rva_table(
               pe, stream, map,
               XX_PE_DATA_STRUCT_CHPE_CODE_RANGE_TO_ENTRY_POINT,
               ranges, range_count, 12U) &&
           pe_append_rva_table(
               pe, stream, map, XX_PE_DATA_STRUCT_CHPE_REDIRECTION_ENTRY,
               redirects, redirect_count, 8U);
}

static bool pe_append_volatile_metadata(xx_pe *pe,
                                        pe_data_stream *stream,
                                        const xx_memory_map *map,
                                        uint64_t va) {
    int64_t offset;
    uint32_t declared;
    uint32_t access_rva;
    uint32_t access_size;
    uint32_t range_rva;
    uint32_t range_size;
    if (!va || !pe_va_range(pe, map, va, 24U, &offset)) return true;
    declared = pe_u32(pe, offset);
    if (declared < 24U || !pe_va_range(pe, map, va, declared, NULL))
        return true;
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_VOLATILE_METADATA,
                            offset, declared, declared, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    access_rva = pe_u32(pe, offset + 8);
    access_size = pe_u32(pe, offset + 12);
    range_rva = pe_u32(pe, offset + 16);
    range_size = pe_u32(pe, offset + 20);
    return pe_append_rva_table(
               pe, stream, map, XX_PE_DATA_STRUCT_VOLATILE_ACCESS,
               access_rva, access_size / 4U, 4U) &&
           pe_append_rva_table(
               pe, stream, map, XX_PE_DATA_STRUCT_VOLATILE_INFO_RANGE,
               range_rva, range_size / 8U, 8U);
}

static bool pe_append_enclave(xx_pe *pe, pe_data_stream *stream,
                              const xx_memory_map *map, uint64_t va) {
    int64_t offset;
    uint64_t minimum = pe->optional_magic == XX_PE_MAGIC_64 ? 76U : 72U;
    uint32_t declared;
    uint32_t count;
    uint32_t list_rva;
    uint32_t stride;
    int64_t imports_offset;
    if (!va || !pe_va_range(pe, map, va, 24U, &offset)) return true;
    declared = pe_u32(pe, offset);
    if (declared < minimum || !pe_va_range(pe, map, va, declared, NULL))
        return true;
    if (!pe_append_absolute(
            pe, stream,
            pe->optional_magic == XX_PE_MAGIC_64
                ? XX_PE_DATA_STRUCT_ENCLAVE_CONFIG64
                : XX_PE_DATA_STRUCT_ENCLAVE_CONFIG32,
            offset, declared, declared, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    count = pe_u32(pe, offset + 12);
    list_rva = pe_u32(pe, offset + 16);
    stride = pe_u32(pe, offset + 20);
    if (stride < 80U || count > PE_DATA_MAX_TABLE_ENTRIES ||
        !pe_rva_range(pe, map, list_rva, (uint64_t)count * stride,
                      &imports_offset))
        return true;
    if (!pe_append_rva_table(pe, stream, map,
                             XX_PE_DATA_STRUCT_ENCLAVE_IMPORT,
                             list_rva, count, stride))
        return false;
    for (uint32_t index = 0U; index < count; ++index) {
        uint32_t name_rva = pe_u32(
            pe, imports_offset + (int64_t)index * stride + 72);
        if (!pe_append_cstring_rva(pe, stream, map,
                                   XX_PE_DATA_STRUCT_ASCII_STRING,
                                   name_rva))
            return false;
    }
    return true;
}

static bool pe_append_hot_patch(xx_pe *pe, pe_data_stream *stream,
                                const xx_memory_map *map, uint32_t rva) {
    int64_t offset;
    uint32_t declared;
    uint32_t list_offset;
    uint32_t base_count;
    uint64_t list_rva;
    int64_t list_absolute;
    if (!rva || !pe_rva_range(pe, map, rva, 20U, &offset)) return true;
    declared = pe_u32(pe, offset + 4);
    if (declared < 20U || !pe_rva_range(pe, map, rva, declared, NULL))
        return true;
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_HOT_PATCH_INFO,
                            offset, declared, declared, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    list_offset = pe_u32(pe, offset + 12);
    base_count = pe_u32(pe, offset + 16);
    if (!list_offset || !base_count || base_count > PE_DATA_MAX_TABLE_ENTRIES)
        return true;
    if ((uint64_t)list_offset > UINT32_MAX - rva) return true;
    list_rva = (uint64_t)rva + list_offset;
    if (!pe_rva_range(pe, map, list_rva,
                      (uint64_t)base_count * 32U, &list_absolute))
        return true;
    if (!pe_append_rva_table(pe, stream, map,
                             XX_PE_DATA_STRUCT_HOT_PATCH_BASE,
                             list_rva, base_count, 32U))
        return false;
    for (uint32_t index = 0U; index < base_count; ++index) {
        int64_t base_offset = list_absolute + (int64_t)index * 32;
        uint32_t integrity_offset = pe_u32(pe, base_offset + 16);
        uint32_t integrity_size = pe_u32(pe, base_offset + 20);
        if (!pe_append_absolute(pe, stream,
                                XX_PE_DATA_STRUCT_HOT_PATCH_MACHINE,
                                base_offset + 4, 4U, 4U, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        if (integrity_size == 52U &&
            integrity_offset <= UINT32_MAX - rva &&
            pe_rva_range(pe, map, (uint64_t)rva + integrity_offset,
                         52U, NULL) &&
            !pe_append_rva(pe, stream, map,
                           XX_PE_DATA_STRUCT_HOT_PATCH_HASHES,
                           (uint64_t)rva + integrity_offset,
                           52U, 52U, 1U,
                           XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
    }
    return true;
}

static bool pe_append_load_config(xx_pe *pe, pe_data_stream *stream,
                                  const xx_memory_map *map,
                                  xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_LOAD_CONFIG];
    uint32_t directory_size = pe->data_directory_size[PE_DIR_LOAD_CONFIG];
    uint64_t pointer_width = pe->optional_magic == XX_PE_MAGIC_64 ? 8U : 4U;
    uint64_t maximum = pe->optional_magic == XX_PE_MAGIC_64 ? 328U : 196U;
    int64_t offset;
    uint32_t declared;
    uint64_t effective;
    uint64_t guard_flags = 0U;
    uint64_t guard_stride = 4U;
    uint64_t value;
    bool dynamic_handled = false;
    if (!rva || !directory_size) return true;
    if (directory_size < 4U ||
        !pe_rva_range(pe, map, rva, directory_size, &offset))
        return true;
    declared = pe_u32(pe, offset);
    effective = declared;
    if (effective > directory_size) effective = directory_size;
    if (effective > maximum) effective = maximum;
    if (effective < 4U) {
        return pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset, directory_size);
    }
    if (!pe_append_absolute(
            pe, stream,
            pe->optional_magic == XX_PE_MAGIC_64
                ? XX_PE_DATA_STRUCT_LOAD_CONFIG64
                : XX_PE_DATA_STRUCT_LOAD_CONFIG32,
            offset, effective, effective, 1U,
            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (directory_size > effective &&
        !pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset + (int64_t)effective, directory_size - effective))
        return false;
    if (pe->optional_magic == XX_PE_MAGIC_64) {
        if (pe_load_has(effective, 148U, 12U) &&
            !pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_LOAD_CONFIG_CODE_INTEGRITY,
                offset + 148, 12U, 12U, 1U,
                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        if (pe_load_has(effective, 144U, 4U))
            guard_flags = pe_u32(pe, offset + 144);
        guard_stride += (guard_flags >> 28U) & 0x0fU;
        if (pe_load_has(effective, 128U, 16U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_FUNCTION,
                xx_io_get_u64(pe->format.device, offset + 128, false),
                xx_io_get_u64(pe->format.device, offset + 136, false),
                guard_stride))
            return false;
        if (pe_load_has(effective, 160U, 16U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_IAT_ENTRY,
                xx_io_get_u64(pe->format.device, offset + 160, false),
                xx_io_get_u64(pe->format.device, offset + 168, false), 4U))
            return false;
        if (pe_load_has(effective, 176U, 16U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_LONGJUMP,
                xx_io_get_u64(pe->format.device, offset + 176, false),
                xx_io_get_u64(pe->format.device, offset + 184, false), 4U))
            return false;
        if (pe_load_has(effective, 264U, 16U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_EH_CONTINUATION,
                xx_io_get_u64(pe->format.device, offset + 264, false),
                xx_io_get_u64(pe->format.device, offset + 272, false), 4U))
            return false;
    } else {
        if (pe_load_has(effective, 92U, 12U) &&
            !pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_LOAD_CONFIG_CODE_INTEGRITY,
                offset + 92, 12U, 12U, 1U,
                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        if (pe_load_has(effective, 88U, 4U))
            guard_flags = pe_u32(pe, offset + 88);
        guard_stride += (guard_flags >> 28U) & 0x0fU;
        if (pe_load_has(effective, 64U, 8U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_SE_HANDLER,
                pe_u32(pe, offset + 64), pe_u32(pe, offset + 68), 4U))
            return false;
        if (pe_load_has(effective, 80U, 8U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_FUNCTION,
                pe_u32(pe, offset + 80), pe_u32(pe, offset + 84),
                guard_stride))
            return false;
        if (pe_load_has(effective, 104U, 8U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_IAT_ENTRY,
                pe_u32(pe, offset + 104), pe_u32(pe, offset + 108), 4U))
            return false;
        if (pe_load_has(effective, 112U, 8U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_LONGJUMP,
                pe_u32(pe, offset + 112), pe_u32(pe, offset + 116), 4U))
            return false;
        if (pe_load_has(effective, 164U, 8U) &&
            !pe_append_va_table(
                pe, stream, map,
                XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_EH_CONTINUATION,
                pe_u32(pe, offset + 164), pe_u32(pe, offset + 168), 4U))
            return false;
    }
    if (xx_pd_is_stopped(pd)) return false;
    {
        uint64_t offset_field = pe->optional_magic == XX_PE_MAGIC_64
                                    ? 224U : 136U;
        uint64_t section_field = pe->optional_magic == XX_PE_MAGIC_64
                                     ? 228U : 140U;
        if (pe_load_has(effective, offset_field, 4U) &&
            pe_load_has(effective, section_field, 2U)) {
            uint32_t dynamic_offset = pe_u32(
                pe, offset + (int64_t)offset_field);
            uint16_t section_number = xx_io_get_u16(
                pe->format.device, offset + (int64_t)section_field, false);
            if (section_number > 0U &&
                section_number <= pe->number_of_sections) {
                const xx_pe_section *section =
                    &pe->sections[section_number - 1U];
                int64_t dynamic_file_offset = -1;
                if (dynamic_offset <= section->raw_size &&
                    section->raw_size - dynamic_offset >= 8U) {
                    if (pe->format.is_mapped) {
                        (void)pe_rva_range(
                            pe, map,
                            (uint64_t)section->virtual_address +
                                dynamic_offset,
                            8U, &dynamic_file_offset);
                    } else {
                        (void)pe_absolute_range(
                            pe, (uint64_t)section->raw_offset +
                                    dynamic_offset,
                            8U, &dynamic_file_offset);
                    }
                }
                if (dynamic_file_offset >= 0) {
                    {
                        uint32_t dynamic_size = pe_u32(
                            pe, dynamic_file_offset + 4);
                        uint64_t available =
                            (uint64_t)section->raw_size - dynamic_offset;
                        if ((uint64_t)dynamic_size + 8U <= available) {
                            if (!pe_append_dynamic_relocations_at(
                                    pe, stream, dynamic_file_offset, pd))
                                return false;
                            dynamic_handled = true;
                        }
                    }
                }
            }
        }
    }
    value = pe->optional_magic == XX_PE_MAGIC_64 ? 192U : 120U;
    if (!dynamic_handled && pe_load_has(effective, value, pointer_width) &&
        !pe_append_dynamic_relocations(
            pe, stream, map,
            pe_load_pointer(pe, offset + (int64_t)value), pd))
        return false;
    value = pe->optional_magic == XX_PE_MAGIC_64 ? 200U : 124U;
    if (pe_load_has(effective, value, pointer_width) &&
        !pe_append_chpe_metadata(
            pe, stream, map, pe_load_pointer(pe, offset + (int64_t)value)))
        return false;
    value = pe->optional_magic == XX_PE_MAGIC_64 ? 240U : 148U;
    if (pe_load_has(effective, value, 4U) &&
        !pe_append_hot_patch(pe, stream, map,
                             pe_u32(pe, offset + (int64_t)value)))
        return false;
    value = pe->optional_magic == XX_PE_MAGIC_64 ? 248U : 156U;
    if (pe_load_has(effective, value, pointer_width) &&
        !pe_append_enclave(
            pe, stream, map, pe_load_pointer(pe, offset + (int64_t)value)))
        return false;
    value = pe->optional_magic == XX_PE_MAGIC_64 ? 256U : 160U;
    if (pe_load_has(effective, value, pointer_width) &&
        !pe_append_volatile_metadata(
            pe, stream, map, pe_load_pointer(pe, offset + (int64_t)value)))
        return false;
    return true;
}

static bool pe_append_bound_imports(xx_pe *pe, pe_data_stream *stream,
                                    const xx_memory_map *map,
                                    xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_BOUND_IMPORT];
    uint32_t size = pe->data_directory_size[PE_DIR_BOUND_IMPORT];
    int64_t offset;
    uint64_t cursor = 0U;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    while (cursor < size) {
        int64_t descriptor;
        uint16_t name_offset;
        uint16_t refs;
        uint64_t ref_bytes;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U) break;
        descriptor = offset + (int64_t)cursor;
        name_offset = xx_io_get_u16(pe->format.device, descriptor + 4,
                                    false);
        refs = xx_io_get_u16(pe->format.device, descriptor + 6, false);
        if (pe_u32(pe, descriptor) == 0U && name_offset == 0U && refs == 0U)
            break;
        if (!pe_u64_product(refs, 8U, &ref_bytes) ||
            ref_bytes > size - cursor - 8U)
            break;
        if (!pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_BOUND_IMPORT_DESCRIPTOR,
                descriptor, 8U, 8U, 1U, XX_DATA_STRUCT_TYPE_ENTRY) ||
            (refs && !pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_BOUND_FORWARDER_REF,
                descriptor + 8, 8U, ref_bytes, refs,
                XX_DATA_STRUCT_TYPE_ENTRY)))
            return false;
        if (name_offset < size) {
            uint64_t length = pe_cstring_size(pe, offset + name_offset,
                                              size - name_offset);
            if (length && !pe_append_absolute(
                    pe, stream, XX_PE_DATA_STRUCT_ASCII_STRING,
                    offset + name_offset, 1U, length, length,
                    XX_DATA_STRUCT_TYPE_RAW_DATA))
                return false;
        }
        for (uint16_t index = 0U; index < refs; ++index) {
            uint16_t child_name = xx_io_get_u16(
                pe->format.device, descriptor + 8 + (int64_t)index * 8 + 4,
                false);
            if (child_name < size) {
                uint64_t length = pe_cstring_size(pe, offset + child_name,
                                                  size - child_name);
                if (length && !pe_append_absolute(
                        pe, stream, XX_PE_DATA_STRUCT_ASCII_STRING,
                        offset + child_name, 1U, length, length,
                        XX_DATA_STRUCT_TYPE_RAW_DATA))
                    return false;
            }
        }
        cursor += 8U + ref_bytes;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        offset + (int64_t)cursor, size - cursor);
}

static bool pe_append_delay_imports(xx_pe *pe, pe_data_stream *stream,
                                    const xx_memory_map *map,
                                    xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_DELAY_IMPORT];
    uint32_t size = pe->data_directory_size[PE_DIR_DELAY_IMPORT];
    int64_t offset;
    uint64_t count = 0U;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    while (count < size / 32U && count < PE_DATA_MAX_LINKED_ENTRIES) {
        int64_t current = offset + (int64_t)(count * 32U);
        bool zero = true;
        for (uint32_t word = 0U; word < 8U; ++word)
            if (pe_u32(pe, current + (int64_t)word * 4) != 0U) {
                zero = false;
                break;
            }
        if (zero) break;
        ++count;
    }
    if (count && !pe_append_absolute(
            pe, stream, XX_PE_DATA_STRUCT_DELAY_IMPORT_DESCRIPTOR,
            offset, 32U, count * 32U, count,
            XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < count; ++index) {
        int64_t current = offset + (int64_t)(index * 32U);
        uint32_t attributes = pe_u32(pe, current);
        bool is_va = (attributes & 1U) == 0U;
        uint64_t name = pe_u32(pe, current + 4);
        uint64_t iat = pe_u32(pe, current + 12);
        uint64_t int_table = pe_u32(pe, current + 16);
        if (xx_pd_is_stopped(pd) ||
            !pe_append_location_string(pe, stream, map,
                                       XX_PE_DATA_STRUCT_ASCII_STRING,
                                       name, is_va) ||
            !pe_append_thunks(pe, stream, map,
                              int_table ? int_table : iat, iat,
                              is_va, is_va, pd))
            return false;
    }
    return count || size == 0U
               ? true
               : pe_append_raw_absolute(
                     pe, stream,
                     XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                     offset, size);
}

static bool pe_append_iat(xx_pe *pe, pe_data_stream *stream,
                          const xx_memory_map *map) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_IAT];
    uint32_t size = pe->data_directory_size[PE_DIR_IAT];
    uint64_t stride = pe->optional_magic == XX_PE_MAGIC_64 ? 8U : 4U;
    int64_t offset;
    uint64_t count;
    uint64_t covered;
    if (!rva || !size) return true;
    if (!pe_rva_range(pe, map, rva, size, &offset)) return true;
    count = size / stride;
    covered = count * stride;
    return (!count || pe_append_absolute(
               pe, stream,
               stride == 8U ? XX_PE_DATA_STRUCT_IAT64
                            : XX_PE_DATA_STRUCT_IAT32,
               offset, stride, covered, count,
               XX_DATA_STRUCT_TYPE_ENTRY)) &&
           pe_append_raw_absolute(
               pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
               offset + (int64_t)covered, size - covered);
}

static bool pe_append_clr_metadata(xx_pe *pe, pe_data_stream *stream,
                                   const xx_memory_map *map, uint32_t rva,
                                   uint32_t size, xx_pd_struct *pd) {
    int64_t offset;
    uint32_t version_length;
    uint64_t storage_relative;
    uint16_t streams;
    uint64_t cursor;
    if (!rva || size < 20U ||
        !pe_rva_range(pe, map, rva, size, &offset))
        return true;
    if (pe_u32(pe, offset) != UINT32_C(0x424a5342))
        return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_CLR_RAW,
                                      offset, size);
    version_length = pe_u32(pe, offset + 12);
    if (version_length > size - 16U ||
        !pe_align_up(16U + version_length, 4U, &storage_relative) ||
        storage_relative > size - 4U)
        return pe_append_raw_absolute(pe, stream, XX_PE_DATA_STRUCT_CLR_RAW,
                                      offset, size);
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_CLR_METADATA_ROOT,
                            offset, 16U, 16U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        (version_length && !pe_append_absolute(
            pe, stream, XX_PE_DATA_STRUCT_ASCII_STRING,
            offset + 16, 1U, version_length, version_length,
            XX_DATA_STRUCT_TYPE_RAW_DATA)) ||
        !pe_append_raw_absolute(
            pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
            offset + 16 + version_length,
            storage_relative - 16U - version_length) ||
        !pe_append_absolute(
            pe, stream, XX_PE_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER,
            offset + (int64_t)storage_relative, 4U, 4U, 1U,
            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    streams = xx_io_get_u16(pe->format.device,
                            offset + (int64_t)storage_relative + 2, false);
    cursor = storage_relative + 4U;
    if (streams > PE_DATA_MAX_LINKED_ENTRIES) streams = 0U;
    for (uint16_t index = 0U; index < streams; ++index) {
        uint64_t name_length;
        uint64_t next;
        uint32_t stream_offset;
        uint32_t stream_size;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 9U) break;
        name_length = pe_cstring_size(pe, offset + (int64_t)cursor + 8,
                                      size - cursor - 8U);
        if (!name_length ||
            !pe_align_up(cursor + 8U + name_length, 4U, &next) ||
            next > size)
            break;
        if (!pe_append_absolute(
                pe, stream, XX_PE_DATA_STRUCT_CLR_STREAM_HEADER,
                offset + (int64_t)cursor, next - cursor,
                next - cursor, 1U, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        stream_offset = pe_u32(pe, offset + (int64_t)cursor);
        stream_size = pe_u32(pe, offset + (int64_t)cursor + 4);
        if (stream_offset <= size && stream_size <= size - stream_offset &&
            !pe_append_raw_absolute(pe, stream,
                                    XX_PE_DATA_STRUCT_CLR_RAW,
                                    offset + stream_offset, stream_size))
            return false;
        cursor = next;
    }
    return true;
}

static bool pe_append_clr_raw_directory(xx_pe *pe,
                                        pe_data_stream *stream,
                                        const xx_memory_map *map,
                                        int64_t cor_offset,
                                        uint32_t field_offset) {
    uint32_t rva = pe_u32(pe, cor_offset + field_offset);
    uint32_t size = pe_u32(pe, cor_offset + field_offset + 4);
    if (!rva || !size || !pe_rva_range(pe, map, rva, size, NULL)) return true;
    return pe_append_raw_rva(pe, stream, map, XX_PE_DATA_STRUCT_CLR_RAW,
                             rva, size);
}

static bool pe_append_clr(xx_pe *pe, pe_data_stream *stream,
                          const xx_memory_map *map, xx_pd_struct *pd) {
    uint32_t rva = pe->data_directory_rva[PE_DIR_COM_DESCRIPTOR];
    uint32_t size = pe->data_directory_size[PE_DIR_COM_DESCRIPTOR];
    int64_t offset;
    uint32_t cb;
    uint32_t fixup_rva;
    uint32_t fixup_size;
    int64_t fixup_offset;
    uint64_t fixup_count;
    if (!rva || !size) return true;
    if (size < 72U || !pe_rva_range(pe, map, rva, size, &offset))
        return true;
    cb = pe_u32(pe, offset);
    if (cb < 72U) return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW, offset, size);
    if (!pe_append_absolute(pe, stream, XX_PE_DATA_STRUCT_COR20_HEADER,
                            offset, 72U, 72U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
        !pe_append_raw_absolute(
            pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
            offset + 72, size - 72U) ||
        !pe_append_clr_metadata(pe, stream, map,
                                pe_u32(pe, offset + 8),
                                pe_u32(pe, offset + 12), pd))
        return false;
    if (!pe_append_clr_raw_directory(pe, stream, map, offset, 24U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 32U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 40U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 56U) ||
        !pe_append_clr_raw_directory(pe, stream, map, offset, 64U))
        return false;
    fixup_rva = pe_u32(pe, offset + 48);
    fixup_size = pe_u32(pe, offset + 52);
    if (!fixup_rva || fixup_size < 8U ||
        !pe_rva_range(pe, map, fixup_rva, fixup_size, &fixup_offset))
        return true;
    fixup_count = fixup_size / 8U;
    if (!pe_append_absolute(pe, stream,
                            XX_PE_DATA_STRUCT_CLR_VTABLE_FIXUP,
                            fixup_offset, 8U, fixup_count * 8U,
                            fixup_count, XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    for (uint64_t index = 0U; index < fixup_count; ++index) {
        int64_t entry = fixup_offset + (int64_t)(index * 8U);
        uint32_t table_rva = pe_u32(pe, entry);
        uint16_t table_count = xx_io_get_u16(pe->format.device,
                                             entry + 4, false);
        uint16_t type = xx_io_get_u16(pe->format.device, entry + 6, false);
        uint64_t stride = (type & 2U) ? 8U : 4U;
        uint64_t bytes;
        if (table_count <= PE_DATA_MAX_TABLE_ENTRIES &&
            pe_u64_product(table_count, stride, &bytes) &&
            pe_rva_range(pe, map, table_rva, bytes, NULL) &&
            !pe_append_raw_rva(pe, stream, map,
                               XX_PE_DATA_STRUCT_CLR_RAW,
                               table_rva, bytes))
            return false;
    }
    return pe_append_raw_absolute(
        pe, stream, XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
        fixup_offset + (int64_t)(fixup_count * 8U),
        fixup_size - fixup_count * 8U);
}

static bool pe_append_directory_raw(xx_pe *pe, pe_data_stream *stream,
                                    const xx_memory_map *map,
                                    uint32_t directory) {
    uint32_t rva = pe->data_directory_rva[directory];
    uint32_t size = pe->data_directory_size[directory];
    if (!rva || !size || !pe_rva_range(pe, map, rva, size, NULL)) return true;
    return pe_append_raw_rva(pe, stream, map,
                             XX_PE_DATA_STRUCT_RESERVED_DIRECTORY_RAW,
                             rva, size);
}

static bool pe_data_build(xx_pe *pe, pe_data_stream *stream,
                          xx_pd_struct *pd) {
    const xx_memory_map *map;
    if (!pe || !stream || xx_pd_is_stopped(pd)) return false;
    map = xx_format_get_memory_map(&pe->format,
                                   XX_MEMORY_MAP_MODE_UNKNOWN, pd);
    if (!map) return false;
    if (!pe_append_core(pe, stream, pd) ||
        !pe_append_coff(pe, stream, pd) ||
        !pe_append_export(pe, stream, map, pd) ||
        !pe_append_import(pe, stream, map, pd) ||
        !pe_append_resources(pe, stream, map, pd) ||
        !pe_append_exception(pe, stream, map, pd) ||
        !pe_append_security(pe, stream, pd) ||
        !pe_append_base_relocations(pe, stream, map, pd) ||
        !pe_append_debug(pe, stream, map, pd) ||
        !pe_append_architecture(pe, stream, map, pd) ||
        !pe_append_directory_raw(pe, stream, map, PE_DIR_GLOBALPTR) ||
        !pe_append_tls(pe, stream, map, pd) ||
        !pe_append_load_config(pe, stream, map, pd) ||
        !pe_append_bound_imports(pe, stream, map, pd) ||
        !pe_append_iat(pe, stream, map) ||
        !pe_append_delay_imports(pe, stream, map, pd) ||
        !pe_append_clr(pe, stream, map, pd) ||
        !pe_append_directory_raw(pe, stream, map, PE_DIR_RESERVED))
        return false;
    if (pe->format.overlay_offset >= pe->format.base_address &&
        pe->format.overlay_size > 0 &&
        !pe_append_raw_absolute(pe, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                pe->format.overlay_offset,
                                (uint64_t)pe->format.overlay_size))
        return false;
    return !xx_pd_is_stopped(pd);
}

const char *xx_pe_data_struct_id_to_string(Abstractformat *format,
                                            uint32_t id) {
    (void)format;
    return id <= XX_PE_DATA_STRUCT_LAST ? pe_data_names[id] : "UNKNOWN";
}

uint32_t xx_pe_data_struct_string_to_id(Abstractformat *format,
                                         const char *name) {
    uint32_t id;
    (void)format;
    if (!name) return XX_PE_DATA_STRUCT_UNKNOWN;
    if (xx_rt_strcmp(name, "IMAGE_OPTIONAL_HEADER") == 0)
        return XX_PE_DATA_STRUCT_OPTIONAL_HEADER32;
    for (id = XX_PE_DATA_STRUCT_UNKNOWN; id <= XX_PE_DATA_STRUCT_LAST; ++id)
        if (xx_rt_strcmp(name, pe_data_names[id]) == 0) return id;
    return XX_PE_DATA_STRUCT_UNKNOWN;
}

xx_data_struct_state *xx_pe_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_data_struct_state *state = NULL;
    pe_data_stream *stream = NULL;
    xx_pe *pe = (xx_pe *)format;
    if (!format || !format->device || xx_pd_is_stopped(pd) ||
        (!format->base_info_handled &&
         !xx_pe_handle_base_info(format, pd)))
        return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (pe_data_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream || !pe_data_build(pe, stream, pd) ||
        stream->count == 0U || stream->count > INT64_MAX)
        goto fail;
    xx_data_struct_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pe_data_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_struct = stream->items[0];
    state->current_index = 0;
    state->has_struct = true;
    return state;
fail:
    if (stream) pe_data_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

const xx_data_struct *xx_pe_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state) {
    return format && state && state->format == format && state->has_struct
               ? &state->current_struct : NULL;
}

bool xx_pe_data_struct_move_to_next(Abstractformat *format,
                                     xx_data_struct_state *state,
                                     xx_pd_struct *pd) {
    pe_data_stream *stream;
    int64_t next;
    if (!format || !state || state->format != format ||
        xx_pd_is_stopped(pd) ||
        !(stream = (pe_data_stream *)state->internal_state)) {
        if (state) state->has_struct = false;
        return false;
    }
    next = state->current_index + 1;
    if (next < 0 || (uint64_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_struct = stream->items[next];
    state->current_index = next;
    state->has_struct = true;
    return true;
}

void xx_pe_free_data_structs_reading(Abstractformat *format,
                                      xx_data_struct_state *state) {
    (void)format;
    xx_data_struct_state_free(state);
}

static bool pe_static_fields(
    const xx_pe *pe, const xx_data_struct *ds,
    const xx_data_struct_field_desc **fields, size_t *count) {
    if (!pe || !ds || !fields || !count) return false;
    *fields = NULL;
    *count = 0U;
#define PE_SELECT(array_) do { \
        *fields = array_; *count = PE_COUNT(array_); return true; \
    } while (0)
    switch (ds->id) {
        case XX_PE_DATA_STRUCT_DOS_HEADER: PE_SELECT(pe_dos_fields);
        case XX_PE_DATA_STRUCT_NT_SIGNATURE: PE_SELECT(pe_signature_fields);
        case XX_PE_DATA_STRUCT_FILE_HEADER: PE_SELECT(pe_file_fields);
        case XX_PE_DATA_STRUCT_OPTIONAL_HEADER32: PE_SELECT(pe_optional32_fields);
        case XX_PE_DATA_STRUCT_OPTIONAL_HEADER64: PE_SELECT(pe_optional64_fields);
        case XX_PE_DATA_STRUCT_DATA_DIRECTORY: {
            int64_t optional = pe->format.base_address + pe->pe_offset + 24;
            int64_t directory = optional +
                (pe->optional_magic == XX_PE_MAGIC_64 ? 112 : 96);
            int64_t index = ds->offset >= directory
                                ? (ds->offset - directory) / 8 : -1;
            if (index == PE_DIR_SECURITY) PE_SELECT(pe_directory_file_fields);
            PE_SELECT(pe_directory_rva_fields);
        }
        case XX_PE_DATA_STRUCT_SECTION_HEADER: PE_SELECT(pe_section_fields);
        case XX_PE_DATA_STRUCT_COFF_SYMBOL: PE_SELECT(pe_symbol_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_SYMBOL: PE_SELECT(pe_aux_generic_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_FUNCTION_DEFINITION: PE_SELECT(pe_aux_function_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_BF_EF: PE_SELECT(pe_aux_bfef_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_WEAK_EXTERNAL: PE_SELECT(pe_aux_weak_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_FILE: PE_SELECT(pe_aux_file_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_SECTION_DEFINITION: PE_SELECT(pe_aux_section_fields);
        case XX_PE_DATA_STRUCT_COFF_AUX_CLR_TOKEN: PE_SELECT(pe_aux_clr_fields);
        case XX_PE_DATA_STRUCT_COFF_STRING_TABLE_HEADER: PE_SELECT(pe_string_table_header_fields);
        case XX_PE_DATA_STRUCT_COFF_RELOCATION: PE_SELECT(pe_relocation_fields);
        case XX_PE_DATA_STRUCT_COFF_LINE_NUMBER: PE_SELECT(pe_line_fields);
        case XX_PE_DATA_STRUCT_EXPORT_DIRECTORY: PE_SELECT(pe_export_fields);
        case XX_PE_DATA_STRUCT_EXPORT_ADDRESS_TABLE:
        case XX_PE_DATA_STRUCT_EXPORT_NAME_POINTER_TABLE:
            PE_SELECT(pe_rva_entry_fields);
        case XX_PE_DATA_STRUCT_EXPORT_ORDINAL_TABLE: PE_SELECT(pe_ordinal_fields);
        case XX_PE_DATA_STRUCT_IMPORT_DESCRIPTOR: PE_SELECT(pe_import_fields);
        case XX_PE_DATA_STRUCT_THUNK32: PE_SELECT(pe_thunk32_fields);
        case XX_PE_DATA_STRUCT_THUNK64: PE_SELECT(pe_thunk64_fields);
        case XX_PE_DATA_STRUCT_RESOURCE_DIRECTORY: PE_SELECT(pe_resource_dir_fields);
        case XX_PE_DATA_STRUCT_RESOURCE_DIRECTORY_ENTRY: PE_SELECT(pe_resource_entry_fields);
        case XX_PE_DATA_STRUCT_RESOURCE_DATA_ENTRY: PE_SELECT(pe_resource_data_fields);
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_X64:
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_IA64:
        case XX_PE_DATA_STRUCT_UNWIND_CHAINED_FUNCTION:
            PE_SELECT(pe_runtime_x64_fields);
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM:
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ARM64:
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_CE:
            PE_SELECT(pe_runtime_arm_fields);
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_MIPS:
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ALPHA32:
            PE_SELECT(pe_runtime_mips_fields);
        case XX_PE_DATA_STRUCT_RUNTIME_FUNCTION_ALPHA64: PE_SELECT(pe_runtime_alpha64_fields);
        case XX_PE_DATA_STRUCT_UNWIND_INFO: PE_SELECT(pe_unwind_info_fields);
        case XX_PE_DATA_STRUCT_UNWIND_CODE: PE_SELECT(pe_unwind_code_fields);
        case XX_PE_DATA_STRUCT_UNWIND_HANDLER: PE_SELECT(pe_unwind_handler_fields);
        case XX_PE_DATA_STRUCT_ARM64_XDATA_HEADER:
        case XX_PE_DATA_STRUCT_ARM32_XDATA_HEADER: PE_SELECT(pe_arm_xdata_fields);
        case XX_PE_DATA_STRUCT_ARM64_XDATA_EXTENDED:
        case XX_PE_DATA_STRUCT_ARM32_XDATA_EXTENDED: PE_SELECT(pe_xdata_extended_fields);
        case XX_PE_DATA_STRUCT_ARM64_EPILOG_SCOPE:
        case XX_PE_DATA_STRUCT_ARM32_EPILOG_SCOPE: PE_SELECT(pe_xdata_scope_fields);
        case XX_PE_DATA_STRUCT_WIN_CERTIFICATE: PE_SELECT(pe_win_certificate_fields);
        case XX_PE_DATA_STRUCT_BASE_RELOCATION_BLOCK: PE_SELECT(pe_base_reloc_fields);
        case XX_PE_DATA_STRUCT_BASE_RELOCATION_ENTRY: PE_SELECT(pe_base_reloc_entry_fields);
        case XX_PE_DATA_STRUCT_DEBUG_DIRECTORY: PE_SELECT(pe_debug_dir_fields);
        case XX_PE_DATA_STRUCT_DEBUG_COFF_SYMBOLS_HEADER: PE_SELECT(pe_debug_coff_fields);
        case XX_PE_DATA_STRUCT_CODEVIEW_RSDS: PE_SELECT(pe_rsds_fields);
        case XX_PE_DATA_STRUCT_CODEVIEW_NB10: PE_SELECT(pe_nb10_fields);
        case XX_PE_DATA_STRUCT_DEBUG_MISC: PE_SELECT(pe_debug_misc_fields);
        case XX_PE_DATA_STRUCT_DEBUG_FPO: PE_SELECT(pe_fpo_fields);
        case XX_PE_DATA_STRUCT_DEBUG_OMAP: PE_SELECT(pe_omap_fields);
        case XX_PE_DATA_STRUCT_DEBUG_VC_FEATURE: PE_SELECT(pe_vc_feature_fields);
        case XX_PE_DATA_STRUCT_DEBUG_POGO_HEADER: PE_SELECT(pe_pogo_header_fields);
        case XX_PE_DATA_STRUCT_DEBUG_POGO_ENTRY: PE_SELECT(pe_pogo_entry_fields);
        case XX_PE_DATA_STRUCT_DEBUG_EMBEDDED_PORTABLE_PDB: PE_SELECT(pe_mpdb_fields);
        case XX_PE_DATA_STRUCT_DEBUG_REPRO_HASH_HEADER: PE_SELECT(pe_repro_fields);
        case XX_PE_DATA_STRUCT_DEBUG_GUID: PE_SELECT(pe_guid_fields);
        case XX_PE_DATA_STRUCT_DEBUG_EX_DLL_CHARACTERISTICS: PE_SELECT(pe_dword_flags_fields);
        case XX_PE_DATA_STRUCT_DEBUG_R2R_PERFMAP: PE_SELECT(pe_r2r_fields);
        case XX_PE_DATA_STRUCT_DEBUG_FUNCTION_ENTRY32: PE_SELECT(pe_debug_function32_fields);
        case XX_PE_DATA_STRUCT_DEBUG_FUNCTION_ENTRY64: PE_SELECT(pe_debug_function64_fields);
        case XX_PE_DATA_STRUCT_POLICY_METADATA: PE_SELECT(pe_policy_metadata_fields);
        case XX_PE_DATA_STRUCT_POLICY_ENTRY32: PE_SELECT(pe_policy_entry32_fields);
        case XX_PE_DATA_STRUCT_POLICY_ENTRY64: PE_SELECT(pe_policy_entry64_fields);
        case XX_PE_DATA_STRUCT_ARCHITECTURE_HEADER: PE_SELECT(pe_architecture_fields);
        case XX_PE_DATA_STRUCT_ARCHITECTURE_ENTRY: PE_SELECT(pe_architecture_entry_fields);
        case XX_PE_DATA_STRUCT_TLS_DIRECTORY32: PE_SELECT(pe_tls32_fields);
        case XX_PE_DATA_STRUCT_TLS_DIRECTORY64: PE_SELECT(pe_tls64_fields);
        case XX_PE_DATA_STRUCT_TLS_CALLBACK32:
        case XX_PE_DATA_STRUCT_IAT32: PE_SELECT(pe_pointer32_fields);
        case XX_PE_DATA_STRUCT_TLS_CALLBACK64:
        case XX_PE_DATA_STRUCT_IAT64: PE_SELECT(pe_pointer64_fields);
        case XX_PE_DATA_STRUCT_LOAD_CONFIG32: PE_SELECT(pe_load32_fields);
        case XX_PE_DATA_STRUCT_LOAD_CONFIG64: PE_SELECT(pe_load64_fields);
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_CODE_INTEGRITY: PE_SELECT(pe_code_integrity_fields);
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_SE_HANDLER:
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_FUNCTION:
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_IAT_ENTRY:
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_LONGJUMP:
        case XX_PE_DATA_STRUCT_LOAD_CONFIG_GUARD_EH_CONTINUATION:
        case XX_PE_DATA_STRUCT_VOLATILE_ACCESS:
            PE_SELECT(pe_rva_entry_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_TABLE: PE_SELECT(pe_dynamic_table_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION32: PE_SELECT(pe_dynamic32_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION64: PE_SELECT(pe_dynamic64_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION32_V2: PE_SELECT(pe_dynamic32_v2_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION64_V2: PE_SELECT(pe_dynamic64_v2_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_ARM64X:
            if (pe->optional_magic == XX_PE_MAGIC_64) {
                if (ds->entry_size >= 24) PE_SELECT(pe_dynamic64_v2_fields);
                PE_SELECT(pe_dynamic64_fields);
            }
            if (ds->entry_size >= 20) PE_SELECT(pe_dynamic32_v2_fields);
            PE_SELECT(pe_dynamic32_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_PROLOGUE: PE_SELECT(pe_prologue_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_EPILOGUE: PE_SELECT(pe_epilogue_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_IMPORT_CONTROL:
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_ARM64_IMPORT_CONTROL:
        case XX_PE_DATA_STRUCT_HOT_PATCH_MACHINE:
            PE_SELECT(pe_dword_flags_fields);
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_INDIR:
        case XX_PE_DATA_STRUCT_DYNAMIC_RELOCATION_SWITCHTABLE:
            PE_SELECT(pe_word_flags_fields);
        case XX_PE_DATA_STRUCT_FUNCTION_OVERRIDE_HEADER: PE_SELECT(pe_function_override_fields);
        case XX_PE_DATA_STRUCT_FUNCTION_OVERRIDE_ENTRY: PE_SELECT(pe_function_override_entry_fields);
        case XX_PE_DATA_STRUCT_BDD_INFO: PE_SELECT(pe_bdd_info_fields);
        case XX_PE_DATA_STRUCT_BDD_NODE: PE_SELECT(pe_bdd_node_fields);
        case XX_PE_DATA_STRUCT_HOT_PATCH_INFO: PE_SELECT(pe_hot_patch_info_fields);
        case XX_PE_DATA_STRUCT_HOT_PATCH_BASE: PE_SELECT(pe_hot_patch_base_fields);
        case XX_PE_DATA_STRUCT_HOT_PATCH_HASHES: PE_SELECT(pe_hot_patch_hashes_fields);
        case XX_PE_DATA_STRUCT_ENCLAVE_CONFIG32: PE_SELECT(pe_enclave32_fields);
        case XX_PE_DATA_STRUCT_ENCLAVE_CONFIG64: PE_SELECT(pe_enclave64_fields);
        case XX_PE_DATA_STRUCT_ENCLAVE_IMPORT: PE_SELECT(pe_enclave_import_fields);
        case XX_PE_DATA_STRUCT_VOLATILE_METADATA: PE_SELECT(pe_volatile_fields);
        case XX_PE_DATA_STRUCT_VOLATILE_INFO_RANGE: PE_SELECT(pe_volatile_range_fields);
        case XX_PE_DATA_STRUCT_CHPE_METADATA:
        case XX_PE_DATA_STRUCT_ARM64EC_METADATA: PE_SELECT(pe_chpe_fields);
        case XX_PE_DATA_STRUCT_CHPE_CODE_RANGE_ENTRY: PE_SELECT(pe_chpe_code_range_fields);
        case XX_PE_DATA_STRUCT_CHPE_CODE_RANGE_TO_ENTRY_POINT: PE_SELECT(pe_chpe_range_entry_fields);
        case XX_PE_DATA_STRUCT_CHPE_REDIRECTION_ENTRY: PE_SELECT(pe_chpe_redirect_fields);
        case XX_PE_DATA_STRUCT_BOUND_IMPORT_DESCRIPTOR: PE_SELECT(pe_bound_fields);
        case XX_PE_DATA_STRUCT_BOUND_FORWARDER_REF: PE_SELECT(pe_bound_forwarder_fields);
        case XX_PE_DATA_STRUCT_DELAY_IMPORT_DESCRIPTOR: PE_SELECT(pe_delay_fields);
        case XX_PE_DATA_STRUCT_COR20_HEADER: PE_SELECT(pe_cor20_fields);
        case XX_PE_DATA_STRUCT_CLR_METADATA_ROOT: PE_SELECT(pe_clr_root_fields);
        case XX_PE_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER: PE_SELECT(pe_clr_storage_fields);
        case XX_PE_DATA_STRUCT_CLR_STREAM_HEADER: PE_SELECT(pe_clr_stream_fields);
        case XX_PE_DATA_STRUCT_CLR_VTABLE_FIXUP: PE_SELECT(pe_clr_vtable_fields);
        case XX_PE_DATA_STRUCT_ARM64X_FIXUP: PE_SELECT(pe_arm64x_fixup_fields);
        case XX_PE_DATA_STRUCT_ARM64X_DELTA: PE_SELECT(pe_arm64x_delta_fields);
        default: return false;
    }
#undef PE_SELECT
}

static void pe_record_stream_free(void *pointer) {
    pe_record_stream *stream = (pe_record_stream *)pointer;
    if (!stream) return;
    if (stream->fields) xx_mem_free(stream->fields);
    xx_mem_free(stream);
}

static bool pe_finish_record(xx_data_struct_record *record,
                             const xx_data_struct_field_desc *field,
                             const wchar_t *display) {
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_data_struct_record_set_display_value(record, display))
        return true;
    xx_data_struct_record_cleanup(record);
    return false;
}

static bool pe_populate_bytes(Abstractformat *format,
                              xx_data_struct_record_state *state,
                              const xx_data_struct_field_desc *field,
                              bool as_string) {
    xx_data_struct_record *record = &state->current_record;
    uint64_t size;
    uint64_t limit;
    uint64_t index;
    uint8_t *bytes;
    wchar_t display[PE_DATA_MAX_DISPLAY * 3U + 8U];
    size_t display_at = 0U;
    if (field->size < 0 || state->parent_struct.offset < 0 ||
        field->rel_offset < 0 ||
        state->parent_struct.offset > INT64_MAX - field->rel_offset)
        return false;
    size = (uint64_t)field->size;
    limit = size > PE_DATA_MAX_DISPLAY ? PE_DATA_MAX_DISPLAY : size;
    bytes = (uint8_t *)xx_mem_alloc((size_t)limit + 1U);
    if (!bytes) return false;
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    display[0] = L'\0';
    for (index = 0U; index < limit; ++index) {
        uint8_t byte = xx_io_get_u8(
            format->device, state->parent_struct.offset + field->rel_offset +
                                (int64_t)index);
        bytes[index] = byte;
        if (as_string && byte == 0U) {
            limit = index;
            break;
        }
        if (!as_string && display_at + 3U < PE_COUNT(display)) {
            display_at = pe_display_append_hex_byte(
                display, PE_COUNT(display), display_at, byte);
        }
    }
    bytes[limit] = 0U;
    if (as_string) {
        for (index = 0U; index < limit && index + 1U < PE_COUNT(display);
             ++index)
            display[index] = bytes[index] >= 0x20U && bytes[index] < 0x7fU
                                 ? (wchar_t)bytes[index] : L'.';
        display[index] = L'\0';
        if (!xx_var_set_str(&record->value, (const char *)bytes)) {
            xx_mem_free(bytes);
            xx_data_struct_record_cleanup(record);
            return false;
        }
    } else {
        if (size > limit && display_at + 4U < PE_COUNT(display)) {
            display[display_at++] = L' ';
            display[display_at++] = L'.';
            display[display_at++] = L'.';
            display[display_at++] = L'.';
            display[display_at] = L'\0';
        }
        if (!xx_var_set_bytes(&record->value, bytes, (size_t)limit)) {
            xx_mem_free(bytes);
            xx_data_struct_record_cleanup(record);
            return false;
        }
    }
    xx_mem_free(bytes);
    return pe_finish_record(record, field, display);
}

static bool pe_populate_unicode(Abstractformat *format,
                                xx_data_struct_record_state *state,
                                const xx_data_struct_field_desc *field) {
    xx_data_struct_record *record = &state->current_record;
    size_t characters = field->size > 0 ? (size_t)field->size / 2U : 0U;
    wchar_t *text;
    size_t index;
    if (characters > PE_DATA_MAX_STRING) characters = PE_DATA_MAX_STRING;
    text = (wchar_t *)xx_mem_alloc((characters + 1U) * sizeof(*text));
    if (!text) return false;
    for (index = 0U; index < characters; ++index) {
        uint16_t value = xx_io_get_u16(
            format->device,
            state->parent_struct.offset + field->rel_offset +
                (int64_t)index * 2,
            false);
        if (!value) break;
        text[index] = (wchar_t)value;
    }
    text[index] = L'\0';
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (!xx_var_set_wstr(&record->value, text)) {
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    if (!pe_finish_record(record, field, text)) {
        xx_mem_free(text);
        return false;
    }
    xx_mem_free(text);
    return true;
}

static bool pe_populate_record(Abstractformat *format,
                               xx_data_struct_record_state *state,
                               size_t index) {
    pe_record_stream *stream;
    const xx_data_struct_field_desc *field;
    if (!format || !format->device || !state ||
        !(stream = (pe_record_stream *)state->internal_state) ||
        index >= stream->count)
        return false;
    field = &stream->fields[index];
    if (xx_rt_wcsncmp(field->type, L"char", 4U) == 0)
        return pe_populate_bytes(format, state, field, true);
    if (xx_rt_wcsncmp(field->type, L"wchar16", 7U) == 0)
        return pe_populate_unicode(format, state, field);
    if (xx_rt_wcschr(field->type, L'[') != NULL)
        return pe_populate_bytes(format, state, field, false);
    if (xx_rt_wcscmp(field->type, L"int16") == 0 ||
        xx_rt_wcscmp(field->type, L"int32") == 0 ||
        xx_rt_wcscmp(field->type, L"int64") == 0) {
        xx_data_struct_record *record = &state->current_record;
        int64_t value;
        wchar_t display[64];
        xx_data_struct_record_init(record);
        record->offset = field->rel_offset;
        record->size = field->size;
        record->property = field->property;
        if (field->size == 8) {
            value = (int64_t)xx_io_get_u64(
                format->device,
                state->parent_struct.offset + field->rel_offset, false);
            xx_var_set_i64(&record->value, value);
        } else if (field->size == 4) {
            value = xx_io_get_i32(format->device,
                                  state->parent_struct.offset +
                                      field->rel_offset,
                                  false);
            xx_var_set_i32(&record->value, (int32_t)value);
        } else {
            value = xx_io_get_i16(format->device,
                                  state->parent_struct.offset +
                                      field->rel_offset,
                                  false);
            xx_var_set_i16(&record->value, (int16_t)value);
        }
        pe_display_i64(display, PE_COUNT(display), value);
        return pe_finish_record(record, field, display);
    }
    return xx_data_struct_record_populate(
        &state->current_record, format->device,
        state->parent_struct.offset, field, false);
}

xx_data_struct_record_state *xx_pe_create_data_struct_records_reading(
    Abstractformat *format, const xx_data_struct *ds, xx_pd_struct *pd) {
    const xx_data_struct_field_desc *static_fields = NULL;
    size_t static_count = 0U;
    size_t valid_count = 0U;
    size_t extra_count = 0U;
    size_t index;
    int64_t limit;
    bool simple_ascii;
    bool import_name;
    bool resource_name;
    bool pogo_entry;
    bool clr_stream;
    xx_data_struct_record_state *state = NULL;
    pe_record_stream *stream = NULL;
    xx_pe *pe = (xx_pe *)format;
    if (!format || !format->device || !ds || xx_pd_is_stopped(pd) ||
        ds->offset < 0 || ds->total_size <= 0)
        return NULL;
    simple_ascii = ds->id == XX_PE_DATA_STRUCT_COFF_STRING ||
                   ds->id == XX_PE_DATA_STRUCT_EXPORT_FORWARDER ||
                   ds->id == XX_PE_DATA_STRUCT_ASCII_STRING ||
                   ds->id == XX_PE_DATA_STRUCT_DEBUG_PDB_CHECKSUM;
    import_name = ds->id == XX_PE_DATA_STRUCT_IMPORT_BY_NAME;
    resource_name = ds->id == XX_PE_DATA_STRUCT_RESOURCE_STRING;
    pogo_entry = ds->id == XX_PE_DATA_STRUCT_DEBUG_POGO_ENTRY;
    clr_stream = ds->id == XX_PE_DATA_STRUCT_CLR_STREAM_HEADER;
    (void)pe_static_fields(pe, ds, &static_fields, &static_count);
    limit = ds->entry_size > 0 && ds->entry_size < ds->total_size
                ? ds->entry_size : ds->total_size;
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 &&
            static_fields[index].rel_offset <= limit &&
            static_fields[index].size <=
                limit - static_fields[index].rel_offset)
            ++valid_count;
    }
    if (simple_ascii) extra_count = 1U;
    else if (import_name || resource_name) extra_count = 2U;
    else if (pogo_entry || clr_stream) extra_count = 1U;
    if (valid_count == 0U && extra_count == 0U) return NULL;
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (pe_record_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) goto fail;
    stream->fields = (xx_data_struct_field_desc *)xx_mem_alloc(
        (valid_count + extra_count) * sizeof(*stream->fields));
    if (!stream->fields) goto fail;
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 &&
            static_fields[index].rel_offset <= limit &&
            static_fields[index].size <=
                limit - static_fields[index].rel_offset)
            stream->fields[stream->count++] = static_fields[index];
    }
    if (simple_ascii) {
        xx_data_struct_field_desc *field = &stream->fields[stream->count++];
        field->name = ds->id == XX_PE_DATA_STRUCT_DEBUG_PDB_CHECKSUM
                          ? L"AlgorithmName" : L"String";
        field->type = L"char[]";
        field->rel_offset = 0;
        field->size = ds->total_size;
        field->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
    } else if (import_name) {
        xx_data_struct_field_desc *hint = &stream->fields[stream->count++];
        xx_data_struct_field_desc *name = &stream->fields[stream->count++];
        hint->name = L"Hint";
        hint->type = L"uint16";
        hint->rel_offset = 0;
        hint->size = 2;
        hint->property = XX_DATA_STRUCT_RECORD_PROPERTY_ID;
        name->name = L"Name";
        name->type = L"char[]";
        name->rel_offset = 2;
        name->size = ds->total_size - 2;
        name->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
    } else if (resource_name) {
        xx_data_struct_field_desc *length = &stream->fields[stream->count++];
        xx_data_struct_field_desc *name = &stream->fields[stream->count++];
        length->name = L"Length";
        length->type = L"uint16";
        length->rel_offset = 0;
        length->size = 2;
        length->property = XX_DATA_STRUCT_RECORD_PROPERTY_COUNT;
        name->name = L"NameString";
        name->type = L"wchar16[]";
        name->rel_offset = 2;
        name->size = ds->total_size - 2;
        name->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
        stream->unicode_string = true;
    } else if (pogo_entry || clr_stream) {
        xx_data_struct_field_desc *name = &stream->fields[stream->count++];
        name->name = pogo_entry ? L"Name" : L"StreamName";
        name->type = L"char[]";
        name->rel_offset = 8;
        name->size = ds->total_size > 8 ? ds->total_size - 8 : 0;
        name->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
    }
    xx_data_struct_record_state_init(state, format, ds);
    state->internal_state = stream;
    state->free_internal = pe_record_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pe_populate_record(format, state, 0U)) goto fail_state;
    state->current_index = 0;
    state->has_record = true;
    return state;
fail_state:
    xx_data_struct_record_state_free(state);
    return NULL;
fail:
    if (stream) pe_record_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

const xx_data_struct_record *xx_pe_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_pe_data_struct_record_move_to_next(
    Abstractformat *format, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    size_t next;
    if (!format || !state || state->format != format ||
        xx_pd_is_stopped(pd)) {
        if (state) state->has_record = false;
        return false;
    }
    next = (size_t)(state->current_index + 1);
    if (state->current_index < 0 || next >= (size_t)state->total_records) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!pe_populate_record(format, state, next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

void xx_pe_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state) {
    (void)format;
    xx_data_struct_record_state_free(state);
}

void xx_pe_setup_data_struct_callbacks(xx_pe *pe) {
    if (!pe) return;
    pe->format.data_struct_id_to_string = xx_pe_data_struct_id_to_string;
    pe->format.data_struct_string_to_id = xx_pe_data_struct_string_to_id;
    pe->format.create_data_structs_reading =
        xx_pe_create_data_structs_reading;
    pe->format.get_current_data_struct = xx_pe_get_current_data_struct;
    pe->format.data_struct_move_to_next =
        xx_pe_data_struct_move_to_next;
    pe->format.free_data_structs_reading =
        xx_pe_free_data_structs_reading;
    pe->format.create_data_struct_records_reading =
        xx_pe_create_data_struct_records_reading;
    pe->format.get_current_data_struct_record =
        xx_pe_get_current_data_struct_record;
    pe->format.data_struct_record_move_to_next =
        xx_pe_data_struct_record_move_to_next;
    pe->format.free_data_struct_records_reading =
        xx_pe_free_data_struct_records_reading;
}
