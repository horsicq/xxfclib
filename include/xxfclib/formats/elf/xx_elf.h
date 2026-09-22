/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_ELF_H
#define XXFCLIB_FORMAT_ELF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ELF_CLASS_32 UINT8_C(1)
#define XX_ELF_CLASS_64 UINT8_C(2)
#define XX_ELF_DATA_LSB UINT8_C(1)
#define XX_ELF_DATA_MSB UINT8_C(2)

#define XX_ELF_TYPE_REL  UINT16_C(1)
#define XX_ELF_TYPE_EXEC UINT16_C(2)
#define XX_ELF_TYPE_DYN  UINT16_C(3)
#define XX_ELF_TYPE_CORE UINT16_C(4)

#define XX_ELF_PROGRAM_NULL       UINT32_C(0)
#define XX_ELF_PROGRAM_LOAD       UINT32_C(1)
#define XX_ELF_PROGRAM_DYNAMIC    UINT32_C(2)
#define XX_ELF_PROGRAM_INTERP     UINT32_C(3)
#define XX_ELF_PROGRAM_NOTE       UINT32_C(4)
#define XX_ELF_PROGRAM_PHDR       UINT32_C(6)
#define XX_ELF_PROGRAM_TLS        UINT32_C(7)
#define XX_ELF_PROGRAM_GNU_PROPERTY UINT32_C(0x6474e553)
#define XX_ELF_PROGRAM_MIPS_REGINFO  UINT32_C(0x70000000)
#define XX_ELF_PROGRAM_MIPS_OPTIONS  UINT32_C(0x70000002)
#define XX_ELF_PROGRAM_MIPS_ABIFLAGS UINT32_C(0x70000003)

#define XX_ELF_SECTION_NULL          UINT32_C(0)
#define XX_ELF_SECTION_PROGBITS      UINT32_C(1)
#define XX_ELF_SECTION_SYMTAB        UINT32_C(2)
#define XX_ELF_SECTION_STRTAB        UINT32_C(3)
#define XX_ELF_SECTION_RELA          UINT32_C(4)
#define XX_ELF_SECTION_HASH          UINT32_C(5)
#define XX_ELF_SECTION_DYNAMIC       UINT32_C(6)
#define XX_ELF_SECTION_NOTE          UINT32_C(7)
#define XX_ELF_SECTION_NOBITS        UINT32_C(8)
#define XX_ELF_SECTION_REL           UINT32_C(9)
#define XX_ELF_SECTION_DYNSYM        UINT32_C(11)
#define XX_ELF_SECTION_INIT_ARRAY    UINT32_C(14)
#define XX_ELF_SECTION_FINI_ARRAY    UINT32_C(15)
#define XX_ELF_SECTION_PREINIT_ARRAY UINT32_C(16)
#define XX_ELF_SECTION_GROUP         UINT32_C(17)
#define XX_ELF_SECTION_SYMTAB_SHNDX  UINT32_C(18)
#define XX_ELF_SECTION_RELR          UINT32_C(19)
#define XX_ELF_SECTION_GNU_HASH      UINT32_C(0x6ffffff6)
#define XX_ELF_SECTION_GNU_LIBLIST   UINT32_C(0x6ffffff7)
#define XX_ELF_SECTION_GNU_VERDEF    UINT32_C(0x6ffffffd)
#define XX_ELF_SECTION_GNU_VERNEED   UINT32_C(0x6ffffffe)
#define XX_ELF_SECTION_GNU_VERSYM    UINT32_C(0x6fffffff)
#define XX_ELF_SECTION_SUNW_CAPINFO  UINT32_C(0x6ffffff0)
#define XX_ELF_SECTION_SUNW_CAP      UINT32_C(0x6ffffff5)
#define XX_ELF_SECTION_SUNW_MOVE     UINT32_C(0x6ffffffa)
#define XX_ELF_SECTION_SUNW_SYMINFO  UINT32_C(0x6ffffffc)
#define XX_ELF_SECTION_MIPS_LIBLIST  UINT32_C(0x70000000)
#define XX_ELF_SECTION_MIPS_CONFLICT UINT32_C(0x70000002)
#define XX_ELF_SECTION_MIPS_GPTAB    UINT32_C(0x70000003)
#define XX_ELF_SECTION_MIPS_REGINFO  UINT32_C(0x70000006)
#define XX_ELF_SECTION_MIPS_OPTIONS  UINT32_C(0x7000000d)
#define XX_ELF_SECTION_MIPS_ABIFLAGS UINT32_C(0x7000002a)
#define XX_ELF_SECTION_LLVM_CALL_GRAPH_PROFILE UINT32_C(0x6fff4c09)

#define XX_ELF_SECTION_FLAG_ALLOC      UINT64_C(0x2)
#define XX_ELF_SECTION_FLAG_COMPRESSED UINT64_C(0x800)

/** Fixed and bounded variable ELF structures exposed by the data stream. */
typedef enum xx_elf_data_struct_id_e {
    XX_ELF_DATA_STRUCT_UNKNOWN = 0,
    XX_ELF_DATA_STRUCT_ELF_HEADER32,
    XX_ELF_DATA_STRUCT_ELF_HEADER64,
    XX_ELF_DATA_STRUCT_PROGRAM_HEADER32,
    XX_ELF_DATA_STRUCT_PROGRAM_HEADER64,
    XX_ELF_DATA_STRUCT_SECTION_HEADER32,
    XX_ELF_DATA_STRUCT_SECTION_HEADER64,
    XX_ELF_DATA_STRUCT_SYMBOL32,
    XX_ELF_DATA_STRUCT_SYMBOL64,
    XX_ELF_DATA_STRUCT_REL32,
    XX_ELF_DATA_STRUCT_REL64,
    XX_ELF_DATA_STRUCT_RELA32,
    XX_ELF_DATA_STRUCT_RELA64,
    XX_ELF_DATA_STRUCT_RELR32,
    XX_ELF_DATA_STRUCT_RELR64,
    XX_ELF_DATA_STRUCT_DYNAMIC32,
    XX_ELF_DATA_STRUCT_DYNAMIC64,
    XX_ELF_DATA_STRUCT_COMPRESSION_HEADER32,
    XX_ELF_DATA_STRUCT_COMPRESSION_HEADER64,
    XX_ELF_DATA_STRUCT_NOTE_HEADER,
    XX_ELF_DATA_STRUCT_SYSV_HASH_HEADER,
    XX_ELF_DATA_STRUCT_SYSV_HASH_BUCKET,
    XX_ELF_DATA_STRUCT_SYSV_HASH_CHAIN,
    XX_ELF_DATA_STRUCT_GNU_HASH_HEADER,
    XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM32,
    XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM64,
    XX_ELF_DATA_STRUCT_GNU_HASH_BUCKET,
    XX_ELF_DATA_STRUCT_GNU_HASH_CHAIN,
    XX_ELF_DATA_STRUCT_GROUP_WORD,
    XX_ELF_DATA_STRUCT_SYMTAB_SHNDX,
    XX_ELF_DATA_STRUCT_ADDRESS32,
    XX_ELF_DATA_STRUCT_ADDRESS64,
    XX_ELF_DATA_STRUCT_VERDEF,
    XX_ELF_DATA_STRUCT_VERDAUX,
    XX_ELF_DATA_STRUCT_VERNEED,
    XX_ELF_DATA_STRUCT_VERNAUX,
    XX_ELF_DATA_STRUCT_VERSYM,
    XX_ELF_DATA_STRUCT_SYMINFO,
    XX_ELF_DATA_STRUCT_AUXV32,
    XX_ELF_DATA_STRUCT_AUXV64,
    XX_ELF_DATA_STRUCT_NT_FILE_HEADER32,
    XX_ELF_DATA_STRUCT_NT_FILE_HEADER64,
    XX_ELF_DATA_STRUCT_NT_FILE_ENTRY32,
    XX_ELF_DATA_STRUCT_NT_FILE_ENTRY64,
    XX_ELF_DATA_STRUCT_GNU_ABI_TAG,
    XX_ELF_DATA_STRUCT_GNU_HWCAP_HEADER,
    XX_ELF_DATA_STRUCT_GNU_PROPERTY_HEADER,
    XX_ELF_DATA_STRUCT_LINUX_SIGINFO,
    XX_ELF_DATA_STRUCT_ANDROID_IDENT_PREFIX,
    XX_ELF_DATA_STRUCT_MOVE32,
    XX_ELF_DATA_STRUCT_MOVE64,
    XX_ELF_DATA_STRUCT_LIB32,
    XX_ELF_DATA_STRUCT_LIB64,
    XX_ELF_DATA_STRUCT_SOLARIS_CAP32,
    XX_ELF_DATA_STRUCT_SOLARIS_CAP64,
    XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO32,
    XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO64,
    XX_ELF_DATA_STRUCT_MIPS_REGINFO32,
    XX_ELF_DATA_STRUCT_MIPS_REGINFO64,
    XX_ELF_DATA_STRUCT_MIPS_OPTIONS,
    XX_ELF_DATA_STRUCT_MIPS_OPTIONS_HW,
    XX_ELF_DATA_STRUCT_MIPS_GPTAB,
    XX_ELF_DATA_STRUCT_MIPS_CONFLICT32,
    XX_ELF_DATA_STRUCT_MIPS_CONFLICT64,
    XX_ELF_DATA_STRUCT_MIPS_ABI_FLAGS,
    XX_ELF_DATA_STRUCT_LLVM_CGPROFILE,
    XX_ELF_DATA_STRUCT_STRING_DATA,
    XX_ELF_DATA_STRUCT_NOTE_NAME,
    XX_ELF_DATA_STRUCT_NOTE_DESC,
    XX_ELF_DATA_STRUCT_PROPERTY_DATA,
    XX_ELF_DATA_STRUCT_RAW_DATA,
    XX_ELF_DATA_STRUCT_LAST = XX_ELF_DATA_STRUCT_RAW_DATA
} xx_elf_data_struct_id_t;

/* The original conflict ID described only the 32-bit ABI record. */
#define XX_ELF_DATA_STRUCT_MIPS_CONFLICT \
    XX_ELF_DATA_STRUCT_MIPS_CONFLICT32

typedef struct xx_elf_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} xx_elf_program_header;

typedef struct xx_elf_section_header {
    uint32_t name_offset;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t address_alignment;
    uint64_t entry_size;
} xx_elf_section_header;

typedef struct xx_elf {
    Abstractformat format;
    uint8_t elf_class;
    uint8_t data_encoding;
    uint8_t ident_version;
    uint8_t os_abi;
    uint8_t abi_version;
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry_point;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint64_t program_header_count;
    uint16_t section_header_entry_size;
    uint64_t section_header_count;
    uint64_t section_name_index;
    xx_elf_program_header *program_headers;
    xx_elf_section_header *section_headers;
} xx_elf;

typedef xx_elf xx_elf_t;
typedef xx_elf XELF;

XXFC_API void xx_elf_init(xx_elf *elf, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_elf *xx_elf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_elf_destroy(xx_elf *elf);
XXFC_API void xx_elf_free(xx_elf *elf);

XXFC_API bool xx_elf_check_is_valid(Abstractformat *format,
                                    xx_pd_struct *pd);
XXFC_API bool xx_elf_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd);
XXFC_API int64_t xx_elf_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd);
XXFC_API bool xx_elf_get_memory_map(Abstractformat *format,
                                    xx_memory_map_mode_t mode,
                                    xx_memory_map *output,
                                    xx_pd_struct *pd);

XXFC_API bool xx_elf_is_64(const xx_elf *elf);
XXFC_API uint16_t xx_elf_get_machine(const xx_elf *elf);
XXFC_API uint16_t xx_elf_get_type(const xx_elf *elf);
XXFC_API uint64_t xx_elf_get_entry_point(const xx_elf *elf);
XXFC_API uint64_t xx_elf_get_number_of_program_headers(const xx_elf *elf);
XXFC_API uint64_t xx_elf_get_number_of_section_headers(const xx_elf *elf);
XXFC_API const xx_elf_program_header *xx_elf_get_program_header(
    const xx_elf *elf, uint64_t index);
XXFC_API const xx_elf_section_header *xx_elf_get_section_header(
    const xx_elf *elf, uint64_t index);

XXFC_API const char *xx_elf_data_struct_id_to_string(Abstractformat *format,
                                                      uint32_t id);
XXFC_API uint32_t xx_elf_data_struct_string_to_id(Abstractformat *format,
                                                   const char *name);
XXFC_API xx_data_struct_state *xx_elf_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_elf_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state);
XXFC_API bool xx_elf_data_struct_move_to_next(
    Abstractformat *format, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_elf_free_data_structs_reading(
    Abstractformat *format, xx_data_struct_state *state);
XXFC_API xx_data_struct_record_state *
xx_elf_create_data_struct_records_reading(Abstractformat *format,
                                           const xx_data_struct *data_struct,
                                           xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_elf_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state);
XXFC_API bool xx_elf_data_struct_record_move_to_next(
    Abstractformat *format, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_elf_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state);

static inline Abstractformat *xx_elf_to_format(xx_elf *elf) {
    return elf ? &elf->format : NULL;
}

static inline const Abstractformat *xx_elf_to_format_const(
    const xx_elf *elf) {
    return elf ? &elf->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ELF_H */
