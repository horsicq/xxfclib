/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_macho_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#define XX_MACHO_DS_HEADER32_SIZE       28U
#define XX_MACHO_DS_HEADER64_SIZE       32U
#define XX_MACHO_DS_LOAD_SIZE            8U
#define XX_MACHO_DS_SEGMENT32_SIZE      56U
#define XX_MACHO_DS_SEGMENT64_SIZE      72U
#define XX_MACHO_DS_SECTION32_SIZE      68U
#define XX_MACHO_DS_SECTION64_SIZE      80U
#define XX_MACHO_DS_MAX_ITEMS       262144U
#define XX_MACHO_DS_MAX_SCAN       1048576U
#define XX_MACHO_DS_MAX_ROWS         65536U

static int xx_macho_display_from_ascii(wchar_t *destination,
                                       size_t capacity,
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

static void xx_macho_display_i64(wchar_t *destination, size_t capacity,
                                 int64_t value) {
    char ascii[64];
    int length = xx_rt_snprintf(ascii, sizeof(ascii), "%lld",
                                (long long)value);
    if (length < 0 || (size_t)length >= sizeof(ascii) ||
        xx_macho_display_from_ascii(destination, capacity, ascii) < 0)
        destination[0] = L'\0';
}

static void xx_macho_display_u64(wchar_t *destination, size_t capacity,
                                 uint64_t value) {
    char ascii[64];
    int length = xx_rt_snprintf(ascii, sizeof(ascii), "%llu",
                                (unsigned long long)value);
    if (length < 0 || (size_t)length >= sizeof(ascii) ||
        xx_macho_display_from_ascii(destination, capacity, ascii) < 0)
        destination[0] = L'\0';
}

#define XX_MACHO_CHAIN_START_NONE  UINT16_C(0xffff)
#define XX_MACHO_CHAIN_START_MULTI UINT16_C(0x8000)
#define XX_MACHO_CHAIN_START_LAST  UINT16_C(0x8000)
#define XX_MACHO_CHAIN_STARTS_USE_FILE_OFFSET UINT32_C(1)
#define XX_MACHO_CHAIN_STARTS_USE_VM_OFFSET   UINT32_C(2)

#define XX_MACHO_CPU_TYPE_I386       UINT32_C(7)
#define XX_MACHO_CPU_TYPE_X86_64     UINT32_C(0x01000007)
#define XX_MACHO_CPU_TYPE_ARM        UINT32_C(12)
#define XX_MACHO_CPU_TYPE_ARM64      UINT32_C(0x0100000c)
#define XX_MACHO_CPU_TYPE_ARM64_32   UINT32_C(0x0200000c)
#define XX_MACHO_CPU_TYPE_PPC        UINT32_C(18)
#define XX_MACHO_CPU_TYPE_PPC64      UINT32_C(0x01000012)

#define XX_MACHO_I386_THREAD_STATE_FLAVOR   UINT32_C(1)
#define XX_MACHO_X86_THREAD_STATE64_FLAVOR  UINT32_C(4)
#define XX_MACHO_X86_FLOAT_STATE64_FLAVOR   UINT32_C(5)
#define XX_MACHO_X86_EXCEPTION_STATE64_FLAVOR UINT32_C(6)
#define XX_MACHO_X86_THREAD_STATE_FLAVOR    UINT32_C(7)
#define XX_MACHO_X86_FLOAT_STATE_FLAVOR     UINT32_C(8)
#define XX_MACHO_X86_EXCEPTION_STATE_FLAVOR UINT32_C(9)
#define XX_MACHO_ARM_THREAD_STATE_FLAVOR    UINT32_C(1)
#define XX_MACHO_ARM_THREAD_STATE64_FLAVOR  UINT32_C(6)
#define XX_MACHO_PPC_THREAD_STATE_FLAVOR    UINT32_C(1)
#define XX_MACHO_PPC_THREAD_STATE64_FLAVOR  UINT32_C(5)

#define XX_MACHO_CS_MAGIC_EMBEDDED_SIGNATURE     UINT32_C(0xfade0cc0)
#define XX_MACHO_CS_MAGIC_EMBEDDED_SIGNATURE_OLD UINT32_C(0xfade0b02)
#define XX_MACHO_CS_MAGIC_DETACHED_SIGNATURE     UINT32_C(0xfade0cc1)
#define XX_MACHO_CS_MAGIC_REQUIREMENT          UINT32_C(0xfade0c00)
#define XX_MACHO_CS_MAGIC_REQUIREMENTS         UINT32_C(0xfade0c01)
#define XX_MACHO_CS_MAGIC_CODEDIRECTORY       UINT32_C(0xfade0c02)

#define XX_MACHO_SECTION_TYPE_MASK             UINT32_C(0x000000ff)
#define XX_MACHO_SECTION_THREAD_LOCAL_VARIABLES UINT32_C(0x00000013)
#define XX_MACHO_RELOCATION_SCATTERED          UINT32_C(0x80000000)

#define XX_MACHO_FIELDS_COUNT(array_) \
    (sizeof(array_) / sizeof((array_)[0]))
#define XX_MACHO_FIELD(name_, type_, offset_, size_, property_) \
    {L##name_, L##type_, offset_, size_, property_}

typedef struct xx_macho_data_stream_s {
    xx_data_struct *items;
    size_t count;
    size_t capacity;
} xx_macho_data_stream;

typedef struct xx_macho_record_stream_s {
    xx_data_struct_field_desc *fields;
    size_t count;
    bool big_endian;
} xx_macho_record_stream;

static const char *const xx_macho_data_struct_names[] = {
    "UNKNOWN",
    "mach_header",
    "mach_header_64",
    "load_command",
    "segment_command",
    "segment_command_64",
    "section",
    "section_64",
    "dylib_command",
    "dylinker_command",
    "rpath_command",
    "symtab_command",
    "dysymtab_command",
    "uuid_command",
    "version_min_command",
    "build_version_command",
    "source_version_command",
    "entry_point_command",
    "encryption_info_command",
    "encryption_info_command_64",
    "linkedit_data_command",
    "dyld_info_command",
    "nlist",
    "nlist_64",
    "string_table",
    "indirect_symbol",
    "function_starts",
    "data_in_code",
    "chained_fixups_header",
    "chained_import",
    "export",
    "symseg_command",
    "thread_command",
    "thread_state_header",
    "fvmlib_command",
    "fvmfile_command",
    "ident_command",
    "prebound_dylib_command",
    "routines_command",
    "routines_command_64",
    "sub_framework_command",
    "sub_umbrella_command",
    "sub_client_command",
    "sub_library_command",
    "twolevel_hints_command",
    "prebind_cksum_command",
    "linker_option_command",
    "note_command",
    "build_tool_version",
    "fileset_entry_command",
    "dylib_module",
    "dylib_module_64",
    "dylib_table_of_contents",
    "dylib_reference",
    "relocation_info",
    "twolevel_hint",
    "chained_starts_in_image",
    "chained_starts_in_segment",
    "chained_import_addend",
    "chained_import_addend64",
    "code_signature_superblob",
    "code_signature_blob_index",
    "code_signature_code_directory",
    "target_triple_command",
    "chained_seg_info_offset",
    "chained_page_start",
    "code_signature_blob",
    "thread_state_word",
    "x86_thread_state32",
    "x86_thread_state64",
    "arm_thread_state32",
    "arm_thread_state64",
    "ppc_thread_state32",
    "ppc_thread_state64",
    "dyld_chained_starts_offsets",
    "chain_start_offset",
    "chained_chain_start",
    "tlv_descriptor",
    "tlv_descriptor_64",
    "code_signature_scatter",
    "scattered_relocation_info",
    "code_signature_requirements",
    "code_signature_requirement",
    "dyld_chained_ptr_arm64e_rebase",
    "dyld_chained_ptr_arm64e_bind",
    "dyld_chained_ptr_arm64e_auth_rebase",
    "dyld_chained_ptr_arm64e_auth_bind",
    "dyld_chained_ptr_arm64e_bind24",
    "dyld_chained_ptr_arm64e_auth_bind24",
    "dyld_chained_ptr_64_rebase",
    "dyld_chained_ptr_64_bind",
    "dyld_chained_ptr_64_kernel_cache_rebase",
    "dyld_chained_ptr_32_rebase",
    "dyld_chained_ptr_32_bind",
    "dyld_chained_ptr_32_cache_rebase",
    "dyld_chained_ptr_32_firmware_rebase",
    "dyld_chained_ptr_arm64e_shared_cache_rebase",
    "dyld_chained_ptr_arm64e_shared_cache_auth_rebase",
    "dyld_chained_ptr_arm64e_segmented_rebase",
    "dyld_chained_ptr_arm64e_auth_segmented_rebase",
    "x86_float_state64",
    "x86_exception_state64"
};

_Static_assert(XX_MACHO_FIELDS_COUNT(xx_macho_data_struct_names) ==
                   (size_t)XX_MACHO_DATA_STRUCT_LAST + 1U,
               "Mach-O data-structure name table is out of sync");

static const xx_data_struct_field_desc xx_macho_header32_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cputype", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cpusubtype", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("filetype", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("ncmds", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("sizeofcmds", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("flags", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_header64_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cputype", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cpusubtype", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("filetype", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("ncmds", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("sizeofcmds", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("flags", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("reserved", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_load_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_segment32_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("segname", "char[16]", 8, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("vmaddr", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("vmsize", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("fileoff", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("filesize", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("maxprot", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("initprot", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("nsects", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("flags", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_segment64_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("segname", "char[16]", 8, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("vmaddr", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("vmsize", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("fileoff", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("filesize", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("maxprot", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("initprot", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("nsects", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("flags", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_section32_fields[] = {
    XX_MACHO_FIELD("sectname", "char[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("segname", "char[16]", 16, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("addr", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("size", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("offset", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("align", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("reloff", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nreloc", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("flags", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("reserved1", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved2", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_section64_fields[] = {
    XX_MACHO_FIELD("sectname", "char[16]", 0, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("segname", "char[16]", 16, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("addr", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("size", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("offset", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("align", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("reloff", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nreloc", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("flags", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("reserved1", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved2", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved3", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_dylib_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("dylib.name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("dylib.timestamp", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    XX_MACHO_FIELD("dylib.current_version", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("dylib.compatibility_version", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_dylinker_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_rpath_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("path", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_target_triple_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("triple", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_symtab_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("symoff", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nsyms", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("stroff", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("strsize", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_dysymtab_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("ilocalsym", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nlocalsym", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iextdefsym", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nextdefsym", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iundefsym", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nundefsym", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("tocoff", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("ntoc", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("modtaboff", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nmodtab", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("extrefsymoff", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nextrefsyms", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("indirectsymoff", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nindirectsyms", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("extreloff", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nextrel", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("locreloff", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nlocrel", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_uuid_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("uuid", "uint8[16]", 8, 16, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_version_min_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("version", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("sdk", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_build_version_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("platform", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("minos", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("sdk", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ntools", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_source_version_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("version", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_entry_point_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("entryoff", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("stacksize", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_encryption32_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("cryptoff", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("cryptsize", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("cryptid", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_encryption64_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("cryptoff", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("cryptsize", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("cryptid", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("pad", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_linkedit_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("dataoff", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("datasize", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_dyld_info_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("rebase_off", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("rebase_size", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("bind_off", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("bind_size", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("weak_bind_off", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("weak_bind_size", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("lazy_bind_off", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("lazy_bind_size", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("export_off", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("export_size", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_nlist32_fields[] = {
    XX_MACHO_FIELD("n_strx", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("n_type", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("n_sect", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("n_desc", "int16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("n_value", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_nlist64_fields[] = {
    XX_MACHO_FIELD("n_strx", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("n_type", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("n_sect", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("n_desc", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("n_value", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_indirect_symbol_fields[] = {
    XX_MACHO_FIELD("symbol_index", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_data_in_code_fields[] = {
    XX_MACHO_FIELD("offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("length", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("kind", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_chained_header_fields[] = {
    XX_MACHO_FIELD("fixups_version", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("starts_offset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("imports_offset", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("symbols_offset", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("imports_count", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("imports_format", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("symbols_format", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_chained_import_fields[] = {
    XX_MACHO_FIELD("import", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("lib_ordinal", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("weak_import", "bool", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("name_offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_chained_import_addend_fields[] = {
    XX_MACHO_FIELD("import", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("lib_ordinal", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("weak_import", "bool", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("name_offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("addend", "int32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_chained_import_addend64_fields[] = {
    XX_MACHO_FIELD("import", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("lib_ordinal", "uint16", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("weak_import", "bool", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("reserved", "uint16", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("name_offset", "uint32", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("addend", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_symseg_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("offset", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("size", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_thread_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_thread_state_header_fields[] = {
    XX_MACHO_FIELD("flavor", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("count", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_thread_state_word_fields[] = {
    XX_MACHO_FIELD("word", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_x86_thread_state32_fields[] = {
    XX_MACHO_FIELD("eax", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ebx", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ecx", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("edx", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("edi", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("esi", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ebp", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("esp", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ss", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("eflags", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("eip", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("cs", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("ds", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("es", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fs", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("gs", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_x86_thread_state64_fields[] = {
    XX_MACHO_FIELD("rax", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rbx", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rcx", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rdx", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rdi", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rsi", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rbp", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rsp", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r8", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r9", "uint64", 72, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r10", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r11", "uint64", 88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r12", "uint64", 96, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r13", "uint64", 104, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r14", "uint64", 112, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r15", "uint64", 120, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("rip", "uint64", 128, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("rflags", "uint64", 136, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("cs", "uint64", 144, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fs", "uint64", 152, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("gs", "uint64", 160, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_x86_float_state64_fields[] = {
    XX_MACHO_FIELD("fpu_reserved[0]", "int32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_reserved[1]", "int32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_fcw", "uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("fpu_fsw", "uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("fpu_ftw", "uint8", 12, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("fpu_rsrv1", "uint8", 13, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_fop", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_ip", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_cs", "uint16", 20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_rsrv2", "uint16", 22, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_dp", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_ds", "uint16", 28, 2, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_rsrv3", "uint16", 30, 2, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_mxcsr", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("fpu_mxcsrmask", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("fpu_stmm0", "uint8[16]", 40, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm1", "uint8[16]", 56, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm2", "uint8[16]", 72, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm3", "uint8[16]", 88, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm4", "uint8[16]", 104, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm5", "uint8[16]", 120, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm6", "uint8[16]", 136, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_stmm7", "uint8[16]", 152, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm0", "uint8[16]", 168, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm1", "uint8[16]", 184, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm2", "uint8[16]", 200, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm3", "uint8[16]", 216, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm4", "uint8[16]", 232, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm5", "uint8[16]", 248, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm6", "uint8[16]", 264, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm7", "uint8[16]", 280, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm8", "uint8[16]", 296, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm9", "uint8[16]", 312, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm10", "uint8[16]", 328, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm11", "uint8[16]", 344, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm12", "uint8[16]", 360, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm13", "uint8[16]", 376, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm14", "uint8[16]", 392, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_xmm15", "uint8[16]", 408, 16, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fpu_rsrv4", "uint8[96]", 424, 96, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("fpu_reserved1", "uint32", 520, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_x86_exception_state64_fields[] = {
    XX_MACHO_FIELD("trapno", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cpu", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("err", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("faultvaddr", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_arm_thread_state32_fields[] = {
    XX_MACHO_FIELD("r0", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r1", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r2", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r3", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r4", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r5", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r6", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r7", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r8", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r9", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r10", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r11", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r12", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("sp", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("lr", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("pc", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("cpsr", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_arm_thread_state64_fields[] = {
    XX_MACHO_FIELD("x0", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x1", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x2", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x3", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x4", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x5", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x6", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x7", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x8", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x9", "uint64", 72, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x10", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x11", "uint64", 88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x12", "uint64", 96, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x13", "uint64", 104, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x14", "uint64", 112, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x15", "uint64", 120, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x16", "uint64", 128, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x17", "uint64", 136, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x18", "uint64", 144, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x19", "uint64", 152, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x20", "uint64", 160, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x21", "uint64", 168, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x22", "uint64", 176, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x23", "uint64", 184, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x24", "uint64", 192, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x25", "uint64", 200, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x26", "uint64", 208, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x27", "uint64", 216, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("x28", "uint64", 224, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fp", "uint64", 232, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("lr", "uint64", 240, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("sp", "uint64", 248, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("pc", "uint64", 256, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("cpsr", "uint32", 264, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("flags", "uint32", 268, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ppc_thread_state32_fields[] = {
    XX_MACHO_FIELD("srr0", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("srr1", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r0", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r1", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r2", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r3", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r4", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r5", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r6", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r7", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r8", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r9", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r10", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r11", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r12", "uint32", 56, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r13", "uint32", 60, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r14", "uint32", 64, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r15", "uint32", 68, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r16", "uint32", 72, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r17", "uint32", 76, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r18", "uint32", 80, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r19", "uint32", 84, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r20", "uint32", 88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r21", "uint32", 92, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r22", "uint32", 96, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r23", "uint32", 100, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r24", "uint32", 104, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r25", "uint32", 108, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r26", "uint32", 112, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r27", "uint32", 116, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r28", "uint32", 120, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r29", "uint32", 124, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r30", "uint32", 128, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r31", "uint32", 132, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("cr", "uint32", 136, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("xer", "uint32", 140, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("lr", "uint32", 144, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("ctr", "uint32", 148, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("mq", "uint32", 152, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("vrsave", "uint32", 156, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ppc_thread_state64_fields[] = {
    XX_MACHO_FIELD("srr0", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("srr1", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r0", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r1", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r2", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r3", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r4", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r5", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r6", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r7", "uint64", 72, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r8", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r9", "uint64", 88, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r10", "uint64", 96, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r11", "uint64", 104, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r12", "uint64", 112, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r13", "uint64", 120, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r14", "uint64", 128, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r15", "uint64", 136, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r16", "uint64", 144, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r17", "uint64", 152, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r18", "uint64", 160, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r19", "uint64", 168, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r20", "uint64", 176, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r21", "uint64", 184, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r22", "uint64", 192, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r23", "uint64", 200, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r24", "uint64", 208, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r25", "uint64", 216, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r26", "uint64", 224, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r27", "uint64", 232, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r28", "uint64", 240, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r29", "uint64", 248, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r30", "uint64", 256, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("r31", "uint64", 264, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("cr", "uint32", 272, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("xer", "uint64", 276, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("lr", "uint64", 284, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("ctr", "uint64", 292, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("vrsave", "uint32", 300, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_fvmlib_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("fvmlib.name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("fvmlib.minor_version", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("fvmlib.header_addr", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_fvmfile_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("header_addr", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_prebound_dylib_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("nmodules", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("linked_modules", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_routines32_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("init_address", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("init_module", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("reserved1", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved2", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved3", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved4", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved5", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved6", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_routines64_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("init_address", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("init_module", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("reserved1", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved2", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved3", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved4", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved5", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("reserved6", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_single_string_command_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("string_offset", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_twolevel_hints_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("offset", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nhints", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_prebind_cksum_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("cksum", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_linker_option_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("count", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_note_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("data_owner", "char[16]", 8, 16, XX_DATA_STRUCT_RECORD_PROPERTY_STRING),
    XX_MACHO_FIELD("offset", "uint64", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("size", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_build_tool_fields[] = {
    XX_MACHO_FIELD("tool", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("version", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_fileset_entry_fields[] = {
    XX_MACHO_FIELD("cmd", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("cmdsize", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("vmaddr", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("fileoff", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("entry_id", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("reserved", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_dylib_module32_fields[] = {
    XX_MACHO_FIELD("module_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("iextdefsym", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nextdefsym", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("irefsym", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nrefsym", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("ilocalsym", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nlocalsym", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iextrel", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nextrel", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iinit_iterm", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("iinit", "uint16", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("iterm", "uint16", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("ninit_nterm", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("ninit", "uint16", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("nterm", "uint16", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("objc_module_info_addr", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("objc_module_info_size", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_dylib_module64_fields[] = {
    XX_MACHO_FIELD("module_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_FIELD("iextdefsym", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nextdefsym", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("irefsym", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nrefsym", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("ilocalsym", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nlocalsym", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iextrel", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("nextrel", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("iinit_iterm", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("iinit", "uint16", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("iterm", "uint16", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("ninit_nterm", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("ninit", "uint16", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("nterm", "uint16", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("objc_module_info_size", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("objc_module_info_addr", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_toc_fields[] = {
    XX_MACHO_FIELD("symbol_index", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("module_index", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc xx_macho_reference_fields[] = {
    XX_MACHO_FIELD("reference", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("isym", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("flags", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_relocation_fields[] = {
    XX_MACHO_FIELD("r_address", "int32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("r_info", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_symbolnum", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("r_pcrel", "bool", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_length", "uint8", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("r_extern", "bool", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_type", "uint8", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_scattered_relocation_fields[] = {
    XX_MACHO_FIELD("r_word0", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_scattered", "bool", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_pcrel", "bool", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("r_length", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("r_type", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("r_address", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("r_value", "int32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};

static const xx_data_struct_field_desc xx_macho_twolevel_hint_fields[] = {
    XX_MACHO_FIELD("hint", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("isub_image", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("itoc", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc xx_macho_tlv32_fields[] = {
    XX_MACHO_FIELD("thunk", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("key", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("offset", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_tlv64_fields[] = {
    XX_MACHO_FIELD("thunk", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    XX_MACHO_FIELD("key", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("offset", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_chained_starts_image_fields[] = {
    XX_MACHO_FIELD("seg_count", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_chained_starts_segment_fields[] = {
    XX_MACHO_FIELD("size", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("page_size", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("pointer_format", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("segment_offset", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("max_valid_pointer", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("page_count", "uint16", 20, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_chained_seg_info_offset_fields[] = {
    XX_MACHO_FIELD("seg_info_offset", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_chained_page_start_fields[] = {
    XX_MACHO_FIELD("page_start", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_chained_chain_start_fields[] = {
    XX_MACHO_FIELD("chain_start", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

#define XX_MACHO_CHAIN_FIELD(name_, type_, width_, property_) \
    XX_MACHO_FIELD(name_, type_, 0, width_, property_)

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("high8", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_bind_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("zero", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("addend", "int32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_auth_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("key", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_auth_bind_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("zero", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("key", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_bind24_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("zero", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("addend", "int32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_arm64e_auth_bind24_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("zero", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("key", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_64_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("high8", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("reserved", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_64_bind_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("addend", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("reserved", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_64_kernel_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("cacheLevel", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("key", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("isAuth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_32_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("next", "uint8", 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_32_bind_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("ordinal", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("addend", "uint8", 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("next", "uint8", 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("bind", "bool", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_32_cache_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("next", "uint8", 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_ptr_32_firmware_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("target", "uint32", 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("next", "uint8", 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_ptr_shared_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("runtimeOffset", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("high8", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("unused", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_shared_auth_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("runtimeOffset", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("keyIsData", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_segmented_rebase_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("targetSegOffset", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("targetSegIndex", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("padding", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

static const xx_data_struct_field_desc xx_macho_ptr_segmented_auth_fields[] = {
    XX_MACHO_CHAIN_FIELD("raw", "uint64", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("targetSegOffset", "uint32", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("targetSegIndex", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("diversity", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_CHAIN_FIELD("addrDiv", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_CHAIN_FIELD("key", "uint8", 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_CHAIN_FIELD("next", "uint16", 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_CHAIN_FIELD("auth", "bool", 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};

#undef XX_MACHO_CHAIN_FIELD

static const xx_data_struct_field_desc xx_macho_chained_starts_offsets_fields[] = {
    XX_MACHO_FIELD("pointer_format", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("starts_count", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_chain_start_offset_fields[] = {
    XX_MACHO_FIELD("chain_start", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};

static const xx_data_struct_field_desc xx_macho_cs_superblob_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("count", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static const xx_data_struct_field_desc xx_macho_cs_blob_index_fields[] = {
    XX_MACHO_FIELD("type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("offset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};

static const xx_data_struct_field_desc xx_macho_cs_blob_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_cs_code_directory_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("version", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("flags", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("hashOffset", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("identOffset", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("nSpecialSlots", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("nCodeSlots", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("codeLimit", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("hashSize", "uint8", 36, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("hashType", "uint8", 37, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("platform", "uint8", 38, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("pageSize", "uint8", 39, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("spare2", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("scatterOffset", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("teamOffset", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("spare3", "uint32", 52, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    XX_MACHO_FIELD("codeLimit64", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("execSegBase", "uint64", 64, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("execSegLimit", "uint64", 72, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("execSegFlags", "uint64", 80, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    XX_MACHO_FIELD("runtime", "uint32", 88, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("preEncryptOffset", "uint32", 92, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("linkageHashType", "uint8", 96, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("linkageApplicationType", "uint8", 97, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("linkageApplicationSubType", "uint16", 98, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("linkageOffset", "uint32", 100, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    XX_MACHO_FIELD("linkageSize", "uint32", 104, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc xx_macho_cs_scatter_fields[] = {
    XX_MACHO_FIELD("count", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    XX_MACHO_FIELD("base", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("targetOffset", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    XX_MACHO_FIELD("spare", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED)
};

static const xx_data_struct_field_desc xx_macho_cs_requirement_fields[] = {
    XX_MACHO_FIELD("magic", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    XX_MACHO_FIELD("length", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    XX_MACHO_FIELD("kind", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static uint32_t xx_macho_data_u32(const xx_macho *macho, int64_t offset) {
    return xx_io_get_u32(macho->format.device, offset,
                         macho->format.endian == XX_ENDIAN_BIG);
}

static uint16_t xx_macho_data_u16(const xx_macho *macho, int64_t offset) {
    return xx_io_get_u16(macho->format.device, offset,
                         macho->format.endian == XX_ENDIAN_BIG);
}

static uint64_t xx_macho_data_u64(const xx_macho *macho, int64_t offset) {
    return xx_io_get_u64(macho->format.device, offset,
                         macho->format.endian == XX_ENDIAN_BIG);
}

static bool xx_macho_data_range(const xx_macho *macho, uint64_t relative,
                                uint64_t size) {
    int64_t total;
    uint64_t available;
    if (!macho || !macho->format.device || macho->format.base_address < 0)
        return false;
    total = xx_io_total_size(macho->format.device);
    if (total < macho->format.base_address) return false;
    available = (uint64_t)(total - macho->format.base_address);
    return relative <= available && size <= available - relative;
}

static bool xx_macho_data_product(uint64_t count, uint64_t size,
                                  uint64_t *result) {
    if (!result || (size != 0U && count > UINT64_MAX / size)) return false;
    *result = count * size;
    return true;
}

static void xx_macho_data_stream_free(void *pointer) {
    xx_macho_data_stream *stream = (xx_macho_data_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_macho_data_append(xx_macho *macho,
                                 xx_macho_data_stream *stream,
                                 uint32_t id, uint64_t relative_offset,
                                 uint64_t entry_size, uint64_t total_size,
                                 uint64_t count,
                                 xx_data_struct_type_t type) {
    xx_data_struct *grown;
    xx_data_struct *item;
    size_t next_capacity;
    if (!macho || !stream || stream->count >= XX_MACHO_DS_MAX_ITEMS ||
        relative_offset > (uint64_t)(INT64_MAX - macho->format.base_address) ||
        entry_size > INT64_MAX || total_size > INT64_MAX ||
        !xx_macho_data_range(macho, relative_offset, total_size)) {
        return false;
    }
    if (stream->count == stream->capacity) {
        next_capacity = stream->capacity ? stream->capacity * 2U : 32U;
        if (next_capacity < stream->capacity ||
            next_capacity > XX_MACHO_DS_MAX_ITEMS)
            next_capacity = XX_MACHO_DS_MAX_ITEMS;
        grown = (xx_data_struct *)xx_mem_realloc(
            stream->items, next_capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = next_capacity;
    }
    item = &stream->items[stream->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = macho->format.base_address + (int64_t)relative_offset;
    item->address = -1;
    item->entry_size = (int64_t)entry_size;
    item->total_size = (int64_t)total_size;
    item->count = count;
    item->type = type;
    return true;
}

static bool xx_macho_data_append_table(xx_macho *macho,
                                       xx_macho_data_stream *stream,
                                       uint32_t id, uint64_t offset,
                                       uint64_t count, uint64_t row_size) {
    uint64_t total_size;
    if (count == 0U) return true;
    return xx_macho_data_product(count, row_size, &total_size) &&
           xx_macho_data_append(macho, stream, id, offset, row_size,
                                total_size, count,
                                XX_DATA_STRUCT_TYPE_ENTRY);
}

static bool xx_macho_data_append_raw(xx_macho *macho,
                                     xx_macho_data_stream *stream,
                                     uint64_t offset, uint64_t size) {
    return size == 0U ||
           xx_macho_data_append(macho, stream, XX_DATA_STRUCT_ID_RAW_DATA,
                                offset, size, size, 1U,
                                XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static uint64_t xx_macho_count_strings(xx_macho *macho, uint64_t offset,
                                       uint64_t size, xx_pd_struct *pd) {
    uint64_t index;
    uint64_t count = 0U;
    uint64_t limit = size > XX_MACHO_DS_MAX_SCAN
                         ? XX_MACHO_DS_MAX_SCAN : size;
    bool starts_row = true;
    if (!xx_macho_data_range(macho, offset, size)) return 0U;
    for (index = 0U; index < limit && count < XX_MACHO_DS_MAX_ROWS; ++index) {
        uint8_t value;
        if (xx_pd_is_stopped(pd)) return 0U;
        if (starts_row) {
            ++count;
            starts_row = false;
        }
        value = xx_io_get_u8(macho->format.device,
                             macho->format.base_address +
                                 (int64_t)(offset + index));
        if (value == 0U) starts_row = true;
    }
    return count;
}

static bool xx_macho_read_uleb(xx_macho *macho, uint64_t blob_offset,
                               uint64_t blob_size, uint64_t *cursor,
                               uint64_t *value) {
    uint64_t result = 0U;
    unsigned shift = 0U;
    unsigned count;
    if (!macho || !cursor || !value) return false;
    for (count = 0U; count < 10U && *cursor < blob_size; ++count) {
        uint8_t byte = xx_io_get_u8(
            macho->format.device,
            macho->format.base_address +
                (int64_t)(blob_offset + *cursor));
        ++*cursor;
        if (shift == 63U && (byte & UINT8_C(0x7e)) != 0U) return false;
        result |= ((uint64_t)(byte & UINT8_C(0x7f))) << shift;
        if ((byte & UINT8_C(0x80)) == 0U) {
            *value = result;
            return true;
        }
        shift += 7U;
    }
    return false;
}

static uint64_t xx_macho_count_function_starts(xx_macho *macho,
                                                uint64_t offset,
                                                uint64_t size,
                                                xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    uint64_t count = 0U;
    uint64_t address = 0U;
    uint64_t limit = size > XX_MACHO_DS_MAX_SCAN
                         ? XX_MACHO_DS_MAX_SCAN : size;
    while (cursor < limit && count < XX_MACHO_DS_MAX_ROWS) {
        uint64_t delta;
        if (xx_pd_is_stopped(pd) ||
            !xx_macho_read_uleb(macho, offset, limit, &cursor, &delta))
            break;
        if (delta == 0U) break;
        if (address > UINT64_MAX - delta) break;
        address += delta;
        ++count;
    }
    return count;
}

static uint64_t xx_macho_count_exports(xx_macho *macho, uint64_t offset,
                                       uint64_t size, xx_pd_struct *pd) {
    uint8_t *visited = NULL;
    uint64_t *stack = NULL;
    uint64_t stack_count = 0U;
    uint64_t result = 0U;
    uint64_t limit = size > XX_MACHO_DS_MAX_SCAN
                         ? XX_MACHO_DS_MAX_SCAN : size;
    if (limit == 0U || !xx_macho_data_range(macho, offset, size) ||
        limit > SIZE_MAX) return 0U;
    visited = (uint8_t *)xx_mem_calloc((size_t)limit, 1U);
    stack = (uint64_t *)xx_mem_alloc(
        (size_t)XX_MACHO_DS_MAX_ROWS * sizeof(*stack));
    if (!visited || !stack) goto cleanup;
    stack[stack_count++] = 0U;
    while (stack_count != 0U && result < XX_MACHO_DS_MAX_ROWS) {
        uint64_t node = stack[--stack_count];
        uint64_t cursor = node;
        uint64_t terminal_size;
        uint64_t terminal_end;
        uint8_t children;
        uint8_t child_index;
        if (xx_pd_is_stopped(pd)) break;
        if (node >= limit || visited[node]) continue;
        visited[node] = 1U;
        if (!xx_macho_read_uleb(macho, offset, limit, &cursor,
                                &terminal_size) ||
            terminal_size > limit - cursor)
            continue;
        terminal_end = cursor + terminal_size;
        if (terminal_size != 0U) ++result;
        cursor = terminal_end;
        if (cursor >= limit) continue;
        children = xx_io_get_u8(macho->format.device,
                                macho->format.base_address +
                                    (int64_t)(offset + cursor));
        ++cursor;
        for (child_index = 0U; child_index < children; ++child_index) {
            uint64_t child;
            bool terminated = false;
            while (cursor < limit) {
                uint8_t byte = xx_io_get_u8(
                    macho->format.device,
                    macho->format.base_address +
                        (int64_t)(offset + cursor++));
                if (byte == 0U) {
                    terminated = true;
                    break;
                }
            }
            if (!terminated ||
                !xx_macho_read_uleb(macho, offset, limit, &cursor, &child))
                break;
            if (child < limit && stack_count < XX_MACHO_DS_MAX_ROWS)
                stack[stack_count++] = child;
        }
    }
cleanup:
    if (visited) xx_mem_free(visited);
    if (stack) xx_mem_free(stack);
    return result;
}

static bool xx_macho_append_symtab_children(xx_macho *macho,
                                             xx_macho_data_stream *stream,
                                             int64_t command_offset,
                                             xx_pd_struct *pd) {
    uint32_t symoff = xx_macho_data_u32(macho, command_offset + 8);
    uint32_t nsyms = xx_macho_data_u32(macho, command_offset + 12);
    uint32_t stroff = xx_macho_data_u32(macho, command_offset + 16);
    uint32_t strsize = xx_macho_data_u32(macho, command_offset + 20);
    uint64_t symbol_size = macho->is_64 ? 16U : 12U;
    uint64_t string_count;
    if (!xx_macho_data_append_table(
            macho, stream,
            macho->is_64 ? XX_MACHO_DATA_STRUCT_NLIST_64
                         : XX_MACHO_DATA_STRUCT_NLIST,
            symoff, nsyms, symbol_size))
        return false;
    if (strsize == 0U) return true;
    if (!xx_macho_data_range(macho, stroff, strsize)) return false;
    string_count = xx_macho_count_strings(macho, stroff, strsize, pd);
    if (xx_pd_is_stopped(pd)) return false;
    return xx_macho_data_append(macho, stream,
                                XX_MACHO_DATA_STRUCT_STRING_TABLE,
                                stroff, 1U, strsize, string_count,
                                XX_DATA_STRUCT_TYPE_ENTRY);
}

static bool xx_macho_append_relocation_table(
    xx_macho *macho, xx_macho_data_stream *stream, uint64_t offset,
    uint32_t count, xx_pd_struct *pd) {
    uint64_t total_size;
    uint32_t run_start;
    uint32_t index;
    bool run_scattered;
    if (count == 0U) return true;
    if (!xx_macho_data_product(count, 8U, &total_size) ||
        !xx_macho_data_range(macho, offset, total_size))
        return false;

    /* A relocation table may freely interleave the two on-disk layouts.
     * Split bounded tables into homogeneous runs so every descriptor has a
     * truthful record layout.  Very large tables stay raw rather than being
     * mislabeled or forcing an unbounded classification scan. */
    if (count > XX_MACHO_DS_MAX_ROWS || total_size > XX_MACHO_DS_MAX_SCAN)
        return xx_macho_data_append_raw(macho, stream, offset, total_size);

    run_start = 0U;
    run_scattered =
        (xx_macho_data_u32(
             macho, macho->format.base_address + (int64_t)offset) &
         XX_MACHO_RELOCATION_SCATTERED) != 0U;
    for (index = 1U; index < count; ++index) {
        bool scattered;
        if (xx_pd_is_stopped(pd)) return false;
        scattered =
            (xx_macho_data_u32(
                 macho, macho->format.base_address + (int64_t)offset +
                            (int64_t)index * 8) &
             XX_MACHO_RELOCATION_SCATTERED) != 0U;
        if (scattered == run_scattered) continue;
        if (!xx_macho_data_append_table(
                macho, stream,
                run_scattered
                    ? XX_MACHO_DATA_STRUCT_SCATTERED_RELOCATION_INFO
                    : XX_MACHO_DATA_STRUCT_RELOCATION_INFO,
                offset + (uint64_t)run_start * 8U,
                (uint64_t)index - run_start, 8U))
            return false;
        run_start = index;
        run_scattered = scattered;
    }
    return xx_macho_data_append_table(
        macho, stream,
        run_scattered ? XX_MACHO_DATA_STRUCT_SCATTERED_RELOCATION_INFO
                      : XX_MACHO_DATA_STRUCT_RELOCATION_INFO,
        offset + (uint64_t)run_start * 8U,
        (uint64_t)count - run_start, 8U);
}

static bool xx_macho_append_dysymtab_children(
    xx_macho *macho, xx_macho_data_stream *stream,
    int64_t command_offset, xx_pd_struct *pd) {
    uint32_t module_size = macho->is_64 ? 56U : 52U;
    struct xx_macho_table_pair_s {
        int offset_field;
        int count_field;
        uint32_t id;
        uint32_t row_size;
    } pairs[] = {
        {32, 36, XX_MACHO_DATA_STRUCT_DYLIB_TABLE_OF_CONTENTS, 8U},
        {40, 44, 0U, module_size},
        {48, 52, XX_MACHO_DATA_STRUCT_DYLIB_REFERENCE, 4U},
        {56, 60, XX_MACHO_DATA_STRUCT_INDIRECT_SYMBOL, 4U},
        {64, 68, XX_MACHO_DATA_STRUCT_RELOCATION_INFO, 8U},
        {72, 76, XX_MACHO_DATA_STRUCT_RELOCATION_INFO, 8U}
    };
    size_t index;
    pairs[1].id = macho->is_64 ? XX_MACHO_DATA_STRUCT_DYLIB_MODULE_64
                               : XX_MACHO_DATA_STRUCT_DYLIB_MODULE;
    for (index = 0U; index < XX_MACHO_FIELDS_COUNT(pairs); ++index) {
        uint32_t offset = xx_macho_data_u32(
            macho, command_offset + pairs[index].offset_field);
        uint32_t count = xx_macho_data_u32(
            macho, command_offset + pairs[index].count_field);
        if (pairs[index].id == XX_MACHO_DATA_STRUCT_RELOCATION_INFO) {
            if (!xx_macho_append_relocation_table(
                    macho, stream, offset, count, pd))
                return false;
        } else if (!xx_macho_data_append_table(
                       macho, stream, pairs[index].id, offset, count,
                       pairs[index].row_size)) {
            return false;
        }
    }
    return true;
}

static bool xx_macho_name16_equals(const xx_macho *macho, int64_t offset,
                                   const char *name) {
    size_t length;
    size_t index;
    if (!macho || !macho->format.device || !name) return false;
    length = xx_rt_strlen(name);
    if (length > 16U) return false;
    for (index = 0U; index < 16U; ++index) {
        uint8_t expected = index < length ? (uint8_t)name[index] : 0U;
        if (xx_io_get_u8(macho->format.device, offset + (int64_t)index) !=
            expected)
            return false;
    }
    return true;
}

static bool xx_macho_append_firmware_pointers(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t starts_offset, uint32_t pointer_format,
    uint32_t starts_count, uint32_t starts_mode, xx_pd_struct *pd);

static bool xx_macho_append_firmware_chain_starts(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t payload_offset, uint64_t payload_size,
    uint32_t starts_mode, xx_pd_struct *pd) {
    uint32_t pointer_format;
    uint32_t starts_count;
    uint64_t starts_size;
    uint64_t parsed_size;
    int64_t absolute;
    if (payload_size == 0U) return true;
    if (!xx_macho_data_range(macho, payload_offset, payload_size))
        return false;
    if (payload_size < 8U)
        return xx_macho_data_append_raw(macho, stream, payload_offset,
                                        payload_size);
    absolute = macho->format.base_address + (int64_t)payload_offset;
    pointer_format = xx_macho_data_u32(macho, absolute);
    starts_count = xx_macho_data_u32(macho, absolute + 4);
    if (!xx_macho_data_append(
            macho, stream, XX_MACHO_DATA_STRUCT_CHAINED_STARTS_OFFSETS,
            payload_offset, 8U, 8U, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (!xx_macho_data_product(starts_count, 4U, &starts_size) ||
        starts_size > payload_size - 8U)
        return xx_macho_data_append_raw(macho, stream,
                                        payload_offset + 8U,
                                        payload_size - 8U);
    if (!xx_macho_data_append_table(
            macho, stream, XX_MACHO_DATA_STRUCT_CHAIN_START_OFFSET,
            payload_offset + 8U, starts_count, 4U))
        return false;
    parsed_size = 8U + starts_size;
    if (!xx_macho_data_append_raw(macho, stream,
                                  payload_offset + parsed_size,
                                  payload_size - parsed_size))
        return false;
    return xx_macho_append_firmware_pointers(
        macho, stream, payload_offset + 8U, pointer_format,
        starts_count, starts_mode, pd);
}

static bool xx_macho_append_tlv_descriptors(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t payload_offset, uint64_t payload_size) {
    uint64_t row_size = macho->is_64 ? 24U : 12U;
    uint64_t count;
    uint64_t represented_size;
    if (payload_size == 0U) return true;
    if (!xx_macho_data_range(macho, payload_offset, payload_size))
        return false;
    count = payload_size / row_size;
    represented_size = count * row_size;
    if (count != 0U &&
        !xx_macho_data_append_table(
            macho, stream,
            macho->is_64 ? XX_MACHO_DATA_STRUCT_TLV_DESCRIPTOR_64
                         : XX_MACHO_DATA_STRUCT_TLV_DESCRIPTOR,
            payload_offset, count, row_size))
        return false;
    return xx_macho_data_append_raw(
        macho, stream, payload_offset + represented_size,
        payload_size - represented_size);
}

static bool xx_macho_append_section_children(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t command_relative, uint32_t command_size,
    xx_pd_struct *pd) {
    uint32_t segment_size = macho->is_64 ? XX_MACHO_DS_SEGMENT64_SIZE
                                         : XX_MACHO_DS_SEGMENT32_SIZE;
    uint32_t section_size = macho->is_64 ? XX_MACHO_DS_SECTION64_SIZE
                                         : XX_MACHO_DS_SECTION32_SIZE;
    int64_t command_offset = macho->format.base_address +
                             (int64_t)command_relative;
    uint32_t nsects = xx_macho_data_u32(
        macho, command_offset + (macho->is_64 ? 64 : 48));
    uint64_t section_bytes;
    uint32_t index;
    if (!xx_macho_data_product(nsects, section_size, &section_bytes) ||
        segment_size > command_size ||
        section_bytes > (uint64_t)command_size - segment_size)
        return false;
    if (nsects != 0U &&
        !xx_macho_data_append_table(
            macho, stream,
            macho->is_64 ? XX_MACHO_DATA_STRUCT_SECTION_64
                         : XX_MACHO_DATA_STRUCT_SECTION,
            command_relative + segment_size, nsects, section_size))
        return false;
    for (index = 0U;
         index < nsects && index < XX_MACHO_DS_MAX_ROWS; ++index) {
        int64_t section_offset = command_offset + segment_size +
                                 (int64_t)index * section_size;
        uint32_t reloff;
        uint32_t nreloc;
        uint32_t flags;
        uint32_t reserved1;
        uint64_t payload_size;
        uint32_t payload_offset;
        if (xx_pd_is_stopped(pd)) return false;
        reloff = xx_macho_data_u32(
            macho, section_offset + (macho->is_64 ? 56 : 48));
        nreloc = xx_macho_data_u32(
            macho, section_offset + (macho->is_64 ? 60 : 52));
        if (!xx_macho_append_relocation_table(
                macho, stream, reloff, nreloc, pd))
            return false;
        flags = xx_macho_data_u32(
            macho, section_offset + (macho->is_64 ? 64 : 56));
        payload_size = macho->is_64
                           ? xx_macho_data_u64(macho, section_offset + 40)
                           : xx_macho_data_u32(macho, section_offset + 36);
        payload_offset = xx_macho_data_u32(
            macho, section_offset + (macho->is_64 ? 48 : 40));
        reserved1 = xx_macho_data_u32(
            macho, section_offset + (macho->is_64 ? 68 : 60));
        if ((flags & XX_MACHO_SECTION_TYPE_MASK) ==
            XX_MACHO_SECTION_THREAD_LOCAL_VARIABLES) {
            if (!xx_macho_append_tlv_descriptors(
                    macho, stream, payload_offset, payload_size))
                return false;
            continue;
        }
        if (!xx_macho_name16_equals(macho, section_offset,
                                    "__chain_starts") ||
            !xx_macho_name16_equals(macho, section_offset + 16,
                                    "__TEXT"))
            continue;
        if (!xx_macho_append_firmware_chain_starts(
                macho, stream, payload_offset, payload_size,
                reserved1, pd))
            return false;
    }
    return true;
}

static uint32_t xx_macho_thread_state_id(uint32_t cpu_type,
                                         uint32_t flavor,
                                         uint32_t count) {
    switch (cpu_type) {
        case XX_MACHO_CPU_TYPE_I386:
            if (flavor == XX_MACHO_I386_THREAD_STATE_FLAVOR && count == 16U)
                return XX_MACHO_DATA_STRUCT_X86_THREAD_STATE32;
            break;
        case XX_MACHO_CPU_TYPE_X86_64:
            if (flavor == XX_MACHO_X86_THREAD_STATE64_FLAVOR && count == 42U)
                return XX_MACHO_DATA_STRUCT_X86_THREAD_STATE64;
            if (flavor == XX_MACHO_X86_FLOAT_STATE64_FLAVOR && count == 131U)
                return XX_MACHO_DATA_STRUCT_X86_FLOAT_STATE64;
            if (flavor == XX_MACHO_X86_EXCEPTION_STATE64_FLAVOR && count == 4U)
                return XX_MACHO_DATA_STRUCT_X86_EXCEPTION_STATE64;
            break;
        case XX_MACHO_CPU_TYPE_ARM:
            if (flavor == XX_MACHO_ARM_THREAD_STATE_FLAVOR && count == 17U)
                return XX_MACHO_DATA_STRUCT_ARM_THREAD_STATE32;
            break;
        case XX_MACHO_CPU_TYPE_ARM64:
        case XX_MACHO_CPU_TYPE_ARM64_32:
            if (flavor == XX_MACHO_ARM_THREAD_STATE64_FLAVOR && count == 68U)
                return XX_MACHO_DATA_STRUCT_ARM_THREAD_STATE64;
            break;
        case XX_MACHO_CPU_TYPE_PPC:
            if (flavor == XX_MACHO_PPC_THREAD_STATE_FLAVOR && count == 40U)
                return XX_MACHO_DATA_STRUCT_PPC_THREAD_STATE32;
            break;
        case XX_MACHO_CPU_TYPE_PPC64:
            if (flavor == XX_MACHO_PPC_THREAD_STATE64_FLAVOR && count == 76U)
                return XX_MACHO_DATA_STRUCT_PPC_THREAD_STATE64;
            break;
        default:
            break;
    }
    return XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD;
}

static bool xx_macho_append_x86_unified_thread_state(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint32_t outer_flavor, uint32_t outer_count,
    uint64_t state_relative, uint64_t state_size, bool *handled) {
    uint32_t expected_flavor = 0U;
    uint32_t expected_count = 0U;
    uint32_t state_id = XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD;
    uint32_t inner_flavor;
    uint32_t inner_count;
    uint64_t leaf_size;
    int64_t absolute;
    bool x86_cpu;
    if (!macho || !stream || !handled) return false;
    *handled = false;
    x86_cpu = macho->cpu_type == XX_MACHO_CPU_TYPE_I386 ||
              macho->cpu_type == XX_MACHO_CPU_TYPE_X86_64;
    if (!x86_cpu) return true;
    if (outer_flavor == XX_MACHO_X86_THREAD_STATE_FLAVOR &&
        outer_count == 44U) {
        if (macho->cpu_type == XX_MACHO_CPU_TYPE_I386) {
            expected_flavor = XX_MACHO_I386_THREAD_STATE_FLAVOR;
            expected_count = 16U;
            state_id = XX_MACHO_DATA_STRUCT_X86_THREAD_STATE32;
        } else {
            expected_flavor = XX_MACHO_X86_THREAD_STATE64_FLAVOR;
            expected_count = 42U;
            state_id = XX_MACHO_DATA_STRUCT_X86_THREAD_STATE64;
        }
    } else if (outer_flavor == XX_MACHO_X86_FLOAT_STATE_FLAVOR &&
               outer_count == 133U) {
        if (macho->cpu_type == XX_MACHO_CPU_TYPE_X86_64) {
            expected_flavor = XX_MACHO_X86_FLOAT_STATE64_FLAVOR;
            expected_count = 131U;
            state_id = XX_MACHO_DATA_STRUCT_X86_FLOAT_STATE64;
        }
    } else if (outer_flavor == XX_MACHO_X86_EXCEPTION_STATE_FLAVOR &&
               outer_count == 6U) {
        if (macho->cpu_type == XX_MACHO_CPU_TYPE_X86_64) {
            expected_flavor = XX_MACHO_X86_EXCEPTION_STATE64_FLAVOR;
            expected_count = 4U;
            state_id = XX_MACHO_DATA_STRUCT_X86_EXCEPTION_STATE64;
        }
    } else {
        return true;
    }
    *handled = true;
    if (state_size < 8U ||
        !xx_macho_data_append(
            macho, stream, XX_MACHO_DATA_STRUCT_THREAD_STATE_HEADER,
            state_relative, 8U, 8U, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    absolute = macho->format.base_address + (int64_t)state_relative;
    inner_flavor = xx_macho_data_u32(macho, absolute);
    inner_count = xx_macho_data_u32(macho, absolute + 4);
    if (state_id == XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD ||
        inner_flavor != expected_flavor || inner_count != expected_count) {
        return xx_macho_data_append_table(
            macho, stream, XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD,
            state_relative + 8U, (state_size - 8U) / 4U, 4U);
    }
    leaf_size = (uint64_t)expected_count * 4U;
    if (leaf_size > state_size - 8U ||
        !xx_macho_data_append(
            macho, stream, state_id, state_relative + 8U,
            leaf_size, leaf_size, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    return xx_macho_data_append_raw(
        macho, stream, state_relative + 8U + leaf_size,
        state_size - 8U - leaf_size);
}

static bool xx_macho_append_thread_children(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t command_relative, uint32_t command_size) {
    uint64_t cursor = 8U;
    uint32_t rows = 0U;
    int64_t command_offset = macho->format.base_address +
                             (int64_t)command_relative;
    while (cursor < command_size && rows < XX_MACHO_DS_MAX_ROWS) {
        uint32_t count;
        uint32_t flavor;
        uint32_t state_id;
        uint64_t state_size;
        bool unified_handled;
        if ((uint64_t)command_size - cursor < 8U) return false;
        flavor = xx_macho_data_u32(macho,
                                   command_offset + (int64_t)cursor);
        count = xx_macho_data_u32(macho,
                                  command_offset + (int64_t)cursor + 4);
        if (!xx_macho_data_product(count, 4U, &state_size) ||
            state_size > (uint64_t)command_size - cursor - 8U)
            return false;
        if (!xx_macho_data_append(
                macho, stream, XX_MACHO_DATA_STRUCT_THREAD_STATE_HEADER,
                command_relative + cursor, 8U, 8U, 1U,
                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        if (!xx_macho_append_x86_unified_thread_state(
                macho, stream, flavor, count,
                command_relative + cursor + 8U, state_size,
                &unified_handled))
            return false;
        state_id = xx_macho_thread_state_id(macho->cpu_type, flavor, count);
        if (unified_handled) {
            /* The unified wrapper was decomposed into its inner header,
             * typed leaf, and any fixed union tail. */
        } else if (state_id == XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD) {
            if (!xx_macho_data_append_table(
                    macho, stream, state_id,
                    command_relative + cursor + 8U, count, 4U))
                return false;
        } else if (!xx_macho_data_append(
                       macho, stream, state_id,
                       command_relative + cursor + 8U, state_size,
                       state_size, 1U, XX_DATA_STRUCT_TYPE_STRUCT)) {
            return false;
        }
        cursor += 8U + state_size;
        ++rows;
    }
    if (cursor < command_size) {
        return xx_macho_data_append_raw(
            macho, stream, command_relative + cursor,
            (uint64_t)command_size - cursor);
    }
    return true;
}

static bool xx_macho_append_generic_code_signature_blob(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t offset, uint32_t length) {
    return length >= 8U &&
           xx_macho_data_append(
               macho, stream, XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB,
               offset, 8U, 8U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) &&
           xx_macho_data_append_raw(macho, stream, offset + 8U,
                                    (uint64_t)length - 8U);
}

static uint32_t xx_macho_code_directory_fixed_size(uint32_t version) {
    return version >= UINT32_C(0x20600) ? 108U
         : version >= UINT32_C(0x20500) ? 96U
         : version >= UINT32_C(0x20400) ? 88U
         : version >= UINT32_C(0x20300) ? 64U
         : version >= UINT32_C(0x20200) ? 52U
         : version >= UINT32_C(0x20100) ? 48U
                                        : 44U;
}

static bool xx_macho_append_code_directory(
    xx_macho *macho, xx_macho_data_stream *stream, uint64_t offset,
    uint32_t length, xx_pd_struct *pd) {
    int64_t absolute;
    uint32_t version;
    uint32_t scatter_offset;
    uint32_t fixed_size;
    uint64_t available_rows;
    uint64_t row_limit;
    uint64_t rows = 0U;
    bool terminated = false;
    if (length < 44U)
        return xx_macho_append_generic_code_signature_blob(
            macho, stream, offset, length);
    if (!xx_macho_data_append(
            macho, stream,
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_CODE_DIRECTORY,
            offset, length, length, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    absolute = macho->format.base_address + (int64_t)offset;
    version = xx_io_get_u32(macho->format.device, absolute + 8, true);
    fixed_size = xx_macho_code_directory_fixed_size(version);
    if (version < UINT32_C(0x20100) || length < 48U ||
        fixed_size > length)
        return true;
    scatter_offset = xx_io_get_u32(
        macho->format.device, absolute + 44, true);
    if (scatter_offset == 0U || scatter_offset < fixed_size ||
        scatter_offset > length - 24U)
        return true;
    available_rows = ((uint64_t)length - scatter_offset) / 24U;
    row_limit = XX_MACHO_DS_MAX_SCAN / 24U;
    if (row_limit > XX_MACHO_DS_MAX_ROWS)
        row_limit = XX_MACHO_DS_MAX_ROWS;
    if (available_rows < row_limit) row_limit = available_rows;
    while (rows < row_limit) {
        uint32_t count;
        if (xx_pd_is_stopped(pd)) return false;
        count = xx_io_get_u32(
            macho->format.device,
            absolute + scatter_offset + (int64_t)rows * 24, true);
        ++rows;
        if (count == 0U) {
            terminated = true;
            break;
        }
    }
    if (!terminated) return true;
    return xx_macho_data_append_table(
        macho, stream, XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SCATTER,
        offset + scatter_offset, rows, 24U);
}

static bool xx_macho_append_code_requirement(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t offset, uint32_t length) {
    if (length < 12U)
        return xx_macho_append_generic_code_signature_blob(
            macho, stream, offset, length);
    return xx_macho_data_append(
               macho, stream,
               XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENT,
               offset, 12U, 12U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) &&
           xx_macho_data_append_raw(macho, stream, offset + 12U,
                                    (uint64_t)length - 12U);
}

static bool xx_macho_append_code_requirements(
    xx_macho *macho, xx_macho_data_stream *stream, uint64_t offset,
    uint32_t length, xx_pd_struct *pd) {
    int64_t absolute;
    uint32_t count;
    uint64_t index_bytes;
    uint32_t index;
    if (length < 12U)
        return xx_macho_append_generic_code_signature_blob(
            macho, stream, offset, length);
    absolute = macho->format.base_address + (int64_t)offset;
    count = xx_io_get_u32(macho->format.device, absolute + 8, true);
    if (!xx_macho_data_product(count, 8U, &index_bytes) ||
        index_bytes > (uint64_t)length - 12U)
        return xx_macho_append_generic_code_signature_blob(
            macho, stream, offset, length);
    if (!xx_macho_data_append(
            macho, stream,
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENTS,
            offset, 12U, 12U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        !xx_macho_data_append_table(
            macho, stream,
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB_INDEX,
            offset + 12U, count, 8U))
        return false;
    for (index = 0U; index < count && index < XX_MACHO_DS_MAX_ROWS;
         ++index) {
        uint32_t blob_offset;
        uint32_t blob_magic;
        uint32_t blob_length;
        if (xx_pd_is_stopped(pd)) return false;
        blob_offset = xx_io_get_u32(
            macho->format.device,
            absolute + 12 + (int64_t)index * 8 + 4, true);
        if (blob_offset < 12U + index_bytes ||
            blob_offset > length - 8U)
            continue;
        blob_magic = xx_io_get_u32(
            macho->format.device, absolute + blob_offset, true);
        blob_length = xx_io_get_u32(
            macho->format.device, absolute + blob_offset + 4, true);
        if (blob_length < 8U || blob_length > length - blob_offset)
            continue;
        if (blob_magic == XX_MACHO_CS_MAGIC_REQUIREMENT) {
            if (!xx_macho_append_code_requirement(
                    macho, stream, offset + blob_offset, blob_length))
                return false;
        } else if (!xx_macho_append_generic_code_signature_blob(
                       macho, stream, offset + blob_offset,
                       blob_length)) {
            return false;
        }
    }
    return true;
}

static bool xx_macho_append_code_signature_child(
    xx_macho *macho, xx_macho_data_stream *stream, uint64_t offset,
    uint32_t magic, uint32_t length, xx_pd_struct *pd) {
    if (magic == XX_MACHO_CS_MAGIC_CODEDIRECTORY)
        return xx_macho_append_code_directory(
            macho, stream, offset, length, pd);
    if (magic == XX_MACHO_CS_MAGIC_REQUIREMENTS)
        return xx_macho_append_code_requirements(
            macho, stream, offset, length, pd);
    if (magic == XX_MACHO_CS_MAGIC_REQUIREMENT)
        return xx_macho_append_code_requirement(
            macho, stream, offset, length);
    return xx_macho_append_generic_code_signature_blob(
        macho, stream, offset, length);
}

static bool xx_macho_append_code_signature(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint32_t dataoff, uint32_t datasize, xx_pd_struct *pd) {
    uint32_t magic;
    uint32_t length;
    uint32_t count;
    uint64_t index_bytes;
    uint32_t index;
    int64_t absolute;
    if (datasize < 8U || !xx_macho_data_range(macho, dataoff, datasize))
        return xx_macho_data_append_raw(macho, stream, dataoff, datasize);
    absolute = macho->format.base_address + (int64_t)dataoff;
    magic = xx_io_get_u32(macho->format.device, absolute, true);
    length = xx_io_get_u32(macho->format.device, absolute + 4, true);
    if (length < 8U || length > datasize)
        return xx_macho_data_append_raw(macho, stream, dataoff, datasize);
    if (magic != XX_MACHO_CS_MAGIC_EMBEDDED_SIGNATURE &&
        magic != XX_MACHO_CS_MAGIC_EMBEDDED_SIGNATURE_OLD &&
        magic != XX_MACHO_CS_MAGIC_DETACHED_SIGNATURE) {
        if (!xx_macho_append_code_signature_child(
                macho, stream, dataoff, magic, length, pd))
            return false;
        return length == datasize ||
               xx_macho_data_append_raw(
                   macho, stream, (uint64_t)dataoff + length,
                   (uint64_t)datasize - length);
    }
    if (length < 12U)
        return xx_macho_data_append_raw(macho, stream, dataoff, datasize);
    count = xx_io_get_u32(macho->format.device, absolute + 8, true);
    if (!xx_macho_data_product(count, 8U, &index_bytes) ||
        index_bytes > (uint64_t)length - 12U) {
        return xx_macho_data_append_raw(macho, stream, dataoff, datasize);
    }
    if (!xx_macho_data_append(
            macho, stream,
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SUPERBLOB,
            dataoff, 12U, 12U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        !xx_macho_data_append_table(
            macho, stream,
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB_INDEX,
            (uint64_t)dataoff + 12U, count, 8U))
        return false;
    for (index = 0U; index < count && index < XX_MACHO_DS_MAX_ROWS;
         ++index) {
        uint32_t blob_offset = xx_io_get_u32(
            macho->format.device, absolute + 12 + (int64_t)index * 8 + 4,
            true);
        uint32_t blob_magic;
        uint32_t blob_length;
        if (xx_pd_is_stopped(pd)) return false;
        if (blob_offset < 12U + index_bytes ||
            blob_offset > length - 8U)
            continue;
        blob_magic = xx_io_get_u32(macho->format.device,
                                   absolute + blob_offset, true);
        blob_length = xx_io_get_u32(macho->format.device,
                                    absolute + blob_offset + 4, true);
        if (blob_length < 8U || blob_length > length - blob_offset)
            continue;
        if (!xx_macho_append_code_signature_child(
                macho, stream, (uint64_t)dataoff + blob_offset,
                blob_magic, blob_length, pd)) {
            return false;
        }
    }
    return length == datasize ||
           xx_macho_data_append_raw(
               macho, stream, (uint64_t)dataoff + length,
               (uint64_t)datasize - length);
}

typedef struct xx_macho_chain_format_s {
    uint8_t word_size;
    uint8_t stride;
    uint8_t next_shift;
    uint8_t next_bits;
    bool is_64;
    bool allows_multi;
} xx_macho_chain_format;

static bool xx_macho_get_chain_format(uint16_t pointer_format,
                                      xx_macho_chain_format *output) {
    xx_macho_chain_format result;
    if (!output) return false;
    xx_mem_zero(&result, sizeof(result));
    result.is_64 = true;
    switch (pointer_format) {
        case 1U:  result.word_size = 8U; result.stride = 8U;
                  result.next_shift = 51U; result.next_bits = 11U; break;
        case 2U:  result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 12U; break;
        case 3U:  result.word_size = 4U; result.stride = 4U;
                  result.next_shift = 26U; result.next_bits = 5U;
                  result.is_64 = false; result.allows_multi = true; break;
        case 4U:  result.word_size = 4U; result.stride = 4U;
                  result.next_shift = 30U; result.next_bits = 2U;
                  result.is_64 = false; result.allows_multi = true; break;
        case 5U:  result.word_size = 4U; result.stride = 4U;
                  result.next_shift = 26U; result.next_bits = 6U;
                  result.is_64 = false; result.allows_multi = true; break;
        case 6U:  result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 12U; break;
        case 7U:  result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 11U; break;
        case 8U:  result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 12U; break;
        case 9U:  result.word_size = 8U; result.stride = 8U;
                  result.next_shift = 51U; result.next_bits = 11U; break;
        case 10U: result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 11U; break;
        case 11U: result.word_size = 8U; result.stride = 1U;
                  result.next_shift = 51U; result.next_bits = 12U; break;
        case 12U: result.word_size = 8U; result.stride = 8U;
                  result.next_shift = 51U; result.next_bits = 11U; break;
        case 13U: result.word_size = 8U; result.stride = 8U;
                  result.next_shift = 52U; result.next_bits = 11U; break;
        case 14U: result.word_size = 8U; result.stride = 4U;
                  result.next_shift = 51U; result.next_bits = 12U; break;
        default:
            return false;
    }
    *output = result;
    return true;
}

static uint32_t xx_macho_chained_pointer_id(uint16_t pointer_format,
                                            uint64_t raw) {
    bool auth;
    bool bind;
    switch (pointer_format) {
        case 1U:
        case 7U:
        case 9U:
        case 10U:
            auth = ((raw >> 63U) & UINT64_C(1)) != 0U;
            bind = ((raw >> 62U) & UINT64_C(1)) != 0U;
            if (auth)
                return bind ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND
                            : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE;
            return bind ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND
                        : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE;
        case 12U:
            auth = ((raw >> 63U) & UINT64_C(1)) != 0U;
            bind = ((raw >> 62U) & UINT64_C(1)) != 0U;
            if (!bind)
                return auth ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE
                            : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE;
            return auth ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24
                        : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24;
        case 2U:
        case 6U:
            return (raw >> 63U) != 0U
                       ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND
                       : XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE;
        case 3U:
            return (raw >> 31U) != 0U
                       ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND
                       : XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_REBASE;
        case 4U:
            return XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_CACHE_REBASE;
        case 5U:
            return XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_FIRMWARE_REBASE;
        case 8U:
        case 11U:
            return XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE;
        case 13U:
            return (raw >> 63U) != 0U
                       ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE
                       : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE;
        case 14U:
            return (raw >> 63U) != 0U
                       ? XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE
                       : XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE;
        default:
            return XX_MACHO_DATA_STRUCT_UNKNOWN;
    }
}

static bool xx_macho_chained_image_base(const xx_macho *macho,
                                         uint64_t *output) {
    uint64_t result = UINT64_MAX;
    uint32_t index;
    bool found = false;
    if (!macho || !macho->segments || !output) return false;
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        if (segment->file_size != 0U && segment->file_offset == 0U &&
            (!found || segment->virtual_address < result)) {
            result = segment->virtual_address;
            found = true;
        }
    }
    if (!found) {
        for (index = 0U; index < macho->segment_count; ++index) {
            const xx_macho_segment *segment = &macho->segments[index];
            if (segment->file_size != 0U &&
                (!found || segment->virtual_address < result)) {
                result = segment->virtual_address;
                found = true;
            }
        }
    }
    if (found) *output = result;
    return found;
}

static const xx_macho_segment *xx_macho_chained_text_segment(
    const xx_macho *macho) {
    uint32_t index;
    if (!macho || !macho->segments) return NULL;
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        if (xx_rt_strcmp(segment->name, "__TEXT") == 0)
            return segment;
    }
    return NULL;
}

static const xx_macho_segment *xx_macho_chained_segment(
    const xx_macho *macho, uint32_t segment_index,
    uint64_t segment_offset) {
    uint64_t image_base;
    uint32_t index;
    if (!xx_macho_chained_image_base(macho, &image_base) ||
        image_base > UINT64_MAX - segment_offset)
        return NULL;
    if (segment_index < macho->segment_count) {
        const xx_macho_segment *candidate = &macho->segments[segment_index];
        if (candidate->file_size != 0U &&
            candidate->virtual_address == image_base + segment_offset)
            return candidate;
    }
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *candidate = &macho->segments[index];
        if (candidate->file_size != 0U &&
            candidate->virtual_address == image_base + segment_offset)
            return candidate;
    }
    return NULL;
}

static bool xx_macho_walk_chained_pointer_chain(
    xx_macho *macho, xx_macho_data_stream *stream,
    const xx_macho_segment *segment, uint64_t page_base,
    uint64_t page_span, uint64_t start,
    uint16_t pointer_format, const xx_macho_chain_format *chain_format,
    uint8_t *visited, uint64_t *pointer_count, xx_pd_struct *pd) {
    uint64_t cursor = start;
    uint64_t step_limit;
    uint64_t steps = 0U;
    uint64_t next_mask;
    if (!macho || !stream || !segment || !chain_format || !pointer_count ||
        start >= page_span)
        return true;
    step_limit = page_span / chain_format->stride;
    if (step_limit != UINT64_MAX) ++step_limit;
    if (step_limit > XX_MACHO_DS_MAX_ROWS)
        step_limit = XX_MACHO_DS_MAX_ROWS;
    next_mask = (UINT64_C(1) << chain_format->next_bits) - UINT64_C(1);
    while (steps++ < step_limit && *pointer_count < XX_MACHO_DS_MAX_ROWS) {
        uint64_t relative;
        uint64_t raw;
        uint64_t next;
        uint64_t delta;
        uint32_t id;
        if (xx_pd_is_stopped(pd)) return false;
        if (cursor > page_span || chain_format->word_size > page_span - cursor)
            return true;
        if (visited) {
            if ((visited[cursor >> 3U] &
                 (uint8_t)(1U << (cursor & 7U))) != 0U)
                return true;
            visited[cursor >> 3U] |=
                (uint8_t)(1U << (cursor & 7U));
        }
        if (segment->file_offset > UINT64_MAX - page_base ||
            segment->file_offset + page_base > UINT64_MAX - cursor)
            return true;
        relative = segment->file_offset + page_base + cursor;
        if (!xx_macho_data_range(macho, relative, chain_format->word_size))
            return true;
        raw = chain_format->word_size == 4U
                  ? xx_io_get_u32(macho->format.device,
                                  macho->format.base_address +
                                      (int64_t)relative, false)
                  : xx_io_get_u64(macho->format.device,
                                  macho->format.base_address +
                                      (int64_t)relative, false);
        if (xx_pd_is_stopped(pd)) return false;
        id = xx_macho_chained_pointer_id(pointer_format, raw);
        if (id == XX_MACHO_DATA_STRUCT_UNKNOWN) return true;
        if (!xx_macho_data_append(
                macho, stream, id, relative, chain_format->word_size,
                chain_format->word_size, 1U, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        ++*pointer_count;
        next = (raw >> chain_format->next_shift) & next_mask;
        if (next == 0U) return true;
        if (next > UINT64_MAX / chain_format->stride) return true;
        delta = next * chain_format->stride;
        if (delta == 0U || cursor > UINT64_MAX - delta) return true;
        cursor += delta;
    }
    return true;
}

static const xx_macho_segment *xx_macho_chained_segment_for_file_offset(
    const xx_macho *macho, uint64_t file_offset, uint8_t word_size,
    uint64_t *segment_offset) {
    uint32_t index;
    if (!macho || !macho->segments || !segment_offset) return NULL;
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        uint64_t offset_in_segment;
        if (file_offset < segment->file_offset) continue;
        offset_in_segment = file_offset - segment->file_offset;
        if (offset_in_segment <= segment->file_size &&
            word_size <= segment->file_size - offset_in_segment) {
            *segment_offset = offset_in_segment;
            return segment;
        }
    }
    return NULL;
}

static const xx_macho_segment *xx_macho_chained_segment_for_vm_address(
    const xx_macho *macho, uint64_t address, uint8_t word_size,
    uint64_t *segment_offset) {
    uint32_t index;
    if (!macho || !macho->segments || !segment_offset)
        return NULL;
    for (index = 0U; index < macho->segment_count; ++index) {
        const xx_macho_segment *segment = &macho->segments[index];
        uint64_t offset_in_segment;
        if (address < segment->virtual_address) continue;
        offset_in_segment = address - segment->virtual_address;
        if (offset_in_segment <= segment->file_size &&
            word_size <= segment->file_size - offset_in_segment) {
            *segment_offset = offset_in_segment;
            return segment;
        }
    }
    return NULL;
}

static bool xx_macho_walk_firmware_file_chain(
    xx_macho *macho, xx_macho_data_stream *stream, uint64_t start,
    uint16_t pointer_format, const xx_macho_chain_format *chain_format,
    uint64_t *pointer_count, xx_pd_struct *pd) {
    uint64_t cursor = start;
    uint64_t steps = 0U;
    uint64_t next_mask;
    if (!macho || !stream || !chain_format || !pointer_count)
        return false;
    next_mask = (UINT64_C(1) << chain_format->next_bits) - UINT64_C(1);
    while (steps++ < XX_MACHO_DS_MAX_ROWS &&
           *pointer_count < XX_MACHO_DS_MAX_ROWS) {
        const xx_macho_segment *segment;
        uint64_t offset_in_segment;
        uint64_t raw;
        uint64_t next;
        uint64_t delta;
        uint32_t id;
        if (xx_pd_is_stopped(pd)) return false;
        segment = xx_macho_chained_segment_for_file_offset(
            macho, cursor, chain_format->word_size, &offset_in_segment);
        if (!segment) return true;
        if (!xx_macho_data_range(macho, cursor, chain_format->word_size))
            return true;
        raw = chain_format->word_size == 4U
                  ? xx_io_get_u32(macho->format.device,
                                  macho->format.base_address +
                                      (int64_t)cursor, false)
                  : xx_io_get_u64(macho->format.device,
                                  macho->format.base_address +
                                      (int64_t)cursor, false);
        if (xx_pd_is_stopped(pd)) return false;
        id = xx_macho_chained_pointer_id(pointer_format, raw);
        if (id == XX_MACHO_DATA_STRUCT_UNKNOWN) return true;
        if (!xx_macho_data_append(
                macho, stream, id, cursor, chain_format->word_size,
                chain_format->word_size, 1U, XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        ++*pointer_count;
        next = (raw >> chain_format->next_shift) & next_mask;
        if (next == 0U) return true;
        if (next > UINT64_MAX / chain_format->stride) return true;
        delta = next * chain_format->stride;
        if (delta == 0U || cursor > UINT64_MAX - delta) return true;
        cursor += delta;
    }
    return true;
}

static bool xx_macho_append_firmware_pointers(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint64_t starts_offset, uint32_t pointer_format,
    uint32_t starts_count, uint32_t starts_mode, xx_pd_struct *pd) {
    xx_macho_chain_format chain_format;
    const xx_macho_segment *text_segment;
    uint64_t pointer_count = 0U;
    uint32_t index;
    if (!macho || !stream || macho->format.endian != XX_ENDIAN_LITTLE ||
        (starts_mode != XX_MACHO_CHAIN_STARTS_USE_FILE_OFFSET &&
         starts_mode != XX_MACHO_CHAIN_STARTS_USE_VM_OFFSET) ||
        (pointer_format != 5U && pointer_format != 6U &&
         pointer_format != 7U && pointer_format != 10U) ||
        pointer_format > UINT16_MAX ||
        !xx_macho_get_chain_format((uint16_t)pointer_format,
                                   &chain_format) ||
        chain_format.is_64 != macho->is_64)
        return true;
    text_segment = xx_macho_chained_text_segment(macho);
    if (!text_segment) return true;
    for (index = 0U;
         index < starts_count && index < XX_MACHO_DS_MAX_ROWS &&
         pointer_count < XX_MACHO_DS_MAX_ROWS;
         ++index) {
        const xx_macho_segment *segment;
        uint64_t offset_in_segment;
        uint32_t chain_start;
        if (xx_pd_is_stopped(pd)) return false;
        chain_start = xx_macho_data_u32(
            macho, macho->format.base_address + (int64_t)starts_offset +
                       (int64_t)index * 4);
        if (xx_pd_is_stopped(pd)) return false;
        if (starts_mode == XX_MACHO_CHAIN_STARTS_USE_FILE_OFFSET) {
            uint64_t file_offset;
            if (text_segment->file_offset > UINT64_MAX - chain_start)
                continue;
            file_offset = text_segment->file_offset + chain_start;
            segment = xx_macho_chained_segment_for_file_offset(
                macho, file_offset, chain_format.word_size,
                &offset_in_segment);
            if (!segment) continue;
            if (!xx_macho_walk_firmware_file_chain(
                    macho, stream, file_offset, (uint16_t)pointer_format,
                    &chain_format, &pointer_count, pd))
                return false;
            continue;
        } else {
            uint64_t address;
            if (text_segment->virtual_address > UINT64_MAX - chain_start)
                continue;
            address = text_segment->virtual_address + chain_start;
            segment = xx_macho_chained_segment_for_vm_address(
                macho, address, chain_format.word_size,
                &offset_in_segment);
        }
        if (!segment) continue;
        if (!xx_macho_walk_chained_pointer_chain(
                macho, stream, segment, 0U, segment->file_size,
                offset_in_segment, (uint16_t)pointer_format,
                &chain_format, NULL, &pointer_count, pd))
            return false;
    }
    return true;
}

static bool xx_macho_append_chained_segment_pointers(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint32_t segment_index, uint64_t segment_info,
    uint32_t segment_size, uint16_t page_count, uint64_t page_words,
    uint64_t *pointer_count, xx_pd_struct *pd) {
    const xx_macho_segment *segment;
    xx_macho_chain_format chain_format;
    uint64_t segment_offset;
    uint16_t page_size;
    uint16_t pointer_format;
    uint8_t *visited = NULL;
    uint32_t page_index;
    bool result = true;
    if (!macho || !stream || !pointer_count ||
        macho->format.endian != XX_ENDIAN_LITTLE)
        return true;
    page_size = xx_macho_data_u16(
        macho, macho->format.base_address + (int64_t)segment_info + 4);
    pointer_format = xx_macho_data_u16(
        macho, macho->format.base_address + (int64_t)segment_info + 6);
    segment_offset = xx_macho_data_u64(
        macho, macho->format.base_address + (int64_t)segment_info + 8);
    if ((page_size != UINT16_C(0x1000) &&
         page_size != UINT16_C(0x4000)) ||
        !xx_macho_get_chain_format(pointer_format, &chain_format) ||
        chain_format.is_64 != macho->is_64)
        return true;
    segment = xx_macho_chained_segment(macho, segment_index,
                                       segment_offset);
    if (!segment) return true;
    visited = (uint8_t *)xx_mem_calloc(((size_t)page_size + 7U) / 8U, 1U);
    if (!visited) return false;
    for (page_index = 0U;
         page_index < page_count && *pointer_count < XX_MACHO_DS_MAX_ROWS;
         ++page_index) {
        uint64_t page_base;
        uint64_t page_span;
        uint16_t page_start;
        if (xx_pd_is_stopped(pd)) {
            result = false;
            break;
        }
        page_base = (uint64_t)page_index * page_size;
        if (page_base >= segment->file_size) continue;
        page_span = segment->file_size - page_base;
        if (page_span > page_size) page_span = page_size;
        xx_mem_zero(visited, ((size_t)page_size + 7U) / 8U);
        page_start = xx_macho_data_u16(
            macho, macho->format.base_address + (int64_t)segment_info +
                       22 + (int64_t)page_index * 2);
        if (page_start == XX_MACHO_CHAIN_START_NONE) continue;
        if ((page_start & XX_MACHO_CHAIN_START_MULTI) != 0U) {
            uint64_t overflow_index;
            uint64_t scan_index;
            uint64_t overflow_steps = 0U;
            bool saw_last = false;
            if (!chain_format.allows_multi) continue;
            overflow_index = page_start & ~((uint64_t)XX_MACHO_CHAIN_START_MULTI);
            if (overflow_index < page_count) continue;
            scan_index = overflow_index;
            while (scan_index < page_words &&
                   scan_index - overflow_index < page_words - page_count) {
                uint16_t overflow_start = xx_macho_data_u16(
                    macho, macho->format.base_address +
                               (int64_t)segment_info + 22 +
                               (int64_t)scan_index * 2);
                if (xx_pd_is_stopped(pd)) {
                    result = false;
                    break;
                }
                if ((overflow_start & XX_MACHO_CHAIN_START_LAST) != 0U) {
                    saw_last = true;
                    break;
                }
                ++scan_index;
            }
            if (!result) break;
            if (!saw_last) continue;
            saw_last = false;
            while (overflow_index < page_words &&
                   overflow_steps++ < page_words - page_count) {
                uint16_t overflow_start = xx_macho_data_u16(
                    macho, macho->format.base_address +
                               (int64_t)segment_info + 22 +
                               (int64_t)overflow_index * 2);
                saw_last = (overflow_start & XX_MACHO_CHAIN_START_LAST) != 0U;
                overflow_start &= ~XX_MACHO_CHAIN_START_LAST;
                if (!xx_macho_walk_chained_pointer_chain(
                        macho, stream, segment, page_base, page_span,
                        overflow_start, pointer_format, &chain_format,
                        visited, pointer_count, pd)) {
                    result = false;
                    break;
                }
                if (saw_last) break;
                ++overflow_index;
            }
            if (!result) break;
        } else if (!xx_macho_walk_chained_pointer_chain(
                       macho, stream, segment, page_base, page_span,
                       page_start, pointer_format, &chain_format,
                       visited, pointer_count, pd)) {
            result = false;
            break;
        }
    }
    xx_mem_free(visited);
    (void)segment_size;
    return result;
}

static bool xx_macho_append_chained_fixups(
    xx_macho *macho, xx_macho_data_stream *stream,
    uint32_t dataoff, uint32_t datasize, xx_pd_struct *pd) {
    int64_t absolute;
    uint32_t starts_offset;
    uint32_t imports_offset;
    uint32_t symbols_offset;
    uint32_t imports_count;
    uint32_t imports_format;
    uint32_t symbols_format;
    uint32_t fixups_version;
    uint32_t row_size;
    uint32_t import_id;
    uint64_t pointer_count = 0U;
    if (datasize < 28U || !xx_macho_data_range(macho, dataoff, datasize))
        return xx_macho_data_append_raw(macho, stream, dataoff, datasize);
    absolute = macho->format.base_address + (int64_t)dataoff;
    fixups_version = xx_macho_data_u32(macho, absolute);
    starts_offset = xx_macho_data_u32(macho, absolute + 4);
    imports_offset = xx_macho_data_u32(macho, absolute + 8);
    symbols_offset = xx_macho_data_u32(macho, absolute + 12);
    imports_count = xx_macho_data_u32(macho, absolute + 16);
    imports_format = xx_macho_data_u32(macho, absolute + 20);
    symbols_format = xx_macho_data_u32(macho, absolute + 24);
    if (!xx_macho_data_append(
            macho, stream, XX_MACHO_DATA_STRUCT_CHAINED_FIXUPS_HEADER,
            dataoff, 28U, 28U, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (starts_offset >= 28U && starts_offset <= datasize - 4U) {
        uint32_t seg_count = xx_macho_data_u32(
            macho, absolute + starts_offset);
        uint64_t image_size;
        if (xx_macho_data_product(seg_count, 4U, &image_size) &&
            image_size <= UINT64_MAX - 4U) {
            image_size += 4U;
            if (image_size <= (uint64_t)datasize - starts_offset) {
                uint32_t index;
                if (!xx_macho_data_append(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_CHAINED_STARTS_IN_IMAGE,
                        (uint64_t)dataoff + starts_offset, 4U,
                        image_size, 1U, XX_DATA_STRUCT_TYPE_STRUCT))
                    return false;
                if (!xx_macho_data_append_table(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_CHAINED_SEG_INFO_OFFSET,
                        (uint64_t)dataoff + starts_offset + 4U,
                        seg_count, 4U))
                    return false;
                for (index = 0U;
                     index < seg_count && index < XX_MACHO_DS_MAX_ROWS;
                     ++index) {
                    uint32_t segment_relative;
                    uint32_t segment_size;
                    uint16_t page_count;
                    uint64_t page_words;
                    uint64_t segment_at;
                    if (xx_pd_is_stopped(pd)) return false;
                    segment_relative = xx_macho_data_u32(
                        macho, absolute + starts_offset + 4 +
                                   (int64_t)index * 4);
                    if (segment_relative == 0U || segment_relative < image_size ||
                        segment_relative > datasize - starts_offset - 4U)
                        continue;
                    segment_at = (uint64_t)starts_offset +
                                 segment_relative;
                    segment_size = xx_macho_data_u32(
                        macho, absolute + (int64_t)segment_at);
                    if (segment_size < 22U ||
                        segment_size > (uint64_t)datasize - segment_at ||
                        ((segment_size - 22U) & 1U) != 0U)
                        continue;
                    page_count = xx_macho_data_u16(
                        macho, absolute + (int64_t)segment_at + 20);
                    page_words = ((uint64_t)segment_size - 22U) / 2U;
                    if ((uint64_t)page_count > page_words) continue;
                    if (!xx_macho_data_append(
                            macho, stream,
                            XX_MACHO_DATA_STRUCT_CHAINED_STARTS_IN_SEGMENT,
                            (uint64_t)dataoff + segment_at, 22U,
                            segment_size, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
                        !xx_macho_data_append_table(
                            macho, stream,
                            XX_MACHO_DATA_STRUCT_CHAINED_PAGE_START,
                            (uint64_t)dataoff + segment_at + 22U,
                            page_count, 2U) ||
                        !xx_macho_data_append_table(
                            macho, stream,
                            XX_MACHO_DATA_STRUCT_CHAINED_CHAIN_START,
                            (uint64_t)dataoff + segment_at + 22U +
                                (uint64_t)page_count * 2U,
                            page_words - page_count, 2U))
                        return false;
                    if (fixups_version == 0U &&
                        !xx_macho_append_chained_segment_pointers(
                            macho, stream, index,
                            (uint64_t)dataoff + segment_at,
                            segment_size, page_count, page_words,
                            &pointer_count, pd))
                        return false;
                }
            }
        }
    }
    if (imports_format == 1U) {
        row_size = 4U;
        import_id = XX_MACHO_DATA_STRUCT_CHAINED_IMPORT;
    } else if (imports_format == 2U) {
        row_size = 8U;
        import_id = XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND;
    } else if (imports_format == 3U) {
        row_size = 16U;
        import_id = XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND64;
    } else {
        row_size = 0U;
        import_id = XX_MACHO_DATA_STRUCT_UNKNOWN;
    }
    if (row_size != 0U && imports_count != 0U && imports_offset >= 28U) {
        uint64_t imports_size;
        if (xx_macho_data_product(imports_count, row_size, &imports_size) &&
            imports_offset <= datasize &&
            imports_size <= (uint64_t)datasize - imports_offset &&
            !xx_macho_data_append_table(
                macho, stream, import_id,
                (uint64_t)dataoff + imports_offset,
                imports_count, row_size))
                return false;
    }
    if (symbols_offset >= 28U && symbols_offset < datasize) {
        uint64_t symbol_size = (uint64_t)datasize - symbols_offset;
        if (symbols_format == 0U) {
            uint64_t symbol_count = xx_macho_count_strings(
                macho, (uint64_t)dataoff + symbols_offset, symbol_size,
                pd);
            if (xx_pd_is_stopped(pd)) return false;
            if (!xx_macho_data_append(
                    macho, stream, XX_MACHO_DATA_STRUCT_STRING_TABLE,
                    (uint64_t)dataoff + symbols_offset, 1U, symbol_size,
                    symbol_count, XX_DATA_STRUCT_TYPE_ENTRY))
                return false;
        } else if (!xx_macho_data_append_raw(
                       macho, stream,
                       (uint64_t)dataoff + symbols_offset, symbol_size)) {
            return false;
        }
    }
    return true;
}

static bool xx_macho_append_linkedit_children(
    xx_macho *macho, xx_macho_data_stream *stream, uint32_t command,
    int64_t command_offset, xx_pd_struct *pd) {
    uint32_t dataoff = xx_macho_data_u32(macho, command_offset + 8);
    uint32_t datasize = xx_macho_data_u32(macho, command_offset + 12);
    uint64_t count;
    if (datasize == 0U) return true;
    if (!xx_macho_data_range(macho, dataoff, datasize)) return false;
    switch (command) {
        case XX_MACHO_LOAD_CODE_SIGNATURE:
        case XX_MACHO_LOAD_DYLIB_CODE_SIGN_DRS:
            return xx_macho_append_code_signature(macho, stream, dataoff,
                                                   datasize, pd);
        case XX_MACHO_LOAD_FUNCTION_STARTS:
            count = xx_macho_count_function_starts(macho, dataoff,
                                                    datasize, pd);
            if (xx_pd_is_stopped(pd)) return false;
            return xx_macho_data_append(
                macho, stream, XX_MACHO_DATA_STRUCT_FUNCTION_STARTS,
                dataoff, 1U, datasize, count,
                XX_DATA_STRUCT_TYPE_ENTRY);
        case XX_MACHO_LOAD_DATA_IN_CODE:
            if ((datasize % 8U) != 0U)
                return xx_macho_data_append_raw(macho, stream, dataoff,
                                                datasize);
            return xx_macho_data_append_table(
                macho, stream, XX_MACHO_DATA_STRUCT_DATA_IN_CODE,
                dataoff, datasize / 8U, 8U);
        case XX_MACHO_LOAD_DYLD_CHAINED_FIXUPS:
            return xx_macho_append_chained_fixups(macho, stream, dataoff,
                                                  datasize, pd);
        case XX_MACHO_LOAD_DYLD_EXPORTS_TRIE:
            count = xx_macho_count_exports(macho, dataoff, datasize, pd);
            if (xx_pd_is_stopped(pd)) return false;
            return xx_macho_data_append(
                macho, stream, XX_MACHO_DATA_STRUCT_EXPORT,
                dataoff, 0U, datasize, count,
                XX_DATA_STRUCT_TYPE_ENTRY);
        default:
            return xx_macho_data_append_raw(macho, stream, dataoff,
                                            datasize);
    }
}

static bool xx_macho_append_dyld_info_children(
    xx_macho *macho, xx_macho_data_stream *stream,
    int64_t command_offset, xx_pd_struct *pd) {
    int field;
    for (field = 8; field <= 32; field += 8) {
        uint32_t offset = xx_macho_data_u32(macho,
                                            command_offset + field);
        uint32_t size = xx_macho_data_u32(macho,
                                          command_offset + field + 4);
        if (size != 0U &&
            !xx_macho_data_append_raw(macho, stream, offset, size))
            return false;
    }
    {
        uint32_t offset = xx_macho_data_u32(macho, command_offset + 40);
        uint32_t size = xx_macho_data_u32(macho, command_offset + 44);
        uint64_t count;
        if (size == 0U) return true;
        if (!xx_macho_data_range(macho, offset, size)) return false;
        count = xx_macho_count_exports(macho, offset, size, pd);
        if (xx_pd_is_stopped(pd)) return false;
        return xx_macho_data_append(
            macho, stream, XX_MACHO_DATA_STRUCT_EXPORT,
            offset, 0U, size, count, XX_DATA_STRUCT_TYPE_ENTRY);
    }
}

static bool xx_macho_data_append_typed_command(
    xx_macho *macho, xx_macho_data_stream *stream, uint32_t id,
    uint64_t command_relative, uint32_t command_size,
    uint32_t fixed_size, bool fixed_only) {
    uint64_t represented_size;
    if (command_size < fixed_size) return true;
    represented_size = fixed_only ? fixed_size : command_size;
    return xx_macho_data_append(macho, stream, id, command_relative,
                                represented_size, represented_size, 1U,
                                XX_DATA_STRUCT_TYPE_STRUCT);
}

static bool xx_macho_data_build(xx_macho *macho,
                                xx_macho_data_stream *stream,
                                xx_pd_struct *pd) {
    uint32_t header_size;
    uint64_t command_relative;
    uint32_t index;
    if (!macho || !stream || !macho->format.base_info_handled ||
        xx_pd_is_stopped(pd))
        return false;
    header_size = macho->is_64 ? XX_MACHO_DS_HEADER64_SIZE
                               : XX_MACHO_DS_HEADER32_SIZE;
    if (!xx_macho_data_append(
            macho, stream,
            macho->is_64 ? XX_MACHO_DATA_STRUCT_MACH_HEADER_64
                         : XX_MACHO_DATA_STRUCT_MACH_HEADER,
            0U, header_size, header_size, 1U,
            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    command_relative = header_size;
    for (index = 0U; index < macho->command_count; ++index) {
        int64_t command_offset;
        uint32_t command;
        uint32_t command_size;
        bool recognized = true;
        if (xx_pd_is_stopped(pd) ||
            !xx_macho_data_range(macho, command_relative,
                                 XX_MACHO_DS_LOAD_SIZE))
            return false;
        command_offset = macho->format.base_address +
                         (int64_t)command_relative;
        command = xx_macho_data_u32(macho, command_offset);
        command_size = xx_macho_data_u32(macho, command_offset + 4);
        if (command_size < XX_MACHO_DS_LOAD_SIZE ||
            !xx_macho_data_range(macho, command_relative, command_size) ||
            !xx_macho_data_append(
                macho, stream, XX_MACHO_DATA_STRUCT_LOAD_COMMAND,
                command_relative, XX_MACHO_DS_LOAD_SIZE,
                XX_MACHO_DS_LOAD_SIZE, 1U,
                XX_DATA_STRUCT_TYPE_STRUCT))
            return false;
        switch (command) {
            case XX_MACHO_LOAD_SEGMENT:
                if (macho->is_64 ||
                    !xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_SEGMENT_COMMAND,
                        command_relative, command_size,
                        XX_MACHO_DS_SEGMENT32_SIZE, true) ||
                    (command_size >= XX_MACHO_DS_SEGMENT32_SIZE &&
                     !xx_macho_append_section_children(
                         macho, stream, command_relative, command_size, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_SEGMENT_64:
                if (!macho->is_64 ||
                    !xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_SEGMENT_COMMAND_64,
                        command_relative, command_size,
                        XX_MACHO_DS_SEGMENT64_SIZE, true) ||
                    (command_size >= XX_MACHO_DS_SEGMENT64_SIZE &&
                     !xx_macho_append_section_children(
                         macho, stream, command_relative, command_size, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_SYMTAB:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_SYMTAB_COMMAND,
                        command_relative, command_size, 24U, true) ||
                    (command_size >= 24U &&
                     !xx_macho_append_symtab_children(
                         macho, stream, command_offset, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_SYMSEG:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_SYMSEG_COMMAND,
                        command_relative, command_size, 16U, true))
                    return false;
                if (command_size >= 16U) {
                    uint32_t data_offset = xx_macho_data_u32(
                        macho, command_offset + 8);
                    uint32_t data_size = xx_macho_data_u32(
                        macho, command_offset + 12);
                    if (!xx_macho_data_append_raw(macho, stream, data_offset,
                                                  data_size))
                        return false;
                }
                break;
            case XX_MACHO_LOAD_THREAD:
            case XX_MACHO_LOAD_UNIXTHREAD:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_THREAD_COMMAND,
                        command_relative, command_size, 8U, true) ||
                    !xx_macho_append_thread_children(
                        macho, stream, command_relative, command_size))
                    return false;
                break;
            case XX_MACHO_LOAD_FVMLIB:
            case XX_MACHO_LOAD_ID_FVMLIB:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_FVMLIB_COMMAND,
                        command_relative, command_size, 20U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_IDENT:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_IDENT_COMMAND,
                        command_relative, command_size, 8U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_FVMFILE:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_FVMFILE_COMMAND,
                        command_relative, command_size, 16U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_PREPAGE:
                recognized = false;
                break;
            case XX_MACHO_LOAD_DYSYMTAB:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_DYSYMTAB_COMMAND,
                        command_relative, command_size, 80U, true) ||
                    (command_size >= 80U &&
                     !xx_macho_append_dysymtab_children(
                         macho, stream, command_offset, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_DYLIB:
            case XX_MACHO_LOAD_ID_DYLIB:
            case XX_MACHO_LOAD_WEAK_DYLIB:
            case XX_MACHO_LOAD_REEXPORT_DYLIB:
            case XX_MACHO_LOAD_LAZY_DYLIB:
            case XX_MACHO_LOAD_UPWARD_DYLIB:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_DYLIB_COMMAND,
                        command_relative, command_size, 24U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_DYLINKER:
            case XX_MACHO_LOAD_ID_DYLINKER:
            case XX_MACHO_LOAD_DYLD_ENVIRONMENT:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_DYLINKER_COMMAND,
                        command_relative, command_size, 12U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_PREBOUND_DYLIB:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_PREBOUND_DYLIB_COMMAND,
                        command_relative, command_size, 20U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_ROUTINES:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_ROUTINES_COMMAND,
                        command_relative, command_size, 40U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_SUB_FRAMEWORK:
            case XX_MACHO_LOAD_SUB_UMBRELLA:
            case XX_MACHO_LOAD_SUB_CLIENT:
            case XX_MACHO_LOAD_SUB_LIBRARY: {
                uint32_t id = command == XX_MACHO_LOAD_SUB_FRAMEWORK
                                  ? XX_MACHO_DATA_STRUCT_SUB_FRAMEWORK_COMMAND
                              : command == XX_MACHO_LOAD_SUB_UMBRELLA
                                  ? XX_MACHO_DATA_STRUCT_SUB_UMBRELLA_COMMAND
                              : command == XX_MACHO_LOAD_SUB_CLIENT
                                  ? XX_MACHO_DATA_STRUCT_SUB_CLIENT_COMMAND
                                  : XX_MACHO_DATA_STRUCT_SUB_LIBRARY_COMMAND;
                if (!xx_macho_data_append_typed_command(
                        macho, stream, id, command_relative, command_size,
                        12U, false))
                    return false;
                break;
            }
            case XX_MACHO_LOAD_TWOLEVEL_HINTS:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_TWOLEVEL_HINTS_COMMAND,
                        command_relative, command_size, 16U, true))
                    return false;
                if (command_size >= 16U &&
                    !xx_macho_data_append_table(
                        macho, stream, XX_MACHO_DATA_STRUCT_TWOLEVEL_HINT,
                        xx_macho_data_u32(macho, command_offset + 8),
                        xx_macho_data_u32(macho, command_offset + 12), 4U))
                    return false;
                break;
            case XX_MACHO_LOAD_PREBIND_CKSUM:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_PREBIND_CKSUM_COMMAND,
                        command_relative, command_size, 12U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_ROUTINES_64:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_ROUTINES_COMMAND_64,
                        command_relative, command_size, 72U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_UUID:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_UUID_COMMAND,
                        command_relative, command_size, 24U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_RPATH:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_RPATH_COMMAND,
                        command_relative, command_size, 12U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_CODE_SIGNATURE:
            case XX_MACHO_LOAD_SEGMENT_SPLIT_INFO:
            case XX_MACHO_LOAD_FUNCTION_STARTS:
            case XX_MACHO_LOAD_DATA_IN_CODE:
            case XX_MACHO_LOAD_DYLIB_CODE_SIGN_DRS:
            case XX_MACHO_LOAD_LINKER_OPTIMIZATION_HINT:
            case XX_MACHO_LOAD_DYLD_EXPORTS_TRIE:
            case XX_MACHO_LOAD_DYLD_CHAINED_FIXUPS:
            case XX_MACHO_LOAD_ATOM_INFO:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_LINKEDIT_DATA_COMMAND,
                        command_relative, command_size, 16U, true) ||
                    (command_size >= 16U &&
                     !xx_macho_append_linkedit_children(
                         macho, stream, command, command_offset, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_ENCRYPTION_INFO:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_ENCRYPTION_INFO_COMMAND,
                        command_relative, command_size, 20U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_DYLD_INFO:
            case XX_MACHO_LOAD_DYLD_INFO_ONLY:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_DYLD_INFO_COMMAND,
                        command_relative, command_size, 48U, true) ||
                    (command_size >= 48U &&
                     !xx_macho_append_dyld_info_children(
                         macho, stream, command_offset, pd)))
                    return false;
                break;
            case XX_MACHO_LOAD_VERSION_MIN_MACOSX:
            case XX_MACHO_LOAD_VERSION_MIN_IPHONEOS:
            case XX_MACHO_LOAD_VERSION_MIN_TVOS:
            case XX_MACHO_LOAD_VERSION_MIN_WATCHOS:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_VERSION_MIN_COMMAND,
                        command_relative, command_size, 16U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_MAIN:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_ENTRY_POINT_COMMAND,
                        command_relative, command_size, 24U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_SOURCE_VERSION:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_SOURCE_VERSION_COMMAND,
                        command_relative, command_size, 16U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_ENCRYPTION_INFO_64:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_ENCRYPTION_INFO_COMMAND_64,
                        command_relative, command_size, 24U, true))
                    return false;
                break;
            case XX_MACHO_LOAD_LINKER_OPTION:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_LINKER_OPTION_COMMAND,
                        command_relative, command_size, 12U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_NOTE:
                if (!xx_macho_data_append_typed_command(
                        macho, stream, XX_MACHO_DATA_STRUCT_NOTE_COMMAND,
                        command_relative, command_size, 40U, true))
                    return false;
                if (command_size >= 40U &&
                    !xx_macho_data_append_raw(
                        macho, stream,
                        xx_macho_data_u64(macho, command_offset + 24),
                        xx_macho_data_u64(macho, command_offset + 32)))
                    return false;
                break;
            case XX_MACHO_LOAD_BUILD_VERSION:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_BUILD_VERSION_COMMAND,
                        command_relative, command_size, 24U, true))
                    return false;
                if (command_size >= 24U) {
                    uint32_t tools = xx_macho_data_u32(
                        macho, command_offset + 20);
                    uint64_t tool_size;
                    if (!xx_macho_data_product(tools, 8U, &tool_size) ||
                        tool_size > (uint64_t)command_size - 24U)
                        return false;
                    if (!xx_macho_data_append_table(
                            macho, stream,
                            XX_MACHO_DATA_STRUCT_BUILD_TOOL_VERSION,
                            command_relative + 24U, tools, 8U))
                        return false;
                }
                break;
            case XX_MACHO_LOAD_FILESET_ENTRY:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_FILESET_ENTRY_COMMAND,
                        command_relative, command_size, 32U, false))
                    return false;
                break;
            case XX_MACHO_LOAD_TARGET_TRIPLE:
                if (!xx_macho_data_append_typed_command(
                        macho, stream,
                        XX_MACHO_DATA_STRUCT_TARGET_TRIPLE_COMMAND,
                        command_relative, command_size, 12U, false))
                    return false;
                break;
            default:
                recognized = false;
                break;
        }
        if (!recognized && command_size > XX_MACHO_DS_LOAD_SIZE &&
            !xx_macho_data_append_raw(
                macho, stream, command_relative + XX_MACHO_DS_LOAD_SIZE,
                command_size - XX_MACHO_DS_LOAD_SIZE))
            return false;
        command_relative += command_size;
    }
    if (macho->format.overlay_size > 0 &&
        !xx_macho_data_append_raw(
            macho, stream,
            (uint64_t)(macho->format.overlay_offset -
                       macho->format.base_address),
            (uint64_t)macho->format.overlay_size))
        return false;
    return !xx_pd_is_stopped(pd);
}

static const char *xx_macho_data_struct_id_to_string(Abstractformat *format,
                                                      uint32_t id) {
    (void)format;
    if (id <= XX_MACHO_DATA_STRUCT_LAST)
        return xx_macho_data_struct_names[id];
    return "UNKNOWN";
}

static uint32_t xx_macho_data_struct_string_to_id(Abstractformat *format,
                                                   const char *name) {
    uint32_t id;
    (void)format;
    if (!name) return XX_MACHO_DATA_STRUCT_UNKNOWN;
    for (id = XX_MACHO_DATA_STRUCT_MACH_HEADER;
         id <= XX_MACHO_DATA_STRUCT_LAST; ++id) {
        if (xx_rt_strcmp(name, xx_macho_data_struct_names[id]) == 0)
            return id;
    }
    return XX_MACHO_DATA_STRUCT_UNKNOWN;
}

static xx_data_struct_state *xx_macho_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_data_struct_state *state = NULL;
    xx_macho_data_stream *stream = NULL;
    xx_macho *macho = (xx_macho *)format;
    if (!format || xx_pd_is_stopped(pd) ||
        (!format->base_info_handled &&
         !xx_macho_handle_base_info(format, pd)))
        return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_macho_data_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) goto fail;
    if (!xx_macho_data_build(macho, stream, pd) || stream->count == 0U)
        goto fail;
    xx_data_struct_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_macho_data_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_struct = stream->items[0];
    state->current_index = 0;
    state->has_struct = true;
    return state;
fail:
    if (stream) xx_macho_data_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

static const xx_data_struct *xx_macho_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state) {
    return format && state && state->format == format && state->has_struct
               ? &state->current_struct : NULL;
}

static bool xx_macho_data_struct_move_to_next(
    Abstractformat *format, xx_data_struct_state *state,
    xx_pd_struct *pd) {
    xx_macho_data_stream *stream;
    int64_t next;
    if (!format || !state || state->format != format ||
        xx_pd_is_stopped(pd) ||
        !(stream = (xx_macho_data_stream *)state->internal_state)) {
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

static void xx_macho_free_data_structs_reading(
    Abstractformat *format, xx_data_struct_state *state) {
    (void)format;
    xx_data_struct_state_free(state);
}

static bool xx_macho_get_static_fields(
    uint32_t id, const xx_data_struct_field_desc **fields, size_t *count) {
    if (!fields || !count) return false;
    *fields = NULL;
    *count = 0U;
#define XX_MACHO_SELECT_FIELDS(array_) do { \
        *fields = array_; \
        *count = XX_MACHO_FIELDS_COUNT(array_); \
        return true; \
    } while (0)
    switch (id) {
        case XX_MACHO_DATA_STRUCT_MACH_HEADER:
            XX_MACHO_SELECT_FIELDS(xx_macho_header32_fields);
        case XX_MACHO_DATA_STRUCT_MACH_HEADER_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_header64_fields);
        case XX_MACHO_DATA_STRUCT_LOAD_COMMAND:
        case XX_MACHO_DATA_STRUCT_IDENT_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_load_fields);
        case XX_MACHO_DATA_STRUCT_SEGMENT_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_segment32_fields);
        case XX_MACHO_DATA_STRUCT_SEGMENT_COMMAND_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_segment64_fields);
        case XX_MACHO_DATA_STRUCT_SECTION:
            XX_MACHO_SELECT_FIELDS(xx_macho_section32_fields);
        case XX_MACHO_DATA_STRUCT_SECTION_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_section64_fields);
        case XX_MACHO_DATA_STRUCT_DYLIB_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_dylib_fields);
        case XX_MACHO_DATA_STRUCT_DYLINKER_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_dylinker_fields);
        case XX_MACHO_DATA_STRUCT_RPATH_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_rpath_fields);
        case XX_MACHO_DATA_STRUCT_TARGET_TRIPLE_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_target_triple_fields);
        case XX_MACHO_DATA_STRUCT_SYMTAB_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_symtab_fields);
        case XX_MACHO_DATA_STRUCT_DYSYMTAB_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_dysymtab_fields);
        case XX_MACHO_DATA_STRUCT_UUID_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_uuid_fields);
        case XX_MACHO_DATA_STRUCT_VERSION_MIN_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_version_min_fields);
        case XX_MACHO_DATA_STRUCT_BUILD_VERSION_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_build_version_fields);
        case XX_MACHO_DATA_STRUCT_SOURCE_VERSION_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_source_version_fields);
        case XX_MACHO_DATA_STRUCT_ENTRY_POINT_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_entry_point_fields);
        case XX_MACHO_DATA_STRUCT_ENCRYPTION_INFO_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_encryption32_fields);
        case XX_MACHO_DATA_STRUCT_ENCRYPTION_INFO_COMMAND_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_encryption64_fields);
        case XX_MACHO_DATA_STRUCT_LINKEDIT_DATA_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_linkedit_fields);
        case XX_MACHO_DATA_STRUCT_DYLD_INFO_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_dyld_info_fields);
        case XX_MACHO_DATA_STRUCT_NLIST:
            XX_MACHO_SELECT_FIELDS(xx_macho_nlist32_fields);
        case XX_MACHO_DATA_STRUCT_NLIST_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_nlist64_fields);
        case XX_MACHO_DATA_STRUCT_INDIRECT_SYMBOL:
            XX_MACHO_SELECT_FIELDS(xx_macho_indirect_symbol_fields);
        case XX_MACHO_DATA_STRUCT_DATA_IN_CODE:
            XX_MACHO_SELECT_FIELDS(xx_macho_data_in_code_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_FIXUPS_HEADER:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_header_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_IMPORT:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_import_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_import_addend_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND64:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_import_addend64_fields);
        case XX_MACHO_DATA_STRUCT_SYMSEG_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_symseg_fields);
        case XX_MACHO_DATA_STRUCT_THREAD_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_thread_fields);
        case XX_MACHO_DATA_STRUCT_THREAD_STATE_HEADER:
            XX_MACHO_SELECT_FIELDS(xx_macho_thread_state_header_fields);
        case XX_MACHO_DATA_STRUCT_THREAD_STATE_WORD:
            XX_MACHO_SELECT_FIELDS(xx_macho_thread_state_word_fields);
        case XX_MACHO_DATA_STRUCT_X86_THREAD_STATE32:
            XX_MACHO_SELECT_FIELDS(xx_macho_x86_thread_state32_fields);
        case XX_MACHO_DATA_STRUCT_X86_THREAD_STATE64:
            XX_MACHO_SELECT_FIELDS(xx_macho_x86_thread_state64_fields);
        case XX_MACHO_DATA_STRUCT_X86_FLOAT_STATE64:
            XX_MACHO_SELECT_FIELDS(xx_macho_x86_float_state64_fields);
        case XX_MACHO_DATA_STRUCT_X86_EXCEPTION_STATE64:
            XX_MACHO_SELECT_FIELDS(xx_macho_x86_exception_state64_fields);
        case XX_MACHO_DATA_STRUCT_ARM_THREAD_STATE32:
            XX_MACHO_SELECT_FIELDS(xx_macho_arm_thread_state32_fields);
        case XX_MACHO_DATA_STRUCT_ARM_THREAD_STATE64:
            XX_MACHO_SELECT_FIELDS(xx_macho_arm_thread_state64_fields);
        case XX_MACHO_DATA_STRUCT_PPC_THREAD_STATE32:
            XX_MACHO_SELECT_FIELDS(xx_macho_ppc_thread_state32_fields);
        case XX_MACHO_DATA_STRUCT_PPC_THREAD_STATE64:
            XX_MACHO_SELECT_FIELDS(xx_macho_ppc_thread_state64_fields);
        case XX_MACHO_DATA_STRUCT_FVMLIB_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_fvmlib_fields);
        case XX_MACHO_DATA_STRUCT_FVMFILE_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_fvmfile_fields);
        case XX_MACHO_DATA_STRUCT_PREBOUND_DYLIB_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_prebound_dylib_fields);
        case XX_MACHO_DATA_STRUCT_ROUTINES_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_routines32_fields);
        case XX_MACHO_DATA_STRUCT_ROUTINES_COMMAND_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_routines64_fields);
        case XX_MACHO_DATA_STRUCT_SUB_FRAMEWORK_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_UMBRELLA_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_CLIENT_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_LIBRARY_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_single_string_command_fields);
        case XX_MACHO_DATA_STRUCT_TWOLEVEL_HINTS_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_twolevel_hints_fields);
        case XX_MACHO_DATA_STRUCT_PREBIND_CKSUM_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_prebind_cksum_fields);
        case XX_MACHO_DATA_STRUCT_LINKER_OPTION_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_linker_option_fields);
        case XX_MACHO_DATA_STRUCT_NOTE_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_note_fields);
        case XX_MACHO_DATA_STRUCT_BUILD_TOOL_VERSION:
            XX_MACHO_SELECT_FIELDS(xx_macho_build_tool_fields);
        case XX_MACHO_DATA_STRUCT_FILESET_ENTRY_COMMAND:
            XX_MACHO_SELECT_FIELDS(xx_macho_fileset_entry_fields);
        case XX_MACHO_DATA_STRUCT_DYLIB_MODULE:
            XX_MACHO_SELECT_FIELDS(xx_macho_dylib_module32_fields);
        case XX_MACHO_DATA_STRUCT_DYLIB_MODULE_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_dylib_module64_fields);
        case XX_MACHO_DATA_STRUCT_DYLIB_TABLE_OF_CONTENTS:
            XX_MACHO_SELECT_FIELDS(xx_macho_toc_fields);
        case XX_MACHO_DATA_STRUCT_DYLIB_REFERENCE:
            XX_MACHO_SELECT_FIELDS(xx_macho_reference_fields);
        case XX_MACHO_DATA_STRUCT_RELOCATION_INFO:
            XX_MACHO_SELECT_FIELDS(xx_macho_relocation_fields);
        case XX_MACHO_DATA_STRUCT_SCATTERED_RELOCATION_INFO:
            XX_MACHO_SELECT_FIELDS(xx_macho_scattered_relocation_fields);
        case XX_MACHO_DATA_STRUCT_TWOLEVEL_HINT:
            XX_MACHO_SELECT_FIELDS(xx_macho_twolevel_hint_fields);
        case XX_MACHO_DATA_STRUCT_TLV_DESCRIPTOR:
            XX_MACHO_SELECT_FIELDS(xx_macho_tlv32_fields);
        case XX_MACHO_DATA_STRUCT_TLV_DESCRIPTOR_64:
            XX_MACHO_SELECT_FIELDS(xx_macho_tlv64_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_STARTS_IN_IMAGE:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_starts_image_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_STARTS_IN_SEGMENT:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_starts_segment_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_SEG_INFO_OFFSET:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_seg_info_offset_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PAGE_START:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_page_start_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_CHAIN_START:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_chain_start_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_STARTS_OFFSETS:
            XX_MACHO_SELECT_FIELDS(xx_macho_chained_starts_offsets_fields);
        case XX_MACHO_DATA_STRUCT_CHAIN_START_OFFSET:
            XX_MACHO_SELECT_FIELDS(xx_macho_chain_start_offset_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SUPERBLOB:
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENTS:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_superblob_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB_INDEX:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_blob_index_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_blob_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_CODE_DIRECTORY:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_code_directory_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SCATTER:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_scatter_fields);
        case XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENT:
            XX_MACHO_SELECT_FIELDS(xx_macho_cs_requirement_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_bind_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_auth_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_auth_bind_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_bind24_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_arm64e_auth_bind24_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_64_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_64_bind_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_64_kernel_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_32_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_32_bind_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_CACHE_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_32_cache_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_FIRMWARE_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_32_firmware_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_shared_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_shared_auth_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_segmented_rebase_fields);
        case XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE:
            XX_MACHO_SELECT_FIELDS(xx_macho_ptr_segmented_auth_fields);
        default:
            return false;
    }
#undef XX_MACHO_SELECT_FIELDS
}

static bool xx_macho_command_string_field(
    xx_macho *macho, const xx_data_struct *data_struct,
    int64_t *relative_offset, int64_t *size) {
    uint32_t pointer_field = 0U;
    uint32_t minimum = 0U;
    uint32_t string_offset;
    if (!macho || !data_struct || !relative_offset || !size) return false;
    if (data_struct->id == XX_MACHO_DATA_STRUCT_STRING_TABLE) {
        if (data_struct->total_size <= 0) return false;
        *relative_offset = 0;
        *size = data_struct->total_size;
        return true;
    }
    switch (data_struct->id) {
        case XX_MACHO_DATA_STRUCT_DYLIB_COMMAND:
            pointer_field = 8U; minimum = 24U; break;
        case XX_MACHO_DATA_STRUCT_DYLINKER_COMMAND:
        case XX_MACHO_DATA_STRUCT_RPATH_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_FRAMEWORK_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_UMBRELLA_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_CLIENT_COMMAND:
        case XX_MACHO_DATA_STRUCT_SUB_LIBRARY_COMMAND:
        case XX_MACHO_DATA_STRUCT_TARGET_TRIPLE_COMMAND:
            pointer_field = 8U; minimum = 12U; break;
        case XX_MACHO_DATA_STRUCT_FVMLIB_COMMAND:
            pointer_field = 8U; minimum = 20U; break;
        case XX_MACHO_DATA_STRUCT_FVMFILE_COMMAND:
            pointer_field = 8U; minimum = 16U; break;
        case XX_MACHO_DATA_STRUCT_PREBOUND_DYLIB_COMMAND:
            pointer_field = 8U; minimum = 20U; break;
        case XX_MACHO_DATA_STRUCT_FILESET_ENTRY_COMMAND:
            pointer_field = 24U; minimum = 32U; break;
        case XX_MACHO_DATA_STRUCT_IDENT_COMMAND:
            if (data_struct->total_size <= 8) return false;
            *relative_offset = 8;
            *size = data_struct->total_size - 8;
            return true;
        case XX_MACHO_DATA_STRUCT_LINKER_OPTION_COMMAND:
            if (data_struct->total_size <= 12) return false;
            *relative_offset = 12;
            *size = data_struct->total_size - 12;
            return true;
        default:
            return false;
    }
    if (data_struct->total_size < minimum ||
        pointer_field > (uint32_t)data_struct->total_size - 4U)
        return false;
    string_offset = xx_macho_data_u32(macho,
                                      data_struct->offset + pointer_field);
    if (string_offset < minimum ||
        string_offset >= (uint64_t)data_struct->total_size)
        return false;
    *relative_offset = string_offset;
    *size = data_struct->total_size - string_offset;
    return true;
}

static void xx_macho_record_stream_free(void *pointer) {
    xx_macho_record_stream *stream = (xx_macho_record_stream *)pointer;
    if (!stream) return;
    if (stream->fields) xx_mem_free(stream->fields);
    xx_mem_free(stream);
}

static bool xx_macho_populate_string_record(
    Abstractformat *format, xx_data_struct_record *record,
    int64_t parent_offset, const xx_data_struct_field_desc *field) {
    uint64_t requested;
    size_t limit;
    size_t length;
    char *text = NULL;
    wchar_t *display = NULL;
    bool result = false;
    if (!format || !format->device || !record || !field ||
        field->size <= 0 || field->rel_offset < 0 ||
        parent_offset > INT64_MAX - field->rel_offset)
        return false;
    requested = (uint64_t)field->size;
    limit = (size_t)(requested > 4096U ? 4096U : requested);
    text = (char *)xx_mem_alloc(limit + 1U);
    if (!text) return false;
    for (length = 0U; length < limit; ++length) {
        text[length] = (char)xx_io_get_u8(
            format->device, parent_offset + field->rel_offset +
                                (int64_t)length);
        if (text[length] == '\0') break;
    }
    if (length == limit) text[length] = '\0';
    else text[length] = '\0';
    display = xx_str_ansi_to_unicode(text);
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_var_set_str(&record->value, text) && display &&
        xx_data_struct_record_set_display_value(record, display))
        result = true;
    if (display) xx_str_wfree(display);
    xx_mem_free(text);
    if (!result) xx_data_struct_record_cleanup(record);
    return result;
}

static bool xx_macho_populate_bytes_record(
    Abstractformat *format, xx_data_struct_record *record,
    int64_t parent_offset, const xx_data_struct_field_desc *field) {
    uint8_t *bytes;
    wchar_t *display;
    size_t size;
    size_t index;
    bool result = false;
    if (!format || !format->device || !record || !field ||
        field->size <= 0 || (uint64_t)field->size > SIZE_MAX ||
        parent_offset > INT64_MAX - field->rel_offset)
        return false;
    size = (size_t)field->size;
    bytes = (uint8_t *)xx_mem_alloc(size);
    display = (wchar_t *)xx_mem_alloc((size * 2U + 3U) * sizeof(wchar_t));
    if (!bytes || !display) goto cleanup;
    display[0] = L'0';
    display[1] = L'x';
    for (index = 0U; index < size; ++index) {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        bytes[index] = xx_io_get_u8(
            format->device, parent_offset + field->rel_offset +
                                (int64_t)index);
        display[2U + index * 2U] = digits[bytes[index] >> 4U];
        display[3U + index * 2U] = digits[bytes[index] & 15U];
    }
    display[2U + size * 2U] = L'\0';
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_var_set_bytes(&record->value, bytes, size) &&
        xx_data_struct_record_set_display_value(record, display))
        result = true;
cleanup:
    if (bytes) xx_mem_free(bytes);
    if (display) xx_mem_free(display);
    if (!result) xx_data_struct_record_cleanup(record);
    return result;
}

static bool xx_macho_populate_unsigned_logical_record(
    xx_data_struct_record_state *state,
    const xx_data_struct_field_desc *field, uint64_t value) {
    xx_data_struct_record *record;
    wchar_t display[32];
    bool result = false;
    if (!state || !field) return false;
    xx_data_struct_record_init(&state->current_record);
    record = &state->current_record;
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (xx_rt_wcscmp(field->type, L"bool") == 0) {
        xx_var_set_bool(&record->value, value != 0U);
        (void)xx_macho_display_from_ascii(
            display, XX_MACHO_FIELDS_COUNT(display),
            value != 0U ? "true" : "false");
    } else if (xx_rt_wcscmp(field->type, L"uint8") == 0) {
        xx_var_set_u8(&record->value, (uint8_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else if (xx_rt_wcscmp(field->type, L"uint16") == 0) {
        xx_var_set_u16(&record->value, (uint16_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else if (xx_rt_wcscmp(field->type, L"uint32") == 0) {
        xx_var_set_u32(&record->value, (uint32_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else if (xx_rt_wcscmp(field->type, L"uint64") == 0) {
        xx_var_set_u64(&record->value, value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else {
        xx_data_struct_record_cleanup(record);
        return false;
    }
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_data_struct_record_set_display_value(record, display))
        result = true;
    if (!result) xx_data_struct_record_cleanup(record);
    return result;
}

static bool xx_macho_populate_packed_logical_field(
    Abstractformat *format, xx_data_struct_record_state *state,
    const xx_data_struct_field_desc *field, bool big_endian) {
    uint32_t packed;
    uint64_t value;
    uint32_t id;
    if (!format || !format->device || !state || !field) return false;
    id = state->parent_struct.id;
    if (id == XX_MACHO_DATA_STRUCT_DYLIB_MODULE ||
        id == XX_MACHO_DATA_STRUCT_DYLIB_MODULE_64) {
        packed = xx_io_get_u32(
            format->device,
            state->parent_struct.offset + field->rel_offset,
            big_endian);
        if (xx_rt_wcscmp(field->name, L"iinit") == 0 ||
            xx_rt_wcscmp(field->name, L"ninit") == 0)
            value = packed & UINT32_C(0xffff);
        else if (xx_rt_wcscmp(field->name, L"iterm") == 0 ||
                 xx_rt_wcscmp(field->name, L"nterm") == 0)
            value = packed >> 16U;
        else
            return false;
    } else if (id == XX_MACHO_DATA_STRUCT_DYLIB_REFERENCE) {
        packed = xx_io_get_u32(
            format->device, state->parent_struct.offset, big_endian);
        if (xx_rt_wcscmp(field->name, L"isym") == 0)
            value = big_endian ? packed >> 8U
                               : packed & UINT32_C(0x00ffffff);
        else if (xx_rt_wcscmp(field->name, L"flags") == 0)
            value = big_endian ? packed & UINT32_C(0xff)
                               : packed >> 24U;
        else
            return false;
    } else if (id == XX_MACHO_DATA_STRUCT_TWOLEVEL_HINT) {
        packed = xx_io_get_u32(
            format->device, state->parent_struct.offset, big_endian);
        if (xx_rt_wcscmp(field->name, L"isub_image") == 0)
            value = big_endian ? packed >> 24U
                               : packed & UINT32_C(0xff);
        else if (xx_rt_wcscmp(field->name, L"itoc") == 0)
            value = big_endian ? packed & UINT32_C(0x00ffffff)
                               : packed >> 8U;
        else
            return false;
    } else if (id == XX_MACHO_DATA_STRUCT_RELOCATION_INFO) {
        packed = xx_io_get_u32(
            format->device, state->parent_struct.offset + 4,
            big_endian);
        if (xx_rt_wcscmp(field->name, L"r_symbolnum") == 0)
            value = big_endian ? packed >> 8U
                               : packed & UINT32_C(0x00ffffff);
        else if (xx_rt_wcscmp(field->name, L"r_pcrel") == 0)
            value = (packed >> (big_endian ? 7U : 24U)) & 1U;
        else if (xx_rt_wcscmp(field->name, L"r_length") == 0)
            value = (packed >> (big_endian ? 5U : 25U)) & 3U;
        else if (xx_rt_wcscmp(field->name, L"r_extern") == 0)
            value = (packed >> (big_endian ? 4U : 27U)) & 1U;
        else if (xx_rt_wcscmp(field->name, L"r_type") == 0)
            value = big_endian ? packed & UINT32_C(0xf)
                               : packed >> 28U;
        else
            return false;
    } else if (id == XX_MACHO_DATA_STRUCT_SCATTERED_RELOCATION_INFO) {
        packed = xx_io_get_u32(
            format->device, state->parent_struct.offset, big_endian);
        if (xx_rt_wcscmp(field->name, L"r_scattered") == 0)
            value = packed >> 31U;
        else if (xx_rt_wcscmp(field->name, L"r_pcrel") == 0)
            value = (packed >> 30U) & 1U;
        else if (xx_rt_wcscmp(field->name, L"r_length") == 0)
            value = (packed >> 28U) & 3U;
        else if (xx_rt_wcscmp(field->name, L"r_type") == 0)
            value = (packed >> 24U) & UINT32_C(0xf);
        else if (xx_rt_wcscmp(field->name, L"r_address") == 0)
            value = packed & UINT32_C(0x00ffffff);
        else
            return false;
    } else {
        return false;
    }
    return xx_macho_populate_unsigned_logical_record(state, field, value);
}

static bool xx_macho_populate_chained_import_bitfield(
    Abstractformat *format, xx_data_struct_record_state *state,
    const xx_data_struct_field_desc *field, bool big_endian) {
    xx_data_struct_record *record;
    uint64_t packed;
    uint64_t value;
    wchar_t display[32];
    bool is_64;
    bool is_weak = false;
    bool result = false;
    if (!format || !format->device || !state || !field) return false;
    is_64 = state->parent_struct.id ==
            XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND64;
    packed = is_64
                 ? xx_io_get_u64(format->device,
                                 state->parent_struct.offset,
                                 big_endian)
                 : xx_io_get_u32(format->device,
                                 state->parent_struct.offset,
                                 big_endian);
    if (xx_rt_wcscmp(field->name, L"lib_ordinal") == 0) {
        value = packed & (is_64 ? UINT64_C(0xffff) : UINT64_C(0xff));
    } else if (xx_rt_wcscmp(field->name, L"weak_import") == 0) {
        value = (packed >> (is_64 ? 16U : 8U)) & UINT64_C(1);
        is_weak = true;
    } else if (xx_rt_wcscmp(field->name, L"reserved") == 0 && is_64) {
        value = (packed >> 17U) & UINT64_C(0x7fff);
    } else if (xx_rt_wcscmp(field->name, L"name_offset") == 0) {
        value = is_64 ? packed >> 32U
                      : (packed >> 9U) & UINT64_C(0x7fffff);
    } else {
        return false;
    }

    xx_data_struct_record_init(&state->current_record);
    record = &state->current_record;
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (is_weak) {
        xx_var_set_bool(&record->value, value != 0U);
        (void)xx_macho_display_from_ascii(
            display, XX_MACHO_FIELDS_COUNT(display),
            value != 0U ? "true" : "false");
    } else if (xx_rt_wcscmp(field->name, L"lib_ordinal") == 0) {
        if (is_64)
            xx_var_set_u16(&record->value, (uint16_t)value);
        else
            xx_var_set_u8(&record->value, (uint8_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else if (xx_rt_wcscmp(field->name, L"reserved") == 0) {
        xx_var_set_u16(&record->value, (uint16_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    } else {
        xx_var_set_u32(&record->value, (uint32_t)value);
        xx_macho_display_u64(display, XX_MACHO_FIELDS_COUNT(display), value);
    }
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_data_struct_record_set_display_value(record, display))
        result = true;
    if (!result) xx_data_struct_record_cleanup(record);
    return result;
}

typedef struct xx_macho_chained_bitfield_s {
    uint32_t id;
    const wchar_t *name;
    uint8_t shift;
    uint8_t bits;
    bool is_signed;
} xx_macho_chained_bitfield;

#define XX_MACHO_CHAIN_BITS(id_, name_, shift_, bits_) \
    {id_, L##name_, shift_, bits_, false}
#define XX_MACHO_CHAIN_SIGNED_BITS(id_, name_, shift_, bits_) \
    {id_, L##name_, shift_, bits_, true}

static const xx_macho_chained_bitfield xx_macho_chained_bitfields[] = {
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE, "target", 0, 43),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE, "high8", 43, 8),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "ordinal", 0, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "zero", 16, 16),
    XX_MACHO_CHAIN_SIGNED_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "addend", 32, 19),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "target", 0, 32),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "diversity", 32, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "addrDiv", 48, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "key", 49, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_REBASE, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "ordinal", 0, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "zero", 16, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "diversity", 32, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "addrDiv", 48, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "key", 49, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "ordinal", 0, 24),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "zero", 24, 8),
    XX_MACHO_CHAIN_SIGNED_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "addend", 32, 19),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_BIND24, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "ordinal", 0, 24),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "zero", 24, 8),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "diversity", 32, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "addrDiv", 48, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "key", 49, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "next", 51, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "bind", 62, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_BIND24, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE, "target", 0, 36),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE, "high8", 36, 8),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE, "reserved", 44, 7),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE, "next", 51, 12),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_REBASE, "bind", 63, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND, "ordinal", 0, 24),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND, "addend", 24, 8),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND, "reserved", 32, 19),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND, "next", 51, 12),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_BIND, "bind", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "target", 0, 30),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "cacheLevel", 30, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "diversity", 32, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "addrDiv", 48, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "key", 49, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "next", 51, 12),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_64_KERNEL_CACHE_REBASE, "isAuth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_REBASE, "target", 0, 26),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_REBASE, "next", 26, 5),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_REBASE, "bind", 31, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND, "ordinal", 0, 20),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND, "addend", 20, 6),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND, "next", 26, 5),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_BIND, "bind", 31, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_CACHE_REBASE, "target", 0, 30),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_CACHE_REBASE, "next", 30, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_FIRMWARE_REBASE, "target", 0, 26),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_32_FIRMWARE_REBASE, "next", 26, 6),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE, "runtimeOffset", 0, 34),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE, "high8", 34, 8),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE, "unused", 42, 10),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE, "next", 52, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_REBASE, "auth", 63, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "runtimeOffset", 0, 34),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "diversity", 34, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "addrDiv", 50, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "keyIsData", 51, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "next", 52, 11),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SHARED_CACHE_AUTH_REBASE, "auth", 63, 1),

    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE, "targetSegOffset", 0, 28),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE, "targetSegIndex", 28, 4),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE, "padding", 32, 19),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE, "next", 51, 12),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_SEGMENTED_REBASE, "auth", 63, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "targetSegOffset", 0, 28),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "targetSegIndex", 28, 4),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "diversity", 32, 16),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "addrDiv", 48, 1),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "key", 49, 2),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "next", 51, 12),
    XX_MACHO_CHAIN_BITS(XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE, "auth", 63, 1)
};

#undef XX_MACHO_CHAIN_BITS
#undef XX_MACHO_CHAIN_SIGNED_BITS

static bool xx_macho_is_chained_pointer_id(uint32_t id) {
    return id >= XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_REBASE &&
           id <= XX_MACHO_DATA_STRUCT_CHAINED_PTR_ARM64E_AUTH_SEGMENTED_REBASE;
}

static bool xx_macho_populate_chained_pointer_bitfield(
    Abstractformat *format, xx_data_struct_record_state *state,
    const xx_data_struct_field_desc *field, bool big_endian) {
    const xx_macho_chained_bitfield *bits = NULL;
    xx_data_struct_record *record;
    uint64_t packed;
    uint64_t mask;
    uint64_t value;
    size_t index;
    wchar_t display[32];
    bool result = false;
    if (!format || !format->device || !state || !field) return false;
    for (index = 0U; index < XX_MACHO_FIELDS_COUNT(xx_macho_chained_bitfields);
        ++index) {
        if (xx_macho_chained_bitfields[index].id == state->parent_struct.id &&
            xx_rt_wcscmp(xx_macho_chained_bitfields[index].name,
                         field->name) == 0) {
            bits = &xx_macho_chained_bitfields[index];
            break;
        }
    }
    if (!bits || bits->bits == 0U || bits->bits >= 64U) return false;
    packed = field->size == 4
                 ? xx_io_get_u32(format->device, state->parent_struct.offset,
                                 big_endian)
                 : xx_io_get_u64(format->device, state->parent_struct.offset,
                                 big_endian);
    mask = (UINT64_C(1) << bits->bits) - UINT64_C(1);
    value = (packed >> bits->shift) & mask;
    xx_data_struct_record_init(&state->current_record);
    record = &state->current_record;
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    if (bits->is_signed) {
        int64_t signed_value = (value & (UINT64_C(1) << (bits->bits - 1U)))
                                   ? (int64_t)(value | ~mask)
                                   : (int64_t)value;
        xx_var_set_i32(&record->value, (int32_t)signed_value);
        xx_macho_display_i64(display, XX_MACHO_FIELDS_COUNT(display),
                             signed_value);
    } else {
        return xx_macho_populate_unsigned_logical_record(state, field, value);
    }
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_data_struct_record_set_display_value(record, display))
        result = true;
    if (!result) xx_data_struct_record_cleanup(record);
    return result;
}

static bool xx_macho_populate_record(
    Abstractformat *format, xx_data_struct_record_state *state,
    size_t index) {
    xx_macho_record_stream *stream;
    const xx_data_struct_field_desc *field;
    if (!format || !format->device || !state ||
        !(stream = (xx_macho_record_stream *)state->internal_state) ||
        index >= stream->count)
        return false;
    field = &stream->fields[index];
    if (xx_macho_is_chained_pointer_id(state->parent_struct.id) &&
        xx_rt_wcscmp(field->name, L"raw") != 0)
        return xx_macho_populate_chained_pointer_bitfield(
            format, state, field, stream->big_endian);
    if ((field->property & XX_DATA_STRUCT_RECORD_PROPERTY_STRING) != 0U)
        return xx_macho_populate_string_record(
            format, &state->current_record, state->parent_struct.offset,
            field);
    if ((state->parent_struct.id == XX_MACHO_DATA_STRUCT_CHAINED_IMPORT ||
         state->parent_struct.id ==
             XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND ||
         state->parent_struct.id ==
             XX_MACHO_DATA_STRUCT_CHAINED_IMPORT_ADDEND64) &&
        xx_rt_wcscmp(field->name, L"import") != 0 &&
        xx_rt_wcscmp(field->name, L"addend") != 0)
        return xx_macho_populate_chained_import_bitfield(
            format, state, field, stream->big_endian);
    if (((state->parent_struct.id == XX_MACHO_DATA_STRUCT_DYLIB_MODULE ||
          state->parent_struct.id ==
              XX_MACHO_DATA_STRUCT_DYLIB_MODULE_64) &&
         (xx_rt_wcscmp(field->name, L"iinit") == 0 ||
          xx_rt_wcscmp(field->name, L"iterm") == 0 ||
          xx_rt_wcscmp(field->name, L"ninit") == 0 ||
          xx_rt_wcscmp(field->name, L"nterm") == 0)) ||
        (state->parent_struct.id == XX_MACHO_DATA_STRUCT_DYLIB_REFERENCE &&
         xx_rt_wcscmp(field->name, L"reference") != 0) ||
        (state->parent_struct.id == XX_MACHO_DATA_STRUCT_TWOLEVEL_HINT &&
         xx_rt_wcscmp(field->name, L"hint") != 0) ||
        (state->parent_struct.id == XX_MACHO_DATA_STRUCT_RELOCATION_INFO &&
         xx_rt_wcscmp(field->name, L"r_address") != 0 &&
         xx_rt_wcscmp(field->name, L"r_info") != 0) ||
        (state->parent_struct.id ==
             XX_MACHO_DATA_STRUCT_SCATTERED_RELOCATION_INFO &&
         xx_rt_wcscmp(field->name, L"r_word0") != 0 &&
         xx_rt_wcscmp(field->name, L"r_value") != 0))
        return xx_macho_populate_packed_logical_field(
            format, state, field, stream->big_endian);
    if (field->size > 8)
        return xx_macho_populate_bytes_record(
            format, &state->current_record, state->parent_struct.offset,
            field);
    if (xx_rt_wcscmp(field->type, L"int16") == 0 ||
        xx_rt_wcscmp(field->type, L"int32") == 0 ||
        xx_rt_wcscmp(field->type, L"int64") == 0) {
        wchar_t display[32];
        int64_t value;
        if (!xx_data_struct_record_populate(
                &state->current_record, format->device,
                state->parent_struct.offset, field,
                stream->big_endian))
            return false;
        if (field->size == 8) {
            value = (int64_t)xx_io_get_u64(
                format->device,
                state->parent_struct.offset + field->rel_offset,
                stream->big_endian);
            xx_var_set_i64(&state->current_record.value, value);
        } else if (field->size == 4) {
            value = (int32_t)xx_io_get_u32(
                format->device,
                state->parent_struct.offset + field->rel_offset,
                stream->big_endian);
            xx_var_set_i32(&state->current_record.value, (int32_t)value);
        } else {
            value = (int16_t)xx_io_get_u16(
                format->device,
                state->parent_struct.offset + field->rel_offset,
                stream->big_endian);
            xx_var_set_i16(&state->current_record.value, (int16_t)value);
        }
        xx_macho_display_i64(display, XX_MACHO_FIELDS_COUNT(display), value);
        return xx_data_struct_record_set_display_value(
            &state->current_record, display);
    }
    return xx_data_struct_record_populate(
        &state->current_record, format->device,
        state->parent_struct.offset, field, stream->big_endian);
}

static xx_data_struct_record_state *
xx_macho_create_data_struct_records_reading(
    Abstractformat *format, const xx_data_struct *data_struct,
    xx_pd_struct *pd) {
    const xx_data_struct_field_desc *static_fields = NULL;
    size_t static_count = 0U;
    size_t valid_count = 0U;
    size_t index;
    int64_t string_offset = 0;
    int64_t string_size = 0;
    bool has_string;
    bool code_signature;
    int64_t field_limit;
    xx_data_struct_record_state *state = NULL;
    xx_macho_record_stream *stream = NULL;
    xx_macho *macho = (xx_macho *)format;
    if (!format || !format->device || !data_struct ||
        data_struct->type == XX_DATA_STRUCT_TYPE_RAW_DATA ||
        xx_pd_is_stopped(pd))
        return NULL;
    if (data_struct->offset < format->base_address ||
        data_struct->total_size < 0 ||
        !xx_macho_data_range(
            macho,
            (uint64_t)(data_struct->offset - format->base_address),
            (uint64_t)data_struct->total_size))
        return NULL;
    (void)xx_macho_get_static_fields(data_struct->id, &static_fields,
                                     &static_count);
    has_string = xx_macho_command_string_field(
        macho, data_struct, &string_offset, &string_size);
    field_limit = data_struct->entry_size;
    if (field_limit <= 0 || field_limit > data_struct->total_size)
        field_limit = data_struct->total_size;
    code_signature =
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SUPERBLOB ||
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB_INDEX ||
        data_struct->id == XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_BLOB ||
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_SCATTER ||
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENTS ||
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_REQUIREMENT ||
        data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_CODE_DIRECTORY;
    if (data_struct->id ==
            XX_MACHO_DATA_STRUCT_CODE_SIGNATURE_CODE_DIRECTORY &&
        field_limit >= 12) {
        uint32_t version = xx_io_get_u32(
            format->device, data_struct->offset + 8, true);
        int64_t version_limit =
            (int64_t)xx_macho_code_directory_fixed_size(version);
        if (field_limit > version_limit) field_limit = version_limit;
    }
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 && field_limit >= 0 &&
            static_fields[index].rel_offset <= field_limit &&
            static_fields[index].size <=
                field_limit - static_fields[index].rel_offset)
            ++valid_count;
    }
    if (valid_count == 0U && !has_string) return NULL;
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_macho_record_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) goto fail;
    stream->fields = (xx_data_struct_field_desc *)xx_mem_alloc(
        (valid_count + (has_string ? 1U : 0U)) *
        sizeof(*stream->fields));
    if (!stream->fields) goto fail;
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 && field_limit >= 0 &&
            static_fields[index].rel_offset <= field_limit &&
            static_fields[index].size <=
                field_limit - static_fields[index].rel_offset)
            stream->fields[stream->count++] = static_fields[index];
    }
    if (data_struct->id == XX_MACHO_DATA_STRUCT_SUB_FRAMEWORK_COMMAND &&
        stream->count >= 3U)
        stream->fields[2].name = L"umbrella";
    else if (data_struct->id ==
                 XX_MACHO_DATA_STRUCT_SUB_UMBRELLA_COMMAND &&
             stream->count >= 3U)
        stream->fields[2].name = L"sub_umbrella";
    else if (data_struct->id == XX_MACHO_DATA_STRUCT_SUB_CLIENT_COMMAND &&
             stream->count >= 3U)
        stream->fields[2].name = L"client";
    else if (data_struct->id == XX_MACHO_DATA_STRUCT_SUB_LIBRARY_COMMAND &&
             stream->count >= 3U)
        stream->fields[2].name = L"sub_library";
    if (has_string) {
        xx_data_struct_field_desc *field =
            &stream->fields[stream->count++];
        field->name = L"string";
        field->type = L"char[]";
        field->rel_offset = string_offset;
        field->size = string_size;
        field->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
    }
    stream->big_endian = code_signature ||
                         format->endian == XX_ENDIAN_BIG;
    xx_data_struct_record_state_init(state, format, data_struct);
    state->internal_state = stream;
    state->free_internal = xx_macho_record_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_macho_populate_record(format, state, 0U)) goto fail_state;
    state->current_index = 0;
    state->has_record = true;
    return state;
fail_state:
    xx_data_struct_record_state_free(state);
    return NULL;
fail:
    if (stream) xx_macho_record_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

static const xx_data_struct_record *xx_macho_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

static bool xx_macho_data_struct_record_move_to_next(
    Abstractformat *format, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    size_t next;
    if (!format || !state || state->format != format ||
        xx_pd_is_stopped(pd)) {
        if (state) state->has_record = false;
        return false;
    }
    next = (size_t)(state->current_index + 1);
    if (state->current_index < 0 ||
        next >= (size_t)state->total_records) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_macho_populate_record(format, state, next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

static void xx_macho_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state) {
    (void)format;
    xx_data_struct_record_state_free(state);
}

void xx_macho_setup_data_struct_callbacks(xx_macho *macho) {
    if (!macho) return;
    macho->format.data_struct_id_to_string =
        xx_macho_data_struct_id_to_string;
    macho->format.data_struct_string_to_id =
        xx_macho_data_struct_string_to_id;
    macho->format.create_data_structs_reading =
        xx_macho_create_data_structs_reading;
    macho->format.get_current_data_struct =
        xx_macho_get_current_data_struct;
    macho->format.data_struct_move_to_next =
        xx_macho_data_struct_move_to_next;
    macho->format.free_data_structs_reading =
        xx_macho_free_data_structs_reading;
    macho->format.create_data_struct_records_reading =
        xx_macho_create_data_struct_records_reading;
    macho->format.get_current_data_struct_record =
        xx_macho_get_current_data_struct_record;
    macho->format.data_struct_record_move_to_next =
        xx_macho_data_struct_record_move_to_next;
    macho->format.free_data_struct_records_reading =
        xx_macho_free_data_struct_records_reading;
}
