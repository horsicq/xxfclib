/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_elf_data.h"

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#define ELF_DATA_MAX_ITEMS UINT64_C(262144)
#define ELF_DATA_MAX_LINKED_RECORDS UINT64_C(65536)
#define ELF_DATA_MAX_NAME UINT64_C(4096)
#define ELF_DATA_MAX_DISPLAY UINT64_C(64)

#define ELF_EHDR32_SIZE UINT64_C(52)
#define ELF_EHDR64_SIZE UINT64_C(64)
#define ELF_PHDR32_SIZE UINT64_C(32)
#define ELF_PHDR64_SIZE UINT64_C(56)
#define ELF_SHDR32_SIZE UINT64_C(40)
#define ELF_SHDR64_SIZE UINT64_C(64)
#define ELF_SYM32_SIZE UINT64_C(16)
#define ELF_SYM64_SIZE UINT64_C(24)
#define ELF_REL32_SIZE UINT64_C(8)
#define ELF_REL64_SIZE UINT64_C(16)
#define ELF_RELA32_SIZE UINT64_C(12)
#define ELF_RELA64_SIZE UINT64_C(24)
#define ELF_DYN32_SIZE UINT64_C(8)
#define ELF_DYN64_SIZE UINT64_C(16)
#define ELF_CHDR32_SIZE UINT64_C(12)
#define ELF_CHDR64_SIZE UINT64_C(24)
#define ELF_NHDR_SIZE UINT64_C(12)
#define ELF_VERDEF_SIZE UINT64_C(20)
#define ELF_VERDAUX_SIZE UINT64_C(8)
#define ELF_VERNEED_SIZE UINT64_C(16)
#define ELF_VERNAUX_SIZE UINT64_C(16)

static int elf_display_from_ascii(wchar_t *destination, size_t capacity,
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

static void elf_display_i64(wchar_t *destination, size_t capacity,
                            int64_t value) {
    char ascii[64];
    int length = xx_rt_snprintf(ascii, sizeof(ascii), "%lld",
                                (long long)value);
    if (length < 0 || (size_t)length >= sizeof(ascii) ||
        elf_display_from_ascii(destination, capacity, ascii) < 0)
        destination[0] = L'\0';
}

static void elf_display_u64(wchar_t *destination, size_t capacity,
                            uint64_t value) {
    char ascii[64];
    int length = xx_rt_snprintf(ascii, sizeof(ascii), "%llu",
                                (unsigned long long)value);
    if (length < 0 || (size_t)length >= sizeof(ascii) ||
        elf_display_from_ascii(destination, capacity, ascii) < 0)
        destination[0] = L'\0';
}

static size_t elf_display_append_hex_byte(wchar_t *display, size_t capacity,
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

#define ELF_MACHINE_MIPS UINT16_C(8)
#define ELF_MACHINE_MIPS_RS3_LE UINT16_C(10)
#define ELF_OSABI_SOLARIS UINT8_C(6)
#define ELF_NOTE_NT_AUXV UINT32_C(6)
#define ELF_NOTE_NT_FILE UINT32_C(0x46494c45)
#define ELF_NOTE_NT_SIGINFO UINT32_C(0x53494749)
#define ELF_NOTE_GNU_ABI_TAG UINT32_C(1)
#define ELF_NOTE_GNU_HWCAP UINT32_C(2)
#define ELF_NOTE_GNU_PROPERTY UINT32_C(5)
#define ELF_NOTE_ANDROID_IDENT UINT32_C(1)

#define ELF_FIELD(name_, type_, offset_, size_, property_) \
    {L##name_, L##type_, offset_, size_, property_}
#define ELF_COUNT(a_) (sizeof(a_) / sizeof((a_)[0]))

typedef struct elf_data_stream_s {
    xx_data_struct *items;
    size_t count;
    size_t capacity;
} elf_data_stream;

typedef struct elf_record_stream_s {
    xx_data_struct_field_desc *fields;
    size_t count;
    bool big_endian;
    uint8_t elf_class;
    uint16_t machine;
} elf_record_stream;

static const char *const elf_data_names[] = {
    "UNKNOWN",
    "elf_header32", "elf_header64",
    "program_header32", "program_header64",
    "section_header32", "section_header64",
    "symbol32", "symbol64",
    "rel32", "rel64", "rela32", "rela64", "relr32", "relr64",
    "dynamic32", "dynamic64",
    "compression_header32", "compression_header64", "note_header",
    "sysv_hash_header", "sysv_hash_bucket", "sysv_hash_chain",
    "gnu_hash_header", "gnu_hash_bloom32", "gnu_hash_bloom64",
    "gnu_hash_bucket", "gnu_hash_chain", "group_word", "symtab_shndx",
    "address32", "address64", "verdef", "verdaux", "verneed",
    "vernaux", "versym", "syminfo", "auxv32", "auxv64",
    "nt_file_header32", "nt_file_header64", "nt_file_entry32",
    "nt_file_entry64", "gnu_abi_tag", "gnu_hwcap_header", "gnu_property_header",
    "linux_siginfo", "android_ident_prefix", "move32", "move64", "lib32", "lib64",
    "solaris_cap32", "solaris_cap64", "solaris_capinfo32",
    "solaris_capinfo64", "mips_reginfo32", "mips_reginfo64",
    "mips_options", "mips_options_hw", "mips_gptab", "mips_conflict32",
    "mips_conflict64", "mips_abi_flags", "llvm_cgprofile", "string_data", "note_name",
    "note_desc", "property_data", "raw_data"
};

_Static_assert(ELF_COUNT(elf_data_names) ==
                   (size_t)XX_ELF_DATA_STRUCT_LAST + 1U,
               "ELF data-structure name table is out of sync");

static const xx_data_struct_field_desc elf_ehdr32_fields[] = {
    ELF_FIELD("e_ident_magic", "uint8[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_class", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_data", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_version", "uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_osabi", "uint8", 7, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_abiversion", "uint8", 8, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_pad", "uint8[7]", 9, 7, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    ELF_FIELD("e_type", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_machine", "uint16", 18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_version", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_entry", "Elf32_Addr", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("e_phoff", "Elf32_Off", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("e_shoff", "Elf32_Off", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("e_flags", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("e_ehsize", "uint16", 40, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_phentsize", "uint16", 42, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_phnum", "uint16", 44, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("e_shentsize", "uint16", 46, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_shnum", "uint16", 48, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("e_shstrndx", "uint16", 50, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc elf_ehdr64_fields[] = {
    ELF_FIELD("e_ident_magic", "uint8[4]", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_class", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_data", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_version", "uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_osabi", "uint8", 7, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_abiversion", "uint8", 8, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ei_pad", "uint8[7]", 9, 7, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    ELF_FIELD("e_type", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_machine", "uint16", 18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_version", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("e_entry", "Elf64_Addr", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("e_phoff", "Elf64_Off", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("e_shoff", "Elf64_Off", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("e_flags", "uint32", 48, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("e_ehsize", "uint16", 52, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_phentsize", "uint16", 54, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_phnum", "uint16", 56, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("e_shentsize", "uint16", 58, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("e_shnum", "uint16", 60, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("e_shstrndx", "uint16", 62, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc elf_phdr32_fields[] = {
    ELF_FIELD("p_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("p_offset", "Elf32_Off", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("p_vaddr", "Elf32_Addr", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("p_paddr", "Elf32_Addr", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("p_filesz", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("p_memsz", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("p_flags", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("p_align", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc elf_phdr64_fields[] = {
    ELF_FIELD("p_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("p_flags", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("p_offset", "Elf64_Off", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("p_vaddr", "Elf64_Addr", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("p_paddr", "Elf64_Addr", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("p_filesz", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("p_memsz", "uint64", 40, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("p_align", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc elf_shdr32_fields[] = {
    ELF_FIELD("sh_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("sh_type", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_flags", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("sh_addr", "Elf32_Addr", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("sh_offset", "Elf32_Off", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("sh_size", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("sh_link", "uint32", 24, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_info", "uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_addralign", "uint32", 32, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("sh_entsize", "uint32", 36, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc elf_shdr64_fields[] = {
    ELF_FIELD("sh_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("sh_type", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_flags", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("sh_addr", "Elf64_Addr", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("sh_offset", "Elf64_Off", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("sh_size", "uint64", 32, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("sh_link", "uint32", 40, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_info", "uint32", 44, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("sh_addralign", "uint64", 48, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("sh_entsize", "uint64", 56, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc elf_sym32_fields[] = {
    ELF_FIELD("st_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("st_value", "Elf32_Addr", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("st_size", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("st_info", "uint8", 12, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("st_bind", "uint8", 12, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_type", "uint8", 12, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_other", "uint8", 13, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("st_visibility", "uint8", 13, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_shndx", "uint16", 14, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc elf_sym64_fields[] = {
    ELF_FIELD("st_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("st_info", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("st_bind", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_type", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_other", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("st_visibility", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_shndx", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("st_value", "Elf64_Addr", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("st_size", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};

static const xx_data_struct_field_desc elf_rel32_fields[] = {
    ELF_FIELD("r_offset", "Elf32_Addr", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("r_info", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("r_sym", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_type", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc elf_rel64_fields[] = {
    ELF_FIELD("r_offset", "Elf64_Addr", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("r_info", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("r_sym", "uint32", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_type", "uint32", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};

static const xx_data_struct_field_desc elf_rela32_fields[] = {
    ELF_FIELD("r_offset", "Elf32_Addr", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("r_info", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("r_sym", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_type", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_addend", "int32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc elf_rela64_fields[] = {
    ELF_FIELD("r_offset", "Elf64_Addr", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("r_info", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("r_sym", "uint32", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_type", "uint32", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("r_addend", "int64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};

static const xx_data_struct_field_desc elf_relr32_fields[] = {
    ELF_FIELD("r_entry", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_relr64_fields[] = {
    ELF_FIELD("r_entry", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_dyn32_fields[] = {
    ELF_FIELD("d_tag", "int32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("d_val_or_ptr", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_dyn64_fields[] = {
    ELF_FIELD("d_tag", "int64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("d_val_or_ptr", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_chdr32_fields[] = {
    ELF_FIELD("ch_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ch_size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("ch_addralign", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_chdr64_fields[] = {
    ELF_FIELD("ch_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ch_reserved", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    ELF_FIELD("ch_size", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("ch_addralign", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_nhdr_fields[] = {
    ELF_FIELD("n_namesz", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("n_descsz", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("n_type", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_sysv_hash_fields[] = {
    ELF_FIELD("nbucket", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("nchain", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};
static const xx_data_struct_field_desc elf_gnu_hash_fields[] = {
    ELF_FIELD("nbuckets", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("symoffset", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("bloom_size", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("bloom_shift", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};
static const xx_data_struct_field_desc elf_word_fields[] = {
    ELF_FIELD("value", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};
static const xx_data_struct_field_desc elf_xword_fields[] = {
    ELF_FIELD("value", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE)
};
static const xx_data_struct_field_desc elf_address32_fields[] = {
    ELF_FIELD("address", "Elf32_Addr", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};
static const xx_data_struct_field_desc elf_address64_fields[] = {
    ELF_FIELD("address", "Elf64_Addr", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};
static const xx_data_struct_field_desc elf_verdef_fields[] = {
    ELF_FIELD("vd_version", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vd_flags", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("vd_ndx", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vd_cnt", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("vd_hash", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vd_aux", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("vd_next", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_verdaux_fields[] = {
    ELF_FIELD("vda_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("vda_next", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_verneed_fields[] = {
    ELF_FIELD("vn_version", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vn_cnt", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("vn_file", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("vn_aux", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("vn_next", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_vernaux_fields[] = {
    ELF_FIELD("vna_hash", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vna_flags", "uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("vna_other", "uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vna_name", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("vna_next", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_versym_fields[] = {
    ELF_FIELD("vs_raw", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("vs_index", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("vs_hidden", "bool", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_syminfo_fields[] = {
    ELF_FIELD("si_boundto", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("si_flags", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_auxv32_fields[] = {
    ELF_FIELD("a_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("a_val", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_auxv64_fields[] = {
    ELF_FIELD("a_type", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("a_val", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_nt_file_head32_fields[] = {
    ELF_FIELD("count", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("page_size", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_nt_file_head64_fields[] = {
    ELF_FIELD("count", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("page_size", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_nt_file_entry32_fields[] = {
    ELF_FIELD("start", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("end", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("file_ofs", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_nt_file_entry64_fields[] = {
    ELF_FIELD("start", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("end", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS),
    ELF_FIELD("file_ofs", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET)
};
static const xx_data_struct_field_desc elf_gnu_abi_fields[] = {
    ELF_FIELD("os", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("major", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("minor", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("subminor", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_property_fields[] = {
    ELF_FIELD("pr_type", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("pr_datasz", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_hwcap_fields[] = {
    ELF_FIELD("num_entries", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("enabled_mask", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_siginfo_fields[] = {
    ELF_FIELD("si_signo", "int32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("si_errno", "int32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("si_code", "int32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_siginfo_mips_fields[] = {
    ELF_FIELD("si_signo", "int32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("si_code", "int32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("si_errno", "int32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_android_fields[] = {
    ELF_FIELD("api_level", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_move32_fields[] = {
    ELF_FIELD("m_value", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    ELF_FIELD("m_info", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("m_sym", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("m_size", "uint8", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("m_poffset", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("m_repeat", "uint16", 16, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("m_stride", "uint16", 18, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_move64_fields[] = {
    ELF_FIELD("m_value", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    ELF_FIELD("m_info", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("m_sym", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("m_size", "uint8", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("m_poffset", "uint64", 16, 8, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("m_repeat", "uint16", 24, 2, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT),
    ELF_FIELD("m_stride", "uint16", 26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_lib_fields[] = {
    ELF_FIELD("l_name", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET),
    ELF_FIELD("l_time_stamp", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP),
    ELF_FIELD("l_checksum", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("l_version", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("l_flags", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_cap32_fields[] = {
    ELF_FIELD("c_tag", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("c_val_or_ptr", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_cap64_fields[] = {
    ELF_FIELD("c_tag", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("c_val_or_ptr", "uint64", 8, 8, XX_DATA_STRUCT_RECORD_PROPERTY_POINTER)
};
static const xx_data_struct_field_desc elf_capinfo32_fields[] = {
    ELF_FIELD("ci_raw", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ci_sym", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ci_group", "uint8", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_capinfo64_fields[] = {
    ELF_FIELD("ci_raw", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ci_sym", "uint32", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("ci_group", "uint32", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_reginfo32_fields[] = {
    ELF_FIELD("ri_gprmask", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask0", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask1", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask2", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask3", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_gp_value", "int32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};
static const xx_data_struct_field_desc elf_reginfo64_fields[] = {
    ELF_FIELD("ri_gprmask", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_pad", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED),
    ELF_FIELD("ri_cprmask0", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask1", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask2", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_cprmask3", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ri_gp_value", "Elf64_Addr", 24, 8, XX_DATA_STRUCT_RECORD_PROPERTY_VIRTUAL_ADDRESS)
};
static const xx_data_struct_field_desc elf_options_fields[] = {
    ELF_FIELD("kind", "uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("size", "uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("section", "uint16", 2, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("info", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_options_hw_fields[] = {
    ELF_FIELD("hwp_flags1", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("hwp_flags2", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_gptab_fields[] = {
    ELF_FIELD("gt_value", "uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE),
    ELF_FIELD("gt_bytes_or_unused", "uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE)
};
static const xx_data_struct_field_desc elf_conflict32_fields[] = {
    ELF_FIELD("c_index", "Elf32_Addr", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_conflict64_fields[] = {
    ELF_FIELD("c_index", "Elf64_Addr", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_ID)
};
static const xx_data_struct_field_desc elf_abiflags_fields[] = {
    ELF_FIELD("version", "uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("isa_level", "uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("isa_rev", "uint8", 3, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("gpr_size", "uint8", 4, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("cpr1_size", "uint8", 5, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("cpr2_size", "uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE),
    ELF_FIELD("fp_abi", "uint8", 7, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID),
    ELF_FIELD("isa_ext", "uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("ases", "uint32", 12, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("flags1", "uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS),
    ELF_FIELD("flags2", "uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)
};
static const xx_data_struct_field_desc elf_cgprofile_fields[] = {
    ELF_FIELD("cgp_weight", "uint64", 0, 8, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT)
};

static bool elf_u64_product(uint64_t left, uint64_t right,
                            uint64_t *result) {
    if (!result || (right != 0U && left > UINT64_MAX / right)) return false;
    *result = left * right;
    return true;
}

static bool elf_u64_add(uint64_t left, uint64_t right, uint64_t *result) {
    if (!result || left > UINT64_MAX - right) return false;
    *result = left + right;
    return true;
}

static bool elf_machine_is_mips(uint16_t machine) {
    return machine == ELF_MACHINE_MIPS ||
           machine == ELF_MACHINE_MIPS_RS3_LE;
}

static bool elf_align_up(uint64_t value, uint64_t alignment,
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

static bool elf_range(const xx_elf *elf, uint64_t relative, uint64_t size) {
    int64_t total;
    uint64_t available;
    if (!elf || !elf->format.device || elf->format.base_address < 0)
        return false;
    total = xx_io_total_size(elf->format.device);
    if (total < elf->format.base_address) return false;
    available = (uint64_t)(total - elf->format.base_address);
    return relative <= available && size <= available - relative;
}

static bool elf_absolute(const xx_elf *elf, uint64_t relative,
                         int64_t *absolute) {
    if (!elf || !absolute || elf->format.base_address < 0 ||
        relative > (uint64_t)(INT64_MAX - elf->format.base_address))
        return false;
    *absolute = elf->format.base_address + (int64_t)relative;
    return true;
}

static uint16_t elf_get_u16(const xx_elf *elf, uint64_t relative) {
    int64_t absolute;
    if (!elf_absolute(elf, relative, &absolute)) return 0U;
    return xx_io_get_u16(elf->format.device, absolute,
                         elf->format.endian == XX_ENDIAN_BIG);
}

static uint32_t elf_get_u32(const xx_elf *elf, uint64_t relative) {
    int64_t absolute;
    if (!elf_absolute(elf, relative, &absolute)) return 0U;
    return xx_io_get_u32(elf->format.device, absolute,
                         elf->format.endian == XX_ENDIAN_BIG);
}

static uint64_t elf_get_u64(const xx_elf *elf, uint64_t relative) {
    int64_t absolute;
    if (!elf_absolute(elf, relative, &absolute)) return 0U;
    return xx_io_get_u64(elf->format.device, absolute,
                         elf->format.endian == XX_ENDIAN_BIG);
}

static uint64_t elf_get_word(const xx_elf *elf, uint64_t relative) {
    return elf && elf->elf_class == XX_ELF_CLASS_64
               ? elf_get_u64(elf, relative)
               : elf_get_u32(elf, relative);
}

static void elf_data_stream_free(void *pointer) {
    elf_data_stream *stream = (elf_data_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool elf_has_exact(const elf_data_stream *stream, uint32_t id,
                          int64_t absolute, uint64_t total_size) {
    size_t index;
    if (!stream || total_size > INT64_MAX) return false;
    for (index = 0U; index < stream->count; ++index) {
        const xx_data_struct *item = &stream->items[index];
        if (item->id == id && item->offset == absolute &&
            item->total_size == (int64_t)total_size)
            return true;
    }
    return false;
}

static bool elf_append(xx_elf *elf, elf_data_stream *stream, uint32_t id,
                       uint64_t relative, uint64_t entry_size,
                       uint64_t total_size, uint64_t count,
                       xx_data_struct_type_t type) {
    xx_data_struct *grown;
    xx_data_struct *item;
    int64_t absolute;
    uint64_t mapped;
    size_t capacity;
    if (!elf || !stream || stream->count >= ELF_DATA_MAX_ITEMS ||
        entry_size > INT64_MAX || total_size > INT64_MAX ||
        !elf_range(elf, relative, total_size) ||
        !elf_absolute(elf, relative, &absolute))
        return false;
    if (elf_has_exact(stream, id, absolute, total_size)) return true;
    if (stream->count == stream->capacity) {
        capacity = stream->capacity ? stream->capacity * 2U : 64U;
        if (capacity < stream->capacity || capacity > ELF_DATA_MAX_ITEMS)
            capacity = (size_t)ELF_DATA_MAX_ITEMS;
        grown = (xx_data_struct *)xx_mem_realloc(
            stream->items, capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    item = &stream->items[stream->count++];
    xx_mem_zero(item, sizeof(*item));
    item->id = id;
    item->offset = absolute;
    mapped = xx_format_offset_to_address(&elf->format, absolute, NULL);
    item->address = mapped != XX_INVALID_ADDRESS && mapped <= INT64_MAX
                        ? (int64_t)mapped : -1;
    item->entry_size = (int64_t)entry_size;
    item->total_size = (int64_t)total_size;
    item->count = count;
    item->type = type;
    return true;
}

static bool elf_append_raw(xx_elf *elf, elf_data_stream *stream,
                           uint64_t relative, uint64_t size) {
    if (size == 0U) return true;
    return elf_append(elf, stream, XX_ELF_DATA_STRUCT_RAW_DATA, relative,
                      1U, size, size, XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static bool elf_append_variable(xx_elf *elf, elf_data_stream *stream,
                                uint32_t id, uint64_t relative,
                                uint64_t size) {
    if (size == 0U) return true;
    return elf_append(elf, stream, id, relative, 1U, size, size,
                      XX_DATA_STRUCT_TYPE_RAW_DATA);
}

static bool elf_append_table(xx_elf *elf, elf_data_stream *stream,
                             uint32_t id, uint64_t relative,
                             uint64_t size, uint64_t stride,
                             uint64_t canonical) {
    uint64_t count;
    uint64_t covered;
    if (size == 0U) return true;
    if (stride < canonical || stride == 0U) return elf_append_raw(elf, stream,
                                                                  relative,
                                                                  size);
    count = size / stride;
    if (!elf_u64_product(count, stride, &covered)) return false;
    if (count != 0U &&
        !elf_append(elf, stream, id, relative, stride, covered, count,
                    XX_DATA_STRUCT_TYPE_ENTRY))
        return false;
    return elf_append_raw(elf, stream, relative + covered, size - covered);
}

static bool elf_name_equals(const char *name, const char *expected) {
    return name && expected && xx_rt_strcmp(name, expected) == 0;
}

static bool elf_section_name(const xx_elf *elf,
                             const xx_elf_section_header *section,
                             char *buffer, size_t buffer_size) {
    const xx_elf_section_header *strings;
    uint64_t cursor;
    size_t length = 0U;
    if (!elf || !section || !buffer || buffer_size == 0U ||
        !elf->section_headers ||
        elf->section_name_index >= elf->section_header_count)
        return false;
    strings = &elf->section_headers[elf->section_name_index];
    if (strings->type != XX_ELF_SECTION_STRTAB ||
        section->name_offset >= strings->size ||
        !elf_range(elf, strings->offset, strings->size))
        return false;
    cursor = strings->offset + section->name_offset;
    while (length + 1U < buffer_size &&
           (uint64_t)section->name_offset + length < strings->size) {
        int64_t absolute;
        uint8_t byte;
        if (!elf_absolute(elf, cursor + length, &absolute)) return false;
        byte = xx_io_get_u8(elf->format.device, absolute);
        if (byte == 0U) {
            buffer[length] = '\0';
            return true;
        }
        buffer[length++] = (char)byte;
    }
    buffer[length] = '\0';
    return length != 0U;
}

static uint64_t elf_section_stride(const xx_elf_section_header *section,
                                   uint64_t canonical) {
    if (!section) return canonical;
    return section->entry_size != 0U ? section->entry_size : canonical;
}

static bool elf_append_sysv_hash(xx_elf *elf, elf_data_stream *stream,
                                 uint64_t offset, uint64_t size) {
    uint64_t buckets_size;
    uint64_t chains_size;
    uint64_t end;
    uint32_t buckets;
    uint32_t chains;
    if (size < 8U || !elf_range(elf, offset, size))
        return elf_append_raw(elf, stream, offset, size);
    buckets = elf_get_u32(elf, offset);
    chains = elf_get_u32(elf, offset + 4U);
    if (!elf_u64_product(buckets, 4U, &buckets_size) ||
        !elf_u64_product(chains, 4U, &chains_size) ||
        !elf_u64_add(8U, buckets_size, &end) ||
        !elf_u64_add(end, chains_size, &end) || end > size)
        return elf_append_raw(elf, stream, offset, size);
    if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_SYSV_HASH_HEADER,
                    offset, 8U, 8U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        (buckets != 0U &&
         !elf_append(elf, stream, XX_ELF_DATA_STRUCT_SYSV_HASH_BUCKET,
                     offset + 8U, 4U, buckets_size, buckets,
                     XX_DATA_STRUCT_TYPE_ENTRY)) ||
        (chains != 0U &&
         !elf_append(elf, stream, XX_ELF_DATA_STRUCT_SYSV_HASH_CHAIN,
                     offset + 8U + buckets_size, 4U, chains_size, chains,
                     XX_DATA_STRUCT_TYPE_ENTRY)))
        return false;
    return elf_append_raw(elf, stream, offset + end, size - end);
}

static bool elf_append_gnu_hash(xx_elf *elf, elf_data_stream *stream,
                                uint64_t offset, uint64_t size) {
    uint64_t word_size = elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U;
    uint64_t bloom_bytes;
    uint64_t bucket_bytes;
    uint64_t cursor;
    uint64_t chain_count;
    uint64_t chain_bytes;
    uint32_t buckets;
    uint32_t bloom_count;
    if (size < 16U || !elf_range(elf, offset, size))
        return elf_append_raw(elf, stream, offset, size);
    buckets = elf_get_u32(elf, offset);
    bloom_count = elf_get_u32(elf, offset + 8U);
    if (!elf_u64_product(bloom_count, word_size, &bloom_bytes) ||
        !elf_u64_product(buckets, 4U, &bucket_bytes) ||
        !elf_u64_add(16U, bloom_bytes, &cursor) ||
        !elf_u64_add(cursor, bucket_bytes, &cursor) || cursor > size)
        return elf_append_raw(elf, stream, offset, size);
    chain_count = (size - cursor) / 4U;
    chain_bytes = chain_count * 4U;
    if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_GNU_HASH_HEADER,
                    offset, 16U, 16U, 1U, XX_DATA_STRUCT_TYPE_STRUCT) ||
        (bloom_count != 0U &&
         !elf_append(elf, stream,
                     elf->elf_class == XX_ELF_CLASS_64
                         ? XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM64
                         : XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM32,
                     offset + 16U, word_size, bloom_bytes, bloom_count,
                     XX_DATA_STRUCT_TYPE_ENTRY)) ||
        (buckets != 0U &&
         !elf_append(elf, stream, XX_ELF_DATA_STRUCT_GNU_HASH_BUCKET,
                     offset + 16U + bloom_bytes, 4U, bucket_bytes, buckets,
                     XX_DATA_STRUCT_TYPE_ENTRY)) ||
        (chain_count != 0U &&
         !elf_append(elf, stream, XX_ELF_DATA_STRUCT_GNU_HASH_CHAIN,
                     offset + cursor, 4U, chain_bytes, chain_count,
                     XX_DATA_STRUCT_TYPE_ENTRY)))
        return false;
    return elf_append_raw(elf, stream, offset + cursor + chain_bytes,
                          size - cursor - chain_bytes);
}

static bool elf_append_verdef(xx_elf *elf, elf_data_stream *stream,
                              uint64_t offset, uint64_t size,
                              xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    uint64_t roots = 0U;
    uint64_t extent = 0U;
    while (cursor < size && roots < ELF_DATA_MAX_LINKED_RECORDS) {
        uint64_t root = cursor;
        uint16_t count;
        uint32_t aux_delta;
        uint32_t next_delta;
        uint64_t aux;
        uint16_t index;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - root < ELF_VERDEF_SIZE) {
            return elf_append_raw(elf, stream, offset + root, size - root);
        }
        if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_VERDEF,
                        offset + root, ELF_VERDEF_SIZE, ELF_VERDEF_SIZE, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        if (root + ELF_VERDEF_SIZE > extent)
            extent = root + ELF_VERDEF_SIZE;
        count = elf_get_u16(elf, offset + root + 6U);
        aux_delta = elf_get_u32(elf, offset + root + 12U);
        next_delta = elf_get_u32(elf, offset + root + 16U);
        if (count != 0U) {
            if (aux_delta < ELF_VERDEF_SIZE || aux_delta > size - root)
                return elf_append_raw(elf, stream, offset + root,
                                      size - root);
            aux = root + aux_delta;
            for (index = 0U; index < count; ++index) {
                uint32_t aux_next;
                if (size - aux < ELF_VERDAUX_SIZE ||
                    !elf_append(elf, stream, XX_ELF_DATA_STRUCT_VERDAUX,
                                offset + aux, ELF_VERDAUX_SIZE,
                                ELF_VERDAUX_SIZE, 1U,
                                XX_DATA_STRUCT_TYPE_ENTRY))
                    return elf_append_raw(elf, stream, offset + root,
                                          size - root);
                if (aux + ELF_VERDAUX_SIZE > extent)
                    extent = aux + ELF_VERDAUX_SIZE;
                aux_next = elf_get_u32(elf, offset + aux + 4U);
                if (index + 1U == count) break;
                if (aux_next < ELF_VERDAUX_SIZE || aux_next > size - aux)
                    return elf_append_raw(elf, stream, offset + root,
                                          size - root);
                aux += aux_next;
            }
        }
        ++roots;
        if (next_delta == 0U)
            return extent <= size
                       ? elf_append_raw(elf, stream, offset + extent,
                                        size - extent)
                       : false;
        if (next_delta < ELF_VERDEF_SIZE || next_delta > size - root)
            return elf_append_raw(elf, stream, offset + root, size - root);
        cursor = root + next_delta;
    }
    return cursor == size || roots < ELF_DATA_MAX_LINKED_RECORDS
               ? true
               : elf_append_raw(elf, stream, offset + cursor, size - cursor);
}

static bool elf_append_verneed(xx_elf *elf, elf_data_stream *stream,
                               uint64_t offset, uint64_t size,
                               xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    uint64_t roots = 0U;
    uint64_t extent = 0U;
    while (cursor < size && roots < ELF_DATA_MAX_LINKED_RECORDS) {
        uint64_t root = cursor;
        uint16_t count;
        uint32_t aux_delta;
        uint32_t next_delta;
        uint64_t aux;
        uint16_t index;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - root < ELF_VERNEED_SIZE)
            return elf_append_raw(elf, stream, offset + root, size - root);
        if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_VERNEED,
                        offset + root, ELF_VERNEED_SIZE, ELF_VERNEED_SIZE, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        if (root + ELF_VERNEED_SIZE > extent)
            extent = root + ELF_VERNEED_SIZE;
        count = elf_get_u16(elf, offset + root + 2U);
        aux_delta = elf_get_u32(elf, offset + root + 8U);
        next_delta = elf_get_u32(elf, offset + root + 12U);
        if (count != 0U) {
            if (aux_delta < ELF_VERNEED_SIZE || aux_delta > size - root)
                return elf_append_raw(elf, stream, offset + root,
                                      size - root);
            aux = root + aux_delta;
            for (index = 0U; index < count; ++index) {
                uint32_t aux_next;
                if (size - aux < ELF_VERNAUX_SIZE ||
                    !elf_append(elf, stream, XX_ELF_DATA_STRUCT_VERNAUX,
                                offset + aux, ELF_VERNAUX_SIZE,
                                ELF_VERNAUX_SIZE, 1U,
                                XX_DATA_STRUCT_TYPE_ENTRY))
                    return elf_append_raw(elf, stream, offset + root,
                                          size - root);
                if (aux + ELF_VERNAUX_SIZE > extent)
                    extent = aux + ELF_VERNAUX_SIZE;
                aux_next = elf_get_u32(elf, offset + aux + 12U);
                if (index + 1U == count) break;
                if (aux_next < ELF_VERNAUX_SIZE || aux_next > size - aux)
                    return elf_append_raw(elf, stream, offset + root,
                                          size - root);
                aux += aux_next;
            }
        }
        ++roots;
        if (next_delta == 0U)
            return extent <= size
                       ? elf_append_raw(elf, stream, offset + extent,
                                        size - extent)
                       : false;
        if (next_delta < ELF_VERNEED_SIZE || next_delta > size - root)
            return elf_append_raw(elf, stream, offset + root, size - root);
        cursor = root + next_delta;
    }
    return cursor == size || roots < ELF_DATA_MAX_LINKED_RECORDS
               ? true
               : elf_append_raw(elf, stream, offset + cursor, size - cursor);
}

static bool elf_read_owner(const xx_elf *elf, uint64_t offset,
                           uint64_t size, char *owner, size_t owner_size) {
    uint64_t index;
    if (!elf || !owner || owner_size == 0U || !elf_range(elf, offset, size))
        return false;
    for (index = 0U; index < size && index + 1U < owner_size; ++index) {
        int64_t absolute;
        uint8_t value;
        if (!elf_absolute(elf, offset + index, &absolute)) return false;
        value = xx_io_get_u8(elf->format.device, absolute);
        owner[index] = (char)value;
        if (value == 0U) return true;
    }
    owner[index < owner_size ? index : owner_size - 1U] = '\0';
    return true;
}

static bool elf_append_gnu_properties(xx_elf *elf, elf_data_stream *stream,
                                      uint64_t offset, uint64_t size,
                                      xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    uint64_t alignment = elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U;
    while (cursor < size) {
        uint64_t data_end;
        uint64_t next;
        uint32_t data_size;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U)
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        data_size = elf_get_u32(elf, offset + cursor + 4U);
        if (!elf_u64_add(cursor + 8U, data_size, &data_end) ||
            data_end > size || !elf_align_up(data_end, alignment, &next) ||
            next > size)
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_GNU_PROPERTY_HEADER,
                        offset + cursor, 8U, 8U, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY) ||
            !elf_append_variable(elf, stream,
                                 XX_ELF_DATA_STRUCT_PROPERTY_DATA,
                                 offset + cursor + 8U, data_size) ||
            !elf_append_raw(elf, stream, offset + data_end, next - data_end))
            return false;
        cursor = next;
    }
    return true;
}

static bool elf_append_note_detail(xx_elf *elf, elf_data_stream *stream,
                                   const char *owner, uint32_t type,
                                   uint64_t offset, uint64_t size,
                                   xx_pd_struct *pd) {
    uint64_t word = elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U;
    if (!elf_append_variable(elf, stream, XX_ELF_DATA_STRUCT_NOTE_DESC,
                             offset, size))
        return false;
    if (xx_rt_strcmp(owner, "GNU") == 0 && type == ELF_NOTE_GNU_ABI_TAG &&
        size >= 16U) {
        return elf_append(elf, stream, XX_ELF_DATA_STRUCT_GNU_ABI_TAG,
                          offset, 16U, 16U, 1U,
                          XX_DATA_STRUCT_TYPE_STRUCT) &&
               elf_append_raw(elf, stream, offset + 16U, size - 16U);
    }
    if (xx_rt_strcmp(owner, "GNU") == 0 && type == ELF_NOTE_GNU_HWCAP &&
        size >= 8U) {
        return elf_append(elf, stream,
                          XX_ELF_DATA_STRUCT_GNU_HWCAP_HEADER,
                          offset, 8U, 8U, 1U,
                          XX_DATA_STRUCT_TYPE_STRUCT) &&
               elf_append_raw(elf, stream, offset + 8U, size - 8U);
    }
    if (xx_rt_strcmp(owner, "GNU") == 0 && type == ELF_NOTE_GNU_PROPERTY)
        return elf_append_gnu_properties(elf, stream, offset, size, pd);
    if ((xx_rt_strcmp(owner, "CORE") == 0 ||
         xx_rt_strcmp(owner, "LINUX") == 0) &&
        type == ELF_NOTE_NT_AUXV) {
        uint64_t entry = word * 2U;
        return elf_append_table(
            elf, stream,
            elf->elf_class == XX_ELF_CLASS_64
                ? XX_ELF_DATA_STRUCT_AUXV64
                : XX_ELF_DATA_STRUCT_AUXV32,
            offset, size, entry, entry);
    }
    if ((xx_rt_strcmp(owner, "CORE") == 0 ||
         xx_rt_strcmp(owner, "LINUX") == 0) &&
        type == ELF_NOTE_NT_FILE && size >= word * 2U) {
        uint64_t count = elf_get_word(elf, offset);
        uint64_t entries_size;
        uint64_t entries_offset = word * 2U;
        uint64_t entry_size = word * 3U;
        if (count > ELF_DATA_MAX_LINKED_RECORDS ||
            !elf_u64_product(count, entry_size, &entries_size) ||
            entries_size > size - entries_offset)
            return true;
        if (!elf_append(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_NT_FILE_HEADER64
                    : XX_ELF_DATA_STRUCT_NT_FILE_HEADER32,
                offset, entries_offset, entries_offset, 1U,
                XX_DATA_STRUCT_TYPE_STRUCT) ||
            (count != 0U &&
             !elf_append(
                 elf, stream,
                 elf->elf_class == XX_ELF_CLASS_64
                     ? XX_ELF_DATA_STRUCT_NT_FILE_ENTRY64
                     : XX_ELF_DATA_STRUCT_NT_FILE_ENTRY32,
                 offset + entries_offset, entry_size, entries_size, count,
                 XX_DATA_STRUCT_TYPE_ENTRY)))
            return false;
        return elf_append_variable(
            elf, stream, XX_ELF_DATA_STRUCT_STRING_DATA,
            offset + entries_offset + entries_size,
            size - entries_offset - entries_size);
    }
    if ((xx_rt_strcmp(owner, "CORE") == 0 ||
         xx_rt_strcmp(owner, "LINUX") == 0) &&
        type == ELF_NOTE_NT_SIGINFO && size >= 12U) {
        return elf_append(elf, stream, XX_ELF_DATA_STRUCT_LINUX_SIGINFO,
                          offset, 12U, 12U, 1U,
                          XX_DATA_STRUCT_TYPE_STRUCT);
    }
    if (xx_rt_strcmp(owner, "Android") == 0 &&
        type == ELF_NOTE_ANDROID_IDENT &&
        size >= 4U) {
        return elf_append(elf, stream,
                          XX_ELF_DATA_STRUCT_ANDROID_IDENT_PREFIX,
                          offset, 4U, 4U, 1U,
                          XX_DATA_STRUCT_TYPE_STRUCT);
    }
    return true;
}

static bool elf_append_notes(xx_elf *elf, elf_data_stream *stream,
                             uint64_t offset, uint64_t size,
                             uint64_t alignment, xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    if (alignment != 4U && alignment != 8U) alignment = 4U;
    while (cursor < size) {
        uint64_t name_start;
        uint64_t name_end;
        uint64_t desc_start;
        uint64_t desc_end;
        uint64_t next;
        uint32_t name_size;
        uint32_t desc_size;
        uint32_t type;
        char owner[32];
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < ELF_NHDR_SIZE)
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        name_size = elf_get_u32(elf, offset + cursor);
        desc_size = elf_get_u32(elf, offset + cursor + 4U);
        type = elf_get_u32(elf, offset + cursor + 8U);
        name_start = cursor + ELF_NHDR_SIZE;
        if (!elf_u64_add(name_start, name_size, &name_end) ||
            name_end > size || !elf_align_up(name_end, alignment, &desc_start) ||
            desc_start > size ||
            !elf_u64_add(desc_start, desc_size, &desc_end) ||
            desc_end > size || !elf_align_up(desc_end, alignment, &next) ||
            next > size)
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_NOTE_HEADER,
                        offset + cursor, ELF_NHDR_SIZE, ELF_NHDR_SIZE, 1U,
                        XX_DATA_STRUCT_TYPE_STRUCT) ||
            !elf_append_variable(elf, stream, XX_ELF_DATA_STRUCT_NOTE_NAME,
                                 offset + name_start, name_size) ||
            !elf_append_raw(elf, stream, offset + name_end,
                            desc_start - name_end))
            return false;
        owner[0] = '\0';
        if (name_size <= ELF_DATA_MAX_NAME)
            (void)elf_read_owner(elf, offset + name_start, name_size,
                                 owner, ELF_COUNT(owner));
        if (!elf_append_note_detail(elf, stream, owner, type,
                                    offset + desc_start, desc_size, pd) ||
            !elf_append_raw(elf, stream, offset + desc_end,
                            next - desc_end))
            return false;
        cursor = next;
    }
    return true;
}

static bool elf_append_mips_options(xx_elf *elf, elf_data_stream *stream,
                                    uint64_t offset, uint64_t size,
                                    xx_pd_struct *pd) {
    uint64_t cursor = 0U;
    while (cursor < size) {
        uint8_t option_size;
        uint8_t kind;
        uint64_t payload_size;
        uint64_t reginfo_size;
        int64_t absolute;
        if (xx_pd_is_stopped(pd)) return false;
        if (size - cursor < 8U ||
            !elf_absolute(elf, offset + cursor + 1U, &absolute))
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        option_size = xx_io_get_u8(elf->format.device, absolute);
        kind = xx_io_get_u8(elf->format.device, absolute - 1);
        if (option_size < 8U || option_size > size - cursor)
            return elf_append_raw(elf, stream, offset + cursor, size - cursor);
        if (!elf_append(elf, stream, XX_ELF_DATA_STRUCT_MIPS_OPTIONS,
                        offset + cursor, 8U, 8U, 1U,
                        XX_DATA_STRUCT_TYPE_ENTRY))
            return false;
        payload_size = option_size - 8U;
        reginfo_size = elf->elf_class == XX_ELF_CLASS_64 ? 32U : 24U;
        if (kind == 1U && payload_size >= reginfo_size) {
            if (!elf_append(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_MIPS_REGINFO64
                        : XX_ELF_DATA_STRUCT_MIPS_REGINFO32,
                    offset + cursor + 8U, reginfo_size, reginfo_size, 1U,
                    XX_DATA_STRUCT_TYPE_STRUCT) ||
                !elf_append_raw(elf, stream,
                                offset + cursor + 8U + reginfo_size,
                                payload_size - reginfo_size))
                return false;
        } else if ((kind == 4U || kind == 7U || kind == 8U) &&
                   payload_size >= 8U) {
            if (!elf_append(elf, stream,
                            XX_ELF_DATA_STRUCT_MIPS_OPTIONS_HW,
                            offset + cursor + 8U, 8U, 8U, 1U,
                            XX_DATA_STRUCT_TYPE_STRUCT) ||
                !elf_append_raw(elf, stream, offset + cursor + 16U,
                                payload_size - 8U))
                return false;
        } else if (!elf_append_raw(elf, stream, offset + cursor + 8U,
                                   payload_size)) {
            return false;
        }
        cursor += option_size;
    }
    return true;
}

static bool elf_append_section(xx_elf *elf, elf_data_stream *stream,
                               const xx_elf_section_header *section,
                               xx_pd_struct *pd) {
    uint64_t word = elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U;
    uint64_t canonical;
    char name[128];
    bool solaris;
    bool mips;
    if (!section || section->size == 0U ||
        section->type == XX_ELF_SECTION_NOBITS ||
        section->type == XX_ELF_SECTION_NULL)
        return true;
    if (!elf_range(elf, section->offset, section->size)) return false;
    name[0] = '\0';
    (void)elf_section_name(elf, section, name, ELF_COUNT(name));
    solaris = elf->os_abi == ELF_OSABI_SOLARIS ||
              xx_rt_strncmp(name, ".SUNW_", 6U) == 0;
    mips = elf_machine_is_mips(elf->machine);
    if ((section->flags & XX_ELF_SECTION_FLAG_COMPRESSED) != 0U) {
        canonical = elf->elf_class == XX_ELF_CLASS_64
                        ? ELF_CHDR64_SIZE : ELF_CHDR32_SIZE;
        if (section->size < canonical)
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        return elf_append(
                   elf, stream,
                   elf->elf_class == XX_ELF_CLASS_64
                       ? XX_ELF_DATA_STRUCT_COMPRESSION_HEADER64
                       : XX_ELF_DATA_STRUCT_COMPRESSION_HEADER32,
                   section->offset, canonical, canonical, 1U,
                   XX_DATA_STRUCT_TYPE_STRUCT) &&
               elf_append_raw(elf, stream, section->offset + canonical,
                              section->size - canonical);
    }
    switch (section->type) {
        case XX_ELF_SECTION_SYMTAB:
        case XX_ELF_SECTION_DYNSYM:
            canonical = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_SYM64_SIZE : ELF_SYM32_SIZE;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_SYMBOL64
                    : XX_ELF_DATA_STRUCT_SYMBOL32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_REL:
            canonical = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_REL64_SIZE : ELF_REL32_SIZE;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_REL64 : XX_ELF_DATA_STRUCT_REL32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_RELA:
            canonical = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_RELA64_SIZE : ELF_RELA32_SIZE;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_RELA64 : XX_ELF_DATA_STRUCT_RELA32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_RELR:
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_RELR64 : XX_ELF_DATA_STRUCT_RELR32,
                section->offset, section->size,
                elf_section_stride(section, word), word);
        case XX_ELF_SECTION_DYNAMIC:
            canonical = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_DYN64_SIZE : ELF_DYN32_SIZE;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_DYNAMIC64
                    : XX_ELF_DATA_STRUCT_DYNAMIC32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_NOTE:
            return elf_append_notes(elf, stream, section->offset,
                                    section->size,
                                    section->address_alignment, pd);
        case XX_ELF_SECTION_HASH:
            return elf_append_sysv_hash(elf, stream, section->offset,
                                        section->size);
        case XX_ELF_SECTION_GNU_HASH:
            if (!solaris || elf_name_equals(name, ".gnu.hash"))
                return elf_append_gnu_hash(elf, stream, section->offset,
                                           section->size);
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        case XX_ELF_SECTION_GROUP:
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_GROUP_WORD,
                                    section->offset, section->size, 4U, 4U);
        case XX_ELF_SECTION_SYMTAB_SHNDX:
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_SYMTAB_SHNDX,
                                    section->offset, section->size, 4U, 4U);
        case XX_ELF_SECTION_INIT_ARRAY:
        case XX_ELF_SECTION_FINI_ARRAY:
        case XX_ELF_SECTION_PREINIT_ARRAY:
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_ADDRESS64
                    : XX_ELF_DATA_STRUCT_ADDRESS32,
                section->offset, section->size,
                elf_section_stride(section, word), word);
        case XX_ELF_SECTION_GNU_VERDEF:
            return elf_append_verdef(elf, stream, section->offset,
                                     section->size, pd);
        case XX_ELF_SECTION_GNU_VERNEED:
            return elf_append_verneed(elf, stream, section->offset,
                                      section->size, pd);
        case XX_ELF_SECTION_GNU_VERSYM:
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_VERSYM,
                                    section->offset, section->size,
                                    elf_section_stride(section, 2U), 2U);
        case XX_ELF_SECTION_SUNW_CAPINFO:
            if (solaris || elf_name_equals(name, ".SUNW_capinfo"))
                return elf_append_table(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO64
                        : XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO32,
                    section->offset, section->size,
                    elf_section_stride(section, word), word);
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        case XX_ELF_SECTION_SUNW_CAP:
            if (elf_name_equals(name, ".SUNW_cap") ||
                (elf->os_abi == ELF_OSABI_SOLARIS &&
                 !elf_name_equals(name, ".gnu.attributes"))) {
                canonical = word * 2U;
                return elf_append_table(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_SOLARIS_CAP64
                        : XX_ELF_DATA_STRUCT_SOLARIS_CAP32,
                    section->offset, section->size,
                    elf_section_stride(section, canonical), canonical);
            }
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        case XX_ELF_SECTION_SUNW_MOVE:
            canonical = elf->elf_class == XX_ELF_CLASS_64 ? 28U : 20U;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_MOVE64
                    : XX_ELF_DATA_STRUCT_MOVE32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_SUNW_SYMINFO:
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_SYMINFO,
                                    section->offset, section->size,
                                    elf_section_stride(section, 4U), 4U);
        case XX_ELF_SECTION_GNU_LIBLIST:
            if (!solaris || elf_name_equals(name, ".gnu.liblist"))
                return elf_append_table(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_LIB64
                        : XX_ELF_DATA_STRUCT_LIB32,
                    section->offset, section->size,
                    elf_section_stride(section, 20U), 20U);
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        case XX_ELF_SECTION_MIPS_LIBLIST:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_LIB64 : XX_ELF_DATA_STRUCT_LIB32,
                section->offset, section->size,
                elf_section_stride(section, 20U), 20U);
        case XX_ELF_SECTION_MIPS_CONFLICT:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            canonical = elf->elf_class == XX_ELF_CLASS_64 ? 8U : 4U;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_MIPS_CONFLICT64
                    : XX_ELF_DATA_STRUCT_MIPS_CONFLICT32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_MIPS_GPTAB:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_MIPS_GPTAB,
                                    section->offset, section->size,
                                    elf_section_stride(section, 8U), 8U);
        case XX_ELF_SECTION_MIPS_REGINFO:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            canonical = elf->elf_class == XX_ELF_CLASS_64 ? 32U : 24U;
            return elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_MIPS_REGINFO64
                    : XX_ELF_DATA_STRUCT_MIPS_REGINFO32,
                section->offset, section->size,
                elf_section_stride(section, canonical), canonical);
        case XX_ELF_SECTION_MIPS_OPTIONS:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            return elf_append_mips_options(elf, stream, section->offset,
                                           section->size, pd);
        case XX_ELF_SECTION_MIPS_ABIFLAGS:
            if (!mips)
                return elf_append_raw(elf, stream, section->offset,
                                      section->size);
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_MIPS_ABI_FLAGS,
                                    section->offset, section->size,
                                    elf_section_stride(section, 24U), 24U);
        case XX_ELF_SECTION_LLVM_CALL_GRAPH_PROFILE:
            return elf_append_table(elf, stream,
                                    XX_ELF_DATA_STRUCT_LLVM_CGPROFILE,
                                    section->offset, section->size,
                                    elf_section_stride(section, 8U), 8U);
        case XX_ELF_SECTION_STRTAB:
            return elf_append_variable(elf, stream,
                                       XX_ELF_DATA_STRUCT_STRING_DATA,
                                       section->offset, section->size);
        case XX_ELF_SECTION_PROGBITS:
            if (elf_name_equals(name, ".interp"))
                return elf_append_variable(
                    elf, stream, XX_ELF_DATA_STRUCT_STRING_DATA,
                    section->offset, section->size);
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
        default:
            return elf_append_raw(elf, stream, section->offset,
                                  section->size);
    }
}

static bool elf_section_source_exists(const xx_elf *elf, uint32_t type,
                                      uint64_t offset, uint64_t size) {
    uint64_t index;
    if (!elf || !elf->section_headers) return false;
    for (index = 0U; index < elf->section_header_count; ++index) {
        const xx_elf_section_header *section = &elf->section_headers[index];
        if (section->type == type && section->offset == offset &&
            section->size == size)
            return true;
    }
    return false;
}

static bool elf_data_build(xx_elf *elf, elf_data_stream *stream,
                           xx_pd_struct *pd) {
    uint64_t canonical_header;
    uint64_t canonical_program;
    uint64_t canonical_section;
    uint64_t table_size;
    uint64_t index;
    if (!elf || !stream || !elf->format.base_info_handled ||
        !elf->format.is_valid || xx_pd_is_stopped(pd))
        return false;
    canonical_header = elf->elf_class == XX_ELF_CLASS_64
                           ? ELF_EHDR64_SIZE : ELF_EHDR32_SIZE;
    canonical_program = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_PHDR64_SIZE : ELF_PHDR32_SIZE;
    canonical_section = elf->elf_class == XX_ELF_CLASS_64
                            ? ELF_SHDR64_SIZE : ELF_SHDR32_SIZE;
    if (!elf_append(
            elf, stream,
            elf->elf_class == XX_ELF_CLASS_64
                ? XX_ELF_DATA_STRUCT_ELF_HEADER64
                : XX_ELF_DATA_STRUCT_ELF_HEADER32,
            0U, canonical_header, canonical_header, 1U,
            XX_DATA_STRUCT_TYPE_STRUCT))
        return false;
    if (elf->header_size > canonical_header &&
        !elf_append_raw(elf, stream, canonical_header,
                        elf->header_size - canonical_header))
        return false;
    if (elf->program_header_count != 0U) {
        if (!elf_u64_product(elf->program_header_count,
                             elf->program_header_entry_size, &table_size) ||
            !elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_PROGRAM_HEADER64
                    : XX_ELF_DATA_STRUCT_PROGRAM_HEADER32,
                elf->program_header_offset, table_size,
                elf->program_header_entry_size, canonical_program))
            return false;
    }
    if (elf->section_header_count != 0U) {
        if (!elf_u64_product(elf->section_header_count,
                             elf->section_header_entry_size, &table_size) ||
            !elf_append_table(
                elf, stream,
                elf->elf_class == XX_ELF_CLASS_64
                    ? XX_ELF_DATA_STRUCT_SECTION_HEADER64
                    : XX_ELF_DATA_STRUCT_SECTION_HEADER32,
                elf->section_header_offset, table_size,
                elf->section_header_entry_size, canonical_section))
            return false;
    }
    for (index = 0U; index < elf->section_header_count; ++index) {
        if (xx_pd_is_stopped(pd) ||
            !elf_append_section(elf, stream, &elf->section_headers[index], pd))
            return false;
    }
    for (index = 0U; index < elf->program_header_count; ++index) {
        const xx_elf_program_header *program = &elf->program_headers[index];
        if (xx_pd_is_stopped(pd)) return false;
        if (program->file_size == 0U ||
            !elf_range(elf, program->offset, program->file_size))
            continue;
        if (program->type == XX_ELF_PROGRAM_DYNAMIC &&
            !elf_section_source_exists(elf, XX_ELF_SECTION_DYNAMIC,
                                       program->offset,
                                       program->file_size)) {
            uint64_t canonical = elf->elf_class == XX_ELF_CLASS_64
                                     ? ELF_DYN64_SIZE : ELF_DYN32_SIZE;
            if (!elf_append_table(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_DYNAMIC64
                        : XX_ELF_DATA_STRUCT_DYNAMIC32,
                    program->offset, program->file_size, canonical,
                    canonical))
                return false;
        } else if ((program->type == XX_ELF_PROGRAM_NOTE ||
                    program->type == XX_ELF_PROGRAM_GNU_PROPERTY) &&
                   !elf_section_source_exists(elf, XX_ELF_SECTION_NOTE,
                                              program->offset,
                                              program->file_size)) {
            uint64_t alignment = program->alignment;
            if (alignment != 4U && alignment != 8U) alignment = 4U;
            if (!elf_append_notes(elf, stream, program->offset,
                                  program->file_size, alignment, pd))
                return false;
        } else if (program->type == XX_ELF_PROGRAM_INTERP) {
            bool found = false;
            uint64_t section_index;
            for (section_index = 0U;
                 section_index < elf->section_header_count; ++section_index) {
                const xx_elf_section_header *section =
                    &elf->section_headers[section_index];
                if (section->offset == program->offset &&
                    section->size == program->file_size) {
                    found = true;
                    break;
                }
            }
            if (!found &&
                !elf_append_variable(elf, stream,
                                     XX_ELF_DATA_STRUCT_STRING_DATA,
                                     program->offset, program->file_size))
                return false;
        } else if (elf_machine_is_mips(elf->machine) &&
                   program->type == XX_ELF_PROGRAM_MIPS_REGINFO &&
                   !elf_section_source_exists(
                       elf, XX_ELF_SECTION_MIPS_REGINFO,
                       program->offset, program->file_size)) {
            uint64_t canonical = elf->elf_class == XX_ELF_CLASS_64
                                     ? 32U : 24U;
            if (!elf_append_table(
                    elf, stream,
                    elf->elf_class == XX_ELF_CLASS_64
                        ? XX_ELF_DATA_STRUCT_MIPS_REGINFO64
                        : XX_ELF_DATA_STRUCT_MIPS_REGINFO32,
                    program->offset, program->file_size, canonical,
                    canonical))
                return false;
        } else if (elf_machine_is_mips(elf->machine) &&
                   program->type == XX_ELF_PROGRAM_MIPS_OPTIONS &&
                   !elf_section_source_exists(
                       elf, XX_ELF_SECTION_MIPS_OPTIONS,
                       program->offset, program->file_size)) {
            if (!elf_append_mips_options(elf, stream, program->offset,
                                         program->file_size, pd))
                return false;
        } else if (elf_machine_is_mips(elf->machine) &&
                   program->type == XX_ELF_PROGRAM_MIPS_ABIFLAGS &&
                   !elf_section_source_exists(
                       elf, XX_ELF_SECTION_MIPS_ABIFLAGS,
                       program->offset, program->file_size)) {
            if (!elf_append_table(elf, stream,
                                  XX_ELF_DATA_STRUCT_MIPS_ABI_FLAGS,
                                  program->offset, program->file_size,
                                  24U, 24U))
                return false;
        }
    }
    if (elf->format.overlay_size > 0 &&
        elf->format.overlay_offset >= elf->format.base_address &&
        !elf_append_raw(
            elf, stream,
            (uint64_t)(elf->format.overlay_offset - elf->format.base_address),
            (uint64_t)elf->format.overlay_size))
        return false;
    return !xx_pd_is_stopped(pd);
}

const char *xx_elf_data_struct_id_to_string(Abstractformat *format,
                                             uint32_t id) {
    (void)format;
    return id <= XX_ELF_DATA_STRUCT_LAST ? elf_data_names[id] : "UNKNOWN";
}

uint32_t xx_elf_data_struct_string_to_id(Abstractformat *format,
                                          const char *name) {
    uint32_t id;
    (void)format;
    if (!name) return XX_ELF_DATA_STRUCT_UNKNOWN;
    if (xx_rt_strcmp(name, "mips_conflict") == 0)
        return XX_ELF_DATA_STRUCT_MIPS_CONFLICT32;
    for (id = XX_ELF_DATA_STRUCT_UNKNOWN;
         id <= XX_ELF_DATA_STRUCT_LAST; ++id) {
        if (xx_rt_strcmp(name, elf_data_names[id]) == 0) return id;
    }
    return XX_ELF_DATA_STRUCT_UNKNOWN;
}

xx_data_struct_state *xx_elf_create_data_structs_reading(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_data_struct_state *state = NULL;
    elf_data_stream *stream = NULL;
    xx_elf *elf = (xx_elf *)format;
    if (!format || !format->device || xx_pd_is_stopped(pd) ||
        (!format->base_info_handled &&
         !xx_elf_handle_base_info(format, pd)))
        return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (elf_data_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream || !elf_data_build(elf, stream, pd) ||
        stream->count == 0U || stream->count > INT64_MAX)
        goto fail;
    xx_data_struct_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = elf_data_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_struct = stream->items[0];
    state->current_index = 0;
    state->has_struct = true;
    return state;
fail:
    if (stream) elf_data_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

const xx_data_struct *xx_elf_get_current_data_struct(
    Abstractformat *format, xx_data_struct_state *state) {
    return format && state && state->format == format && state->has_struct
               ? &state->current_struct : NULL;
}

bool xx_elf_data_struct_move_to_next(Abstractformat *format,
                                      xx_data_struct_state *state,
                                      xx_pd_struct *pd) {
    elf_data_stream *stream;
    int64_t next;
    if (!format || !state || state->format != format ||
        xx_pd_is_stopped(pd) ||
        !(stream = (elf_data_stream *)state->internal_state)) {
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

void xx_elf_free_data_structs_reading(Abstractformat *format,
                                       xx_data_struct_state *state) {
    (void)format;
    xx_data_struct_state_free(state);
}

static bool elf_static_fields(
    const xx_elf *elf, uint32_t id,
    const xx_data_struct_field_desc **fields, size_t *count) {
    if (!elf || !fields || !count) return false;
    *fields = NULL;
    *count = 0U;
#define ELF_SELECT(array_) do { \
        *fields = array_; *count = ELF_COUNT(array_); return true; \
    } while (0)
    switch (id) {
        case XX_ELF_DATA_STRUCT_ELF_HEADER32: ELF_SELECT(elf_ehdr32_fields);
        case XX_ELF_DATA_STRUCT_ELF_HEADER64: ELF_SELECT(elf_ehdr64_fields);
        case XX_ELF_DATA_STRUCT_PROGRAM_HEADER32: ELF_SELECT(elf_phdr32_fields);
        case XX_ELF_DATA_STRUCT_PROGRAM_HEADER64: ELF_SELECT(elf_phdr64_fields);
        case XX_ELF_DATA_STRUCT_SECTION_HEADER32: ELF_SELECT(elf_shdr32_fields);
        case XX_ELF_DATA_STRUCT_SECTION_HEADER64: ELF_SELECT(elf_shdr64_fields);
        case XX_ELF_DATA_STRUCT_SYMBOL32: ELF_SELECT(elf_sym32_fields);
        case XX_ELF_DATA_STRUCT_SYMBOL64: ELF_SELECT(elf_sym64_fields);
        case XX_ELF_DATA_STRUCT_REL32: ELF_SELECT(elf_rel32_fields);
        case XX_ELF_DATA_STRUCT_REL64: ELF_SELECT(elf_rel64_fields);
        case XX_ELF_DATA_STRUCT_RELA32: ELF_SELECT(elf_rela32_fields);
        case XX_ELF_DATA_STRUCT_RELA64: ELF_SELECT(elf_rela64_fields);
        case XX_ELF_DATA_STRUCT_RELR32: ELF_SELECT(elf_relr32_fields);
        case XX_ELF_DATA_STRUCT_RELR64: ELF_SELECT(elf_relr64_fields);
        case XX_ELF_DATA_STRUCT_DYNAMIC32: ELF_SELECT(elf_dyn32_fields);
        case XX_ELF_DATA_STRUCT_DYNAMIC64: ELF_SELECT(elf_dyn64_fields);
        case XX_ELF_DATA_STRUCT_COMPRESSION_HEADER32: ELF_SELECT(elf_chdr32_fields);
        case XX_ELF_DATA_STRUCT_COMPRESSION_HEADER64: ELF_SELECT(elf_chdr64_fields);
        case XX_ELF_DATA_STRUCT_NOTE_HEADER: ELF_SELECT(elf_nhdr_fields);
        case XX_ELF_DATA_STRUCT_SYSV_HASH_HEADER: ELF_SELECT(elf_sysv_hash_fields);
        case XX_ELF_DATA_STRUCT_GNU_HASH_HEADER: ELF_SELECT(elf_gnu_hash_fields);
        case XX_ELF_DATA_STRUCT_SYSV_HASH_BUCKET:
        case XX_ELF_DATA_STRUCT_SYSV_HASH_CHAIN:
        case XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM32:
        case XX_ELF_DATA_STRUCT_GNU_HASH_BUCKET:
        case XX_ELF_DATA_STRUCT_GNU_HASH_CHAIN:
        case XX_ELF_DATA_STRUCT_GROUP_WORD:
        case XX_ELF_DATA_STRUCT_SYMTAB_SHNDX:
            ELF_SELECT(elf_word_fields);
        case XX_ELF_DATA_STRUCT_GNU_HASH_BLOOM64:
            ELF_SELECT(elf_xword_fields);
        case XX_ELF_DATA_STRUCT_ADDRESS32: ELF_SELECT(elf_address32_fields);
        case XX_ELF_DATA_STRUCT_ADDRESS64: ELF_SELECT(elf_address64_fields);
        case XX_ELF_DATA_STRUCT_VERDEF: ELF_SELECT(elf_verdef_fields);
        case XX_ELF_DATA_STRUCT_VERDAUX: ELF_SELECT(elf_verdaux_fields);
        case XX_ELF_DATA_STRUCT_VERNEED: ELF_SELECT(elf_verneed_fields);
        case XX_ELF_DATA_STRUCT_VERNAUX: ELF_SELECT(elf_vernaux_fields);
        case XX_ELF_DATA_STRUCT_VERSYM: ELF_SELECT(elf_versym_fields);
        case XX_ELF_DATA_STRUCT_SYMINFO: ELF_SELECT(elf_syminfo_fields);
        case XX_ELF_DATA_STRUCT_AUXV32: ELF_SELECT(elf_auxv32_fields);
        case XX_ELF_DATA_STRUCT_AUXV64: ELF_SELECT(elf_auxv64_fields);
        case XX_ELF_DATA_STRUCT_NT_FILE_HEADER32: ELF_SELECT(elf_nt_file_head32_fields);
        case XX_ELF_DATA_STRUCT_NT_FILE_HEADER64: ELF_SELECT(elf_nt_file_head64_fields);
        case XX_ELF_DATA_STRUCT_NT_FILE_ENTRY32: ELF_SELECT(elf_nt_file_entry32_fields);
        case XX_ELF_DATA_STRUCT_NT_FILE_ENTRY64: ELF_SELECT(elf_nt_file_entry64_fields);
        case XX_ELF_DATA_STRUCT_GNU_ABI_TAG: ELF_SELECT(elf_gnu_abi_fields);
        case XX_ELF_DATA_STRUCT_GNU_HWCAP_HEADER: ELF_SELECT(elf_hwcap_fields);
        case XX_ELF_DATA_STRUCT_GNU_PROPERTY_HEADER: ELF_SELECT(elf_property_fields);
        case XX_ELF_DATA_STRUCT_LINUX_SIGINFO:
            if (elf->machine == ELF_MACHINE_MIPS)
                ELF_SELECT(elf_siginfo_mips_fields);
            ELF_SELECT(elf_siginfo_fields);
        case XX_ELF_DATA_STRUCT_ANDROID_IDENT_PREFIX: ELF_SELECT(elf_android_fields);
        case XX_ELF_DATA_STRUCT_MOVE32: ELF_SELECT(elf_move32_fields);
        case XX_ELF_DATA_STRUCT_MOVE64: ELF_SELECT(elf_move64_fields);
        case XX_ELF_DATA_STRUCT_LIB32:
        case XX_ELF_DATA_STRUCT_LIB64: ELF_SELECT(elf_lib_fields);
        case XX_ELF_DATA_STRUCT_SOLARIS_CAP32: ELF_SELECT(elf_cap32_fields);
        case XX_ELF_DATA_STRUCT_SOLARIS_CAP64: ELF_SELECT(elf_cap64_fields);
        case XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO32: ELF_SELECT(elf_capinfo32_fields);
        case XX_ELF_DATA_STRUCT_SOLARIS_CAPINFO64: ELF_SELECT(elf_capinfo64_fields);
        case XX_ELF_DATA_STRUCT_MIPS_REGINFO32: ELF_SELECT(elf_reginfo32_fields);
        case XX_ELF_DATA_STRUCT_MIPS_REGINFO64: ELF_SELECT(elf_reginfo64_fields);
        case XX_ELF_DATA_STRUCT_MIPS_OPTIONS: ELF_SELECT(elf_options_fields);
        case XX_ELF_DATA_STRUCT_MIPS_OPTIONS_HW: ELF_SELECT(elf_options_hw_fields);
        case XX_ELF_DATA_STRUCT_MIPS_GPTAB: ELF_SELECT(elf_gptab_fields);
        case XX_ELF_DATA_STRUCT_MIPS_CONFLICT32: ELF_SELECT(elf_conflict32_fields);
        case XX_ELF_DATA_STRUCT_MIPS_CONFLICT64: ELF_SELECT(elf_conflict64_fields);
        case XX_ELF_DATA_STRUCT_MIPS_ABI_FLAGS: ELF_SELECT(elf_abiflags_fields);
        case XX_ELF_DATA_STRUCT_LLVM_CGPROFILE: ELF_SELECT(elf_cgprofile_fields);
        default: return false;
    }
#undef ELF_SELECT
}

static void elf_record_stream_free(void *pointer) {
    elf_record_stream *stream = (elf_record_stream *)pointer;
    if (!stream) return;
    if (stream->fields) xx_mem_free(stream->fields);
    xx_mem_free(stream);
}

static bool elf_finish_record(xx_data_struct_record *record,
                              const xx_data_struct_field_desc *field,
                              const wchar_t *display) {
    if (xx_data_struct_record_set_name(record, field->name) &&
        xx_data_struct_record_set_type(record, field->type) &&
        xx_data_struct_record_set_display_value(record, display))
        return true;
    xx_data_struct_record_cleanup(record);
    return false;
}

static bool elf_populate_bytes(Abstractformat *format,
                               xx_data_struct_record_state *state,
                               const xx_data_struct_field_desc *field,
                               bool as_string) {
    xx_data_struct_record *record = &state->current_record;
    uint64_t size;
    uint64_t limit;
    uint64_t index;
    char *text;
    wchar_t display[ELF_DATA_MAX_DISPLAY * 3U + 8U];
    size_t display_at = 0U;
    if (field->size < 0 || state->parent_struct.offset < 0 ||
        field->rel_offset < 0 ||
        state->parent_struct.offset > INT64_MAX - field->rel_offset)
        return false;
    size = (uint64_t)field->size;
    limit = size > ELF_DATA_MAX_DISPLAY ? ELF_DATA_MAX_DISPLAY : size;
    text = (char *)xx_mem_alloc((size_t)limit + 1U);
    if (!text) return false;
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    display[0] = L'\0';
    for (index = 0U; index < limit; ++index) {
        uint8_t byte = xx_io_get_u8(
            format->device, state->parent_struct.offset + field->rel_offset +
                                (int64_t)index);
        if (as_string && byte == 0U) {
            text[index] = '\0';
            limit = index;
            break;
        }
        text[index] = as_string && byte >= 0x20U && byte < 0x7fU
                          ? (char)byte : (as_string ? '.' : (char)byte);
        if (!as_string && display_at + 3U < ELF_COUNT(display)) {
            display_at = elf_display_append_hex_byte(
                display, ELF_COUNT(display), display_at, byte);
        }
    }
    text[limit] = '\0';
    if (as_string) {
        size_t out;
        for (out = 0U; out < (size_t)limit && text[out] != '\0'; ++out)
            display[out] = (wchar_t)(unsigned char)text[out];
        display[out] = L'\0';
    } else if (size > limit && display_at + 4U < ELF_COUNT(display)) {
        display[display_at++] = L' ';
        display[display_at++] = L'.';
        display[display_at++] = L'.';
        display[display_at++] = L'.';
        display[display_at] = L'\0';
    }
    if (!xx_var_set_str(&record->value, text)) {
        xx_mem_free(text);
        xx_data_struct_record_cleanup(record);
        return false;
    }
    xx_mem_free(text);
    return elf_finish_record(record, field, display);
}

static uint64_t elf_record_unsigned(Abstractformat *format,
                                    const xx_data_struct_record_state *state,
                                    const xx_data_struct_field_desc *field,
                                    bool big_endian) {
    int64_t absolute = state->parent_struct.offset + field->rel_offset;
    if (field->size == 1) return xx_io_get_u8(format->device, absolute);
    if (field->size == 2)
        return xx_io_get_u16(format->device, absolute, big_endian);
    if (field->size == 4)
        return xx_io_get_u32(format->device, absolute, big_endian);
    return xx_io_get_u64(format->device, absolute, big_endian);
}

static bool elf_populate_logical(Abstractformat *format,
                                 xx_data_struct_record_state *state,
                                 const xx_data_struct_field_desc *field,
                                 const elf_record_stream *stream) {
    xx_data_struct_record *record = &state->current_record;
    uint64_t raw = elf_record_unsigned(format, state, field,
                                       stream->big_endian);
    uint64_t value = 0U;
    wchar_t display[64];
    bool matched = true;
    if (xx_rt_wcscmp(field->name, L"st_bind") == 0) value = raw >> 4U;
    else if (xx_rt_wcscmp(field->name, L"st_type") == 0) value = raw & 0x0fU;
    else if (xx_rt_wcscmp(field->name, L"st_visibility") == 0) value = raw & 0x03U;
    else if (xx_rt_wcscmp(field->name, L"r_sym") == 0) {
        if (stream->elf_class == XX_ELF_CLASS_32) value = raw >> 8U;
        else {
            if ((stream->machine == ELF_MACHINE_MIPS ||
                 stream->machine == 10U) && !stream->big_endian)
                raw = (raw << 32U) |
                      ((raw >> 8U) & UINT64_C(0xff000000)) |
                      ((raw >> 24U) & UINT64_C(0x00ff0000)) |
                      ((raw >> 40U) & UINT64_C(0x0000ff00)) |
                      ((raw >> 56U) & UINT64_C(0x000000ff));
            value = raw >> 32U;
        }
    } else if (xx_rt_wcscmp(field->name, L"r_type") == 0) {
        if (stream->elf_class == XX_ELF_CLASS_32) value = raw & 0xffU;
        else {
            if ((stream->machine == ELF_MACHINE_MIPS ||
                 stream->machine == 10U) && !stream->big_endian)
                raw = (raw << 32U) |
                      ((raw >> 8U) & UINT64_C(0xff000000)) |
                      ((raw >> 24U) & UINT64_C(0x00ff0000)) |
                      ((raw >> 40U) & UINT64_C(0x0000ff00)) |
                      ((raw >> 56U) & UINT64_C(0x000000ff));
            value = raw & UINT64_C(0xffffffff);
        }
    } else if (xx_rt_wcscmp(field->name, L"vs_index") == 0)
        value = raw & 0x7fffU;
    else if (xx_rt_wcscmp(field->name, L"vs_hidden") == 0)
        value = (raw & 0x8000U) != 0U;
    else if (xx_rt_wcscmp(field->name, L"m_sym") == 0) value = raw >> 8U;
    else if (xx_rt_wcscmp(field->name, L"m_size") == 0) value = raw & 0xffU;
    else if (xx_rt_wcscmp(field->name, L"ci_sym") == 0)
        value = stream->elf_class == XX_ELF_CLASS_64 ? raw >> 32U
                                                      : raw >> 8U;
    else if (xx_rt_wcscmp(field->name, L"ci_group") == 0)
        value = stream->elf_class == XX_ELF_CLASS_64
                    ? raw & UINT64_C(0xffffffff) : raw & 0xffU;
    else matched = false;
    if (!matched) return false;
    xx_data_struct_record_init(record);
    record->offset = field->rel_offset;
    record->size = field->size;
    record->property = field->property;
    xx_var_set_u64(&record->value, value);
    elf_display_u64(display, ELF_COUNT(display), value);
    return elf_finish_record(record, field, display);
}

static bool elf_is_logical_field(const wchar_t *name) {
    return xx_rt_wcscmp(name, L"st_bind") == 0 ||
           xx_rt_wcscmp(name, L"st_type") == 0 ||
           xx_rt_wcscmp(name, L"st_visibility") == 0 ||
           xx_rt_wcscmp(name, L"r_sym") == 0 ||
           xx_rt_wcscmp(name, L"r_type") == 0 ||
           xx_rt_wcscmp(name, L"vs_index") == 0 ||
           xx_rt_wcscmp(name, L"vs_hidden") == 0 ||
           xx_rt_wcscmp(name, L"m_sym") == 0 ||
           xx_rt_wcscmp(name, L"m_size") == 0 ||
           xx_rt_wcscmp(name, L"ci_sym") == 0 ||
           xx_rt_wcscmp(name, L"ci_group") == 0;
}

static bool elf_populate_record(Abstractformat *format,
                                xx_data_struct_record_state *state,
                                size_t index) {
    elf_record_stream *stream;
    const xx_data_struct_field_desc *field;
    if (!format || !format->device || !state ||
        !(stream = (elf_record_stream *)state->internal_state) ||
        index >= stream->count)
        return false;
    field = &stream->fields[index];
    if (elf_is_logical_field(field->name))
        return elf_populate_logical(format, state, field, stream);
    if (xx_rt_wcsncmp(field->type, L"char", 4U) == 0)
        return elf_populate_bytes(format, state, field, true);
    if (xx_rt_wcschr(field->type, L'[') != NULL)
        return elf_populate_bytes(format, state, field, false);
    if (xx_rt_wcscmp(field->type, L"int32") == 0 ||
        xx_rt_wcscmp(field->type, L"int64") == 0) {
        xx_data_struct_record *record = &state->current_record;
        int64_t signed_value;
        wchar_t display[64];
        xx_data_struct_record_init(record);
        record->offset = field->rel_offset;
        record->size = field->size;
        record->property = field->property;
        if (field->size == 8) {
            signed_value = (int64_t)xx_io_get_u64(
                format->device,
                state->parent_struct.offset + field->rel_offset,
                stream->big_endian);
            xx_var_set_i64(&record->value, signed_value);
        } else {
            signed_value = (int32_t)xx_io_get_u32(
                format->device,
                state->parent_struct.offset + field->rel_offset,
                stream->big_endian);
            xx_var_set_i32(&record->value, (int32_t)signed_value);
        }
        elf_display_i64(display, ELF_COUNT(display), signed_value);
        return elf_finish_record(record, field, display);
    }
    return xx_data_struct_record_populate(
        &state->current_record, format->device,
        state->parent_struct.offset, field, stream->big_endian);
}

xx_data_struct_record_state *xx_elf_create_data_struct_records_reading(
    Abstractformat *format, const xx_data_struct *data_struct,
    xx_pd_struct *pd) {
    const xx_data_struct_field_desc *static_fields = NULL;
    size_t static_count = 0U;
    size_t valid_count = 0U;
    size_t index;
    int64_t limit;
    bool variable_string;
    xx_data_struct_record_state *state = NULL;
    elf_record_stream *stream = NULL;
    xx_elf *elf = (xx_elf *)format;
    if (!format || !format->device || !data_struct ||
        xx_pd_is_stopped(pd) || data_struct->offset < 0 ||
        data_struct->total_size <= 0)
        return NULL;
    variable_string =
        data_struct->id == XX_ELF_DATA_STRUCT_STRING_DATA ||
        data_struct->id == XX_ELF_DATA_STRUCT_NOTE_NAME;
    if (data_struct->id == XX_ELF_DATA_STRUCT_RAW_DATA ||
        data_struct->id == XX_ELF_DATA_STRUCT_NOTE_DESC ||
        data_struct->id == XX_ELF_DATA_STRUCT_PROPERTY_DATA)
        return NULL;
    (void)elf_static_fields(elf, data_struct->id,
                            &static_fields, &static_count);
    limit = data_struct->entry_size > 0 &&
                    data_struct->entry_size < data_struct->total_size
                ? data_struct->entry_size : data_struct->total_size;
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 &&
            static_fields[index].rel_offset <= limit &&
            static_fields[index].size <=
                limit - static_fields[index].rel_offset)
            ++valid_count;
    }
    if (valid_count == 0U && !variable_string) return NULL;
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (elf_record_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) goto fail;
    stream->fields = (xx_data_struct_field_desc *)xx_mem_alloc(
        (valid_count + (variable_string ? 1U : 0U)) *
        sizeof(*stream->fields));
    if (!stream->fields) goto fail;
    for (index = 0U; index < static_count; ++index) {
        if (static_fields[index].rel_offset >= 0 &&
            static_fields[index].size > 0 &&
            static_fields[index].rel_offset <= limit &&
            static_fields[index].size <=
                limit - static_fields[index].rel_offset)
            stream->fields[stream->count++] = static_fields[index];
    }
    if (variable_string) {
        xx_data_struct_field_desc *field = &stream->fields[stream->count++];
        field->name = data_struct->id == XX_ELF_DATA_STRUCT_NOTE_NAME
                          ? L"note_name" : L"string_data";
        field->type = L"char[]";
        field->rel_offset = 0;
        field->size = data_struct->total_size;
        field->property = XX_DATA_STRUCT_RECORD_PROPERTY_STRING;
    }
    stream->big_endian = format->endian == XX_ENDIAN_BIG;
    stream->elf_class = elf->elf_class;
    stream->machine = elf->machine;
    xx_data_struct_record_state_init(state, format, data_struct);
    state->internal_state = stream;
    state->free_internal = elf_record_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!elf_populate_record(format, state, 0U)) goto fail_state;
    state->current_index = 0;
    state->has_record = true;
    return state;
fail_state:
    xx_data_struct_record_state_free(state);
    return NULL;
fail:
    if (stream) elf_record_stream_free(stream);
    if (state) xx_mem_free(state);
    return NULL;
}

const xx_data_struct_record *xx_elf_get_current_data_struct_record(
    Abstractformat *format, xx_data_struct_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_elf_data_struct_record_move_to_next(
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
    if (!elf_populate_record(format, state, next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)next;
    state->has_record = true;
    return true;
}

void xx_elf_free_data_struct_records_reading(
    Abstractformat *format, xx_data_struct_record_state *state) {
    (void)format;
    xx_data_struct_record_state_free(state);
}

void xx_elf_setup_data_struct_callbacks(xx_elf *elf) {
    if (!elf) return;
    elf->format.data_struct_id_to_string =
        xx_elf_data_struct_id_to_string;
    elf->format.data_struct_string_to_id =
        xx_elf_data_struct_string_to_id;
    elf->format.create_data_structs_reading =
        xx_elf_create_data_structs_reading;
    elf->format.get_current_data_struct =
        xx_elf_get_current_data_struct;
    elf->format.data_struct_move_to_next =
        xx_elf_data_struct_move_to_next;
    elf->format.free_data_structs_reading =
        xx_elf_free_data_structs_reading;
    elf->format.create_data_struct_records_reading =
        xx_elf_create_data_struct_records_reading;
    elf->format.get_current_data_struct_record =
        xx_elf_get_current_data_struct_record;
    elf->format.data_struct_record_move_to_next =
        xx_elf_data_struct_record_move_to_next;
    elf->format.free_data_struct_records_reading =
        xx_elf_free_data_struct_records_reading;
}
