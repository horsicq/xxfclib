/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <xxfclib/xxfclib.h>

#include "pe_dump.h"

#include <stdio.h>
#include <string.h>

#define XXFC_DUMP_MAX_ITEMS UINT64_C(10000000)

typedef enum dump_command_e {
    DUMP_COMMAND_UNKNOWN = 0,
    DUMP_COMMAND_MAP,
    DUMP_COMMAND_DATA_STRUCTS,
    DUMP_COMMAND_RESOURCES,
    DUMP_COMMAND_IMPORTS,
    DUMP_COMMAND_EXPORTS,
    DUMP_COMMAND_ARCHIVE_RECORDS
} dump_command;

static void print_usage(FILE *stream, const char *program) {
    fprintf(stream,
            "Usage: %s <command> <file>\n"
            "\n"
            "Commands:\n"
            "  --dump-map              Dump the common memory map\n"
            "  --dump-data-structs     Dump parsed format structures and fields\n"
            "  --dump-structs          Alias for --dump-data-structs\n"
            "  --dump-resources        Dump resource count and directory details\n"
            "  --dump-imports          Dump import count and directory details\n"
            "  --dump-exports          Dump export count and directory details\n"
            "  --dump-archive-records  Dump archive member records\n"
            "  --help                  Show this help\n",
            program);
}

static dump_command parse_command(const char *value) {
    if (!value) return DUMP_COMMAND_UNKNOWN;
    if (strcmp(value, "--dump-map") == 0) return DUMP_COMMAND_MAP;
    if (strcmp(value, "--dump-data-structs") == 0 ||
        strcmp(value, "--dump-structs") == 0)
        return DUMP_COMMAND_DATA_STRUCTS;
    if (strcmp(value, "--dump-resources") == 0)
        return DUMP_COMMAND_RESOURCES;
    if (strcmp(value, "--dump-imports") == 0) return DUMP_COMMAND_IMPORTS;
    if (strcmp(value, "--dump-exports") == 0) return DUMP_COMMAND_EXPORTS;
    if (strcmp(value, "--dump-archive-records") == 0)
        return DUMP_COMMAND_ARCHIVE_RECORDS;
    return DUMP_COMMAND_UNKNOWN;
}

static const char *file_type_name(xx_file_type_t type) {
    switch (type) {
        case XX_FILE_TYPE_UNKNOWN: return "UNKNOWN";
        case XX_FILE_TYPE_BINARY: return "BINARY";
        case XX_FILE_TYPE_ZIP: return "ZIP";
        case XX_FILE_TYPE_ZIP64: return "ZIP64";
        case XX_FILE_TYPE_7ZIP: return "7ZIP";
        case XX_FILE_TYPE_RAR: return "RAR";
        case XX_FILE_TYPE_AR: return "AR";
        case XX_FILE_TYPE_BZ2: return "BZ2";
        case XX_FILE_TYPE_GZ: return "GZ";
        case XX_FILE_TYPE_XZ: return "XZ";
        case XX_FILE_TYPE_TAR: return "TAR";
        case XX_FILE_TYPE_JAR: return "JAR";
        case XX_FILE_TYPE_APK: return "APK";
        case XX_FILE_TYPE_TAR_GZ: return "TAR.GZ";
        case XX_FILE_TYPE_TAR_BZ2: return "TAR.BZ2";
        case XX_FILE_TYPE_TAR_XZ: return "TAR.XZ";
        case XX_FILE_TYPE_IPA: return "IPA";
        case XX_FILE_TYPE_NPM: return "NPM";
        case XX_FILE_TYPE_ISO9660: return "ISO9660";
        case XX_FILE_TYPE_TAR_LZ4: return "TAR.LZ4";
        case XX_FILE_TYPE_ACE: return "ACE";
        case XX_FILE_TYPE_AIN: return "AIN";
        case XX_FILE_TYPE_ALDUS: return "ALDUS";
        case XX_FILE_TYPE_ALZ: return "ALZ";
        case XX_FILE_TYPE_AMPK: return "AMPK";
        case XX_FILE_TYPE_AODOS: return "AODOS";
        case XX_FILE_TYPE_ARCFS: return "ARCFS";
        case XX_FILE_TYPE_PDP11AR: return "PDP11AR";
        case XX_FILE_TYPE_ARTIPACK: return "ARTIPACK";
        case XX_FILE_TYPE_ARCV4: return "ARCV4";
        case XX_FILE_TYPE_ARCV2: return "ARCV2";
        case XX_FILE_TYPE_WARC: return "WARC";
        case XX_FILE_TYPE_TAR_ZSTD: return "TAR.ZSTD";
        case XX_FILE_TYPE_PE32: return "PE32";
        case XX_FILE_TYPE_PE64: return "PE64";
        case XX_FILE_TYPE_CPIO: return "CPIO";
        case XX_FILE_TYPE_MTREE: return "MTREE";
        case XX_FILE_TYPE_TAR_NEXTSTEP: return "TAR.NEXTSTEP";
        case XX_FILE_TYPE_TAR_COMPRESS: return "TAR.COMPRESS";
        case XX_FILE_TYPE_TAR_LZIP: return "TAR.LZIP";
        case XX_FILE_TYPE_TAR_LZMA: return "TAR.LZMA";
        case XX_FILE_TYPE_TAR_LZOP: return "TAR.LZOP";
        case XX_FILE_TYPE_MSDOS: return "MSDOS";
        case XX_FILE_TYPE_ARJ: return "ARJ";
        case XX_FILE_TYPE_CAB: return "CAB";
        case XX_FILE_TYPE_AIXBFF: return "BFF";
        case XX_FILE_TYPE_ARX: return "ARX";
        case XX_FILE_TYPE_ELF32: return "ELF32";
        case XX_FILE_TYPE_ELF64: return "ELF64";
        case XX_FILE_TYPE_MACHO32: return "MACH-O32";
        case XX_FILE_TYPE_MACHO64: return "MACH-O64";
        case XX_FILE_TYPE_NE: return "NE";
        case XX_FILE_TYPE_LE: return "LE";
        case XX_FILE_TYPE_LX: return "LX";
        case XX_FILE_TYPE_DEX: return "DEX";
        default: return "UNRECOGNIZED";
    }
}

static Abstractformat *create_format(xx_io_device *device,
                                     xx_file_type_t type) {
    switch (type) {
        case XX_FILE_TYPE_ZIP:
        case XX_FILE_TYPE_ZIP64:
            return (Abstractformat *)xx_zip_create(device, 0);
        case XX_FILE_TYPE_7ZIP:
            return (Abstractformat *)xx_7zip_create(device, 0);
        case XX_FILE_TYPE_RAR:
            return (Abstractformat *)xx_rar_create(device, 0);
        case XX_FILE_TYPE_AR:
            return (Abstractformat *)xx_ar_create(device, 0);
        case XX_FILE_TYPE_BZ2:
            return (Abstractformat *)xx_bz2_create(device, 0);
        case XX_FILE_TYPE_GZ:
            return (Abstractformat *)xx_gz_create(device, 0);
        case XX_FILE_TYPE_XZ:
            return (Abstractformat *)xx_xz_create(device, 0);
        case XX_FILE_TYPE_TAR:
            return (Abstractformat *)xx_tar_create(device, 0);
        case XX_FILE_TYPE_JAR:
            return (Abstractformat *)xx_jar_create(device, 0);
        case XX_FILE_TYPE_APK:
            return (Abstractformat *)xx_apk_create(device, 0);
        case XX_FILE_TYPE_TAR_GZ:
            return (Abstractformat *)xx_tar_gz_create(device, 0);
        case XX_FILE_TYPE_TAR_BZ2:
            return (Abstractformat *)xx_tar_bz2_create(device, 0);
        case XX_FILE_TYPE_TAR_XZ:
            return (Abstractformat *)xx_tar_xz_create(device, 0);
        case XX_FILE_TYPE_IPA:
            return (Abstractformat *)xx_ipa_create(device, 0);
        case XX_FILE_TYPE_NPM:
            return (Abstractformat *)xx_npm_create(device, 0);
        case XX_FILE_TYPE_ISO9660:
            return (Abstractformat *)xx_iso9660_create(device, 0);
        case XX_FILE_TYPE_TAR_LZ4:
            return (Abstractformat *)xx_tar_lz4_create(device, 0);
        case XX_FILE_TYPE_ACE:
            return (Abstractformat *)xx_ace_create(device, 0);
        case XX_FILE_TYPE_AIN:
            return (Abstractformat *)xx_ain_create(device, 0);
        case XX_FILE_TYPE_ALDUS:
            return (Abstractformat *)xx_aldus_create(device, 0);
        case XX_FILE_TYPE_ALZ:
            return (Abstractformat *)xx_alz_create(device, 0);
        case XX_FILE_TYPE_AMPK:
            return (Abstractformat *)xx_ampk_create(device, 0);
        case XX_FILE_TYPE_AODOS:
            return (Abstractformat *)xx_aodos_create(device, 0);
        case XX_FILE_TYPE_ARCFS:
            return (Abstractformat *)xx_arcfs_create(device, 0);
        case XX_FILE_TYPE_PDP11AR:
            return (Abstractformat *)xx_pdp11ar_create(device, 0);
        case XX_FILE_TYPE_ARTIPACK:
            return (Abstractformat *)xx_artipack_create(device, 0);
        case XX_FILE_TYPE_ARCV4:
            return (Abstractformat *)xx_arcv4_create(device, 0);
        case XX_FILE_TYPE_ARCV2:
            return (Abstractformat *)xx_arcv2_create(device, 0);
        case XX_FILE_TYPE_WARC:
            return (Abstractformat *)xx_warc_create(device, 0);
        case XX_FILE_TYPE_TAR_ZSTD:
            return (Abstractformat *)xx_tar_zstd_create(device, 0);
        case XX_FILE_TYPE_PE32:
        case XX_FILE_TYPE_PE64:
            return (Abstractformat *)xx_pe_create(device, 0);
        case XX_FILE_TYPE_MSDOS:
            return (Abstractformat *)xx_msdos_create(device, 0);
        case XX_FILE_TYPE_ARJ:
            return (Abstractformat *)xx_arj_create(device, 0);
        case XX_FILE_TYPE_CAB:
            return (Abstractformat *)xx_cab_create(device, 0);
        case XX_FILE_TYPE_AIXBFF:
            return (Abstractformat *)xx_aixbff_create(device, 0);
        case XX_FILE_TYPE_ARX:
            return (Abstractformat *)xx_arx_create(device, 0);
        case XX_FILE_TYPE_ELF32:
        case XX_FILE_TYPE_ELF64:
            return (Abstractformat *)xx_elf_create(device, 0);
        case XX_FILE_TYPE_MACHO32:
        case XX_FILE_TYPE_MACHO64:
            return (Abstractformat *)xx_macho_create(device, 0);
        case XX_FILE_TYPE_NE:
            return (Abstractformat *)xx_ne_create(device, 0);
        case XX_FILE_TYPE_LE:
            return (Abstractformat *)xx_le_create(device, 0);
        case XX_FILE_TYPE_LX:
            return (Abstractformat *)xx_lx_create(device, 0);
        case XX_FILE_TYPE_DEX:
            return (Abstractformat *)xx_dex_create(device, 0);
        case XX_FILE_TYPE_CPIO:
            return (Abstractformat *)xx_cpio_create(device, 0);
        case XX_FILE_TYPE_MTREE:
            return (Abstractformat *)xx_mtree_create(device, 0);
        case XX_FILE_TYPE_TAR_NEXTSTEP:
            return (Abstractformat *)xx_tar_nextstep_create(device, 0);
        case XX_FILE_TYPE_TAR_COMPRESS:
            return (Abstractformat *)xx_tar_compress_create(device, 0);
        case XX_FILE_TYPE_TAR_LZIP:
            return (Abstractformat *)xx_tar_lzip_create(device, 0);
        case XX_FILE_TYPE_TAR_LZMA:
            return (Abstractformat *)xx_tar_lzma_create(device, 0);
        case XX_FILE_TYPE_TAR_LZOP:
            return (Abstractformat *)xx_tar_lzop_create(device, 0);
        case XX_FILE_TYPE_BINARY:
        case XX_FILE_TYPE_UNKNOWN:
        default: {
            Abstractformat *format = xx_format_create(device, 0);
            if (format) xx_format_set_file_type(format, type);
            return format;
        }
    }
}

static void print_wide(const wchar_t *value) {
    char *utf8;
    if (!value) {
        fputs("(null)", stdout);
        return;
    }
    utf8 = xx_str_unicode_to_utf8(value);
    if (!utf8) {
        fputs("(conversion failed)", stdout);
        return;
    }
    fputs(utf8, stdout);
    xx_str_free_ansi(utf8);
}

static void append_flag(char *buffer, size_t capacity, const char *name) {
    size_t length;
    if (!buffer || capacity == 0U || !name) return;
    length = strlen(buffer);
    if (length >= capacity - 1U) return;
    (void)snprintf(buffer + length, capacity - length, "%s%s",
                   length == 0U ? "" : "|", name);
}

static const char *file_part_name(xx_file_part_t part, char *buffer,
                                  size_t capacity) {
    if (!buffer || capacity == 0U) return "";
    buffer[0] = '\0';
    if (part & XX_FILE_PART_REGION) append_flag(buffer, capacity, "REGION");
    if (part & XX_FILE_PART_SECTION) append_flag(buffer, capacity, "SECTION");
    if (part & XX_FILE_PART_SEGMENT) append_flag(buffer, capacity, "SEGMENT");
    if (part & XX_FILE_PART_HEADER) append_flag(buffer, capacity, "HEADER");
    if (part & XX_FILE_PART_OVERLAY) append_flag(buffer, capacity, "OVERLAY");
    if (part & XX_FILE_PART_RESOURCE) append_flag(buffer, capacity, "RESOURCE");
    if (part & XX_FILE_PART_DEBUG) append_flag(buffer, capacity, "DEBUG");
    if (part & XX_FILE_PART_STREAM) append_flag(buffer, capacity, "STREAM");
    if (part & XX_FILE_PART_SIGNATURE)
        append_flag(buffer, capacity, "SIGNATURE");
    if (part & XX_FILE_PART_FOOTER) append_flag(buffer, capacity, "FOOTER");
    if (part & XX_FILE_PART_DATA) append_flag(buffer, capacity, "DATA");
    if (part & XX_FILE_PART_OBJECT) append_flag(buffer, capacity, "OBJECT");
    if (part & XX_FILE_PART_TABLE) append_flag(buffer, capacity, "TABLE");
    if (part & XX_FILE_PART_VALUE) append_flag(buffer, capacity, "VALUE");
    if (buffer[0] == '\0') (void)snprintf(buffer, capacity, "%s", "UNKNOWN");
    return buffer;
}

static void format_offset_range(char *buffer, size_t capacity,
                                int64_t offset, int64_t size) {
    if (offset < 0 || size < 0) {
        (void)snprintf(buffer, capacity, "%s", "-");
        return;
    }
    (void)snprintf(buffer, capacity, "[0x%llX,0x%llX)",
                   (unsigned long long)offset,
                   (unsigned long long)((uint64_t)offset + (uint64_t)size));
}

static void format_address_range(char *buffer, size_t capacity,
                                 uint64_t address, int64_t size) {
    if (address == XX_INVALID_ADDRESS || size < 0) {
        (void)snprintf(buffer, capacity, "%s", "-");
        return;
    }
    (void)snprintf(buffer, capacity, "[0x%llX,0x%llX)",
                   (unsigned long long)address,
                   (unsigned long long)(address + (uint64_t)size));
}

static bool dump_memory_map(Abstractformat *format) {
    const xx_memory_map *map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL);
    size_t index;
    if (!map) {
        fprintf(stderr, "The format could not produce a memory map.\n");
        return false;
    }

    printf("Map mode:           %u\n", (unsigned)map->mode);
    printf("Module address:     0x%llX\n",
           (unsigned long long)map->module_address);
    if (map->entry_point_address == XX_INVALID_ADDRESS) {
        puts("Entry point:        -");
    } else {
        printf("Entry point:        0x%llX\n",
               (unsigned long long)map->entry_point_address);
    }
    printf("Binary offset:      0x%llX\n",
           (unsigned long long)map->binary_offset);
    printf("Binary size:        0x%llX\n",
           (unsigned long long)map->binary_size);
    printf("Image size:         0x%llX\n",
           (unsigned long long)map->image_size);
    printf("Records:            %llu\n\n",
           (unsigned long long)map->record_count);
    puts("Idx  Part                    Device range             Address range            Size       Flags  Name");
    puts("---  ----------------------  -----------------------  -----------------------  ---------  -----  ----");

    for (index = 0U; index < map->record_count; ++index) {
        const xx_memory_record *record = &map->records[index];
        char part[128];
        char device_range[48];
        char address_range[48];
        format_offset_range(device_range, sizeof(device_range), record->offset,
                            record->size);
        format_address_range(address_range, sizeof(address_range),
                             record->address, record->size);
        printf("%3llu  %-22s  %-23s  %-23s  0x%-7llX  %c%c     %s\n",
               (unsigned long long)index,
               file_part_name(record->file_part, part, sizeof(part)),
               device_range, address_range,
               (unsigned long long)record->size,
               record->is_virtual ? 'V' : 'P',
               record->is_invisible ? 'I' : '-', record->name);
    }
    return true;
}

static bool dump_data_structs(Abstractformat *format) {
    xx_data_struct_state *state;
    const xx_data_struct *data_struct;
    uint64_t count = 0U;
    if (!format->create_data_structs_reading) {
        fprintf(stderr,
                "This format does not provide data-structure records.\n");
        return false;
    }
    state = xx_format_create_data_structs_reading(format, NULL);
    if (!state) {
        fprintf(stderr, "Could not start data-structure reading.\n");
        return false;
    }

    data_struct = xx_format_get_current_data_struct(format, state);
    while (data_struct && count < XXFC_DUMP_MAX_ITEMS) {
        wchar_t *description =
            xx_format_data_struct_to_string(format, data_struct);
        xx_data_struct_record_state *record_state;
        const xx_data_struct_record *record;
        printf("[%llu] ", (unsigned long long)count);
        if (description) {
            print_wide(description);
        } else {
            printf("id=%u offset=%lld size=%lld count=%llu",
                   (unsigned)data_struct->id,
                   (long long)data_struct->offset,
                   (long long)data_struct->total_size,
                   (unsigned long long)data_struct->count);
        }
        if (data_struct->address >= 0)
            printf(" address=0x%llX",
                   (unsigned long long)data_struct->address);
        putchar('\n');
        xx_str_wfree(description);

        record_state = xx_format_create_data_struct_records_reading(
            format, data_struct, NULL);
        record = record_state
                     ? xx_format_get_current_data_struct_record(format,
                                                                record_state)
                     : NULL;
        while (record) {
            fputs("    ", stdout);
            print_wide(record->name);
            fputs(" (", stdout);
            print_wide(record->type);
            fputs(") = ", stdout);
            print_wide(record->display_value);
            printf("  [offset=%lld size=%lld property=0x%X]\n",
                   (long long)record->offset, (long long)record->size,
                   (unsigned)record->property);
            if (!xx_format_data_struct_record_move_to_next(
                    format, record_state, NULL))
                break;
            record = xx_format_get_current_data_struct_record(format,
                                                               record_state);
        }
        if (record_state)
            xx_format_free_data_struct_records_reading(format, record_state);

        ++count;
        if (!xx_format_data_struct_move_to_next(format, state, NULL)) break;
        data_struct = xx_format_get_current_data_struct(format, state);
    }
    xx_format_free_data_structs_reading(format, state);

    if (count == XXFC_DUMP_MAX_ITEMS) {
        fprintf(stderr, "Stopped after %llu data structures.\n",
                (unsigned long long)XXFC_DUMP_MAX_ITEMS);
        return false;
    }
    printf("\nTotal data structures: %llu\n", (unsigned long long)count);
    return true;
}

static void print_archive_name(const xx_archive_record *record) {
    const char *name = xx_archive_record_get_original_name(record);
    if (name) {
        fputs(name, stdout);
    } else {
        print_wide(xx_archive_record_get_original_name_w(record));
    }
}

static void print_archive_comment(const xx_archive_record *record) {
    const char *comment = xx_archive_record_get_meta_str(
        record, XX_META_ID_COMMENT);
    if (comment) {
        fputs(comment, stdout);
    } else {
        print_wide(xx_archive_record_get_meta_wstr(record,
                                                   XX_META_ID_COMMENT));
    }
}

static bool dump_archive_records(Abstractformat *format) {
    xx_archive_record_state *state;
    const xx_archive_record *record;
    uint64_t count = 0U;
    uint64_t declared =
        xx_format_get_number_of_archive_records(format, NULL);
    printf("Declared records:   %llu\n\n",
           (unsigned long long)declared);
    if (!format->create_archive_records_reading) {
        fprintf(stderr, "This format does not provide archive records.\n");
        return false;
    }
    state = xx_format_create_archive_records_reading(format, NULL, NULL);
    if (!state) {
        fprintf(stderr, "Could not start archive-record reading.\n");
        return false;
    }

    record = xx_format_get_current_archive_record(format, state);
    while (record && count < XXFC_DUMP_MAX_ITEMS) {
        uint64_t compressed_size = record->compressed_size >= 0
                                       ? (uint64_t)record->compressed_size
                                       : xx_archive_record_get_meta_u64(
                                             record,
                                             XX_META_ID_COMPRESSED_SIZE, 0U);
        bool folder = xx_archive_record_get_meta_bool(
            record, XX_META_ID_IS_FOLDER, false);
        bool encrypted = xx_archive_record_get_meta_bool(
            record, XX_META_ID_IS_ENCRYPTED, false);
        printf("[%llu] ", (unsigned long long)count);
        print_archive_name(record);
        if (folder) fputs(" [directory]", stdout);
        putchar('\n');
        printf("    Header:       offset=%lld size=%lld\n",
               (long long)record->header_offset,
               (long long)record->header_size);
        printf("    Data offset:  %lld\n", (long long)record->data_offset);
        if (xx_archive_record_find_meta(record,
                                        XX_META_ID_UNCOMPRESSED_SIZE))
            printf("    Uncompressed: %llu\n",
                   (unsigned long long)xx_archive_record_get_meta_u64(
                       record, XX_META_ID_UNCOMPRESSED_SIZE, 0U));
        printf("    Compressed:   %llu\n",
               (unsigned long long)compressed_size);
        if (xx_archive_record_find_meta(record, XX_META_ID_CRC32))
            printf("    CRC32:        0x%08llX\n",
                   (unsigned long long)xx_archive_record_get_meta_u64(
                       record, XX_META_ID_CRC32, 0U));
        if (xx_archive_record_find_meta(record,
                                        XX_META_ID_COMPRESSION_METHOD))
            printf("    Method:       %llu\n",
                   (unsigned long long)xx_archive_record_get_meta_u64(
                       record, XX_META_ID_COMPRESSION_METHOD, 0U));
        if (xx_archive_record_find_meta(record, XX_META_ID_FLAGS))
            printf("    Flags:        0x%llX\n",
                   (unsigned long long)xx_archive_record_get_meta_u64(
                       record, XX_META_ID_FLAGS, 0U));
        if (xx_archive_record_find_meta(record,
                                        XX_META_ID_DISK_NUMBER_START))
            printf("    Start disk:   %llu\n",
                   (unsigned long long)xx_archive_record_get_meta_u64(
                       record, XX_META_ID_DISK_NUMBER_START, 0U));
        printf("    Encrypted:    %s\n", encrypted ? "yes" : "no");
        if (xx_archive_record_find_meta(record, XX_META_ID_COMMENT)) {
            fputs("    Comment:      ", stdout);
            print_archive_comment(record);
            putchar('\n');
        }
        putchar('\n');

        ++count;
        if (!xx_format_archive_record_move_to_next(format, state, NULL))
            break;
        record = xx_format_get_current_archive_record(format, state);
    }
    xx_format_free_archive_records_reading(format, state);

    if (count == XXFC_DUMP_MAX_ITEMS) {
        fprintf(stderr, "Stopped after %llu archive records.\n",
                (unsigned long long)XXFC_DUMP_MAX_ITEMS);
        return false;
    }
    printf("Total archive records: %llu\n", (unsigned long long)count);
    return true;
}

static bool dump_pe_directory(Abstractformat *format, dump_command command) {
    const char *label;
    const char *count_label;
    uint32_t index;
    uint64_t count;

    switch (command) {
        case DUMP_COMMAND_EXPORTS:
            label = "Export";
            count_label = "Export functions";
            index = 0U;
            count = xx_format_get_number_of_exports(format, NULL);
            break;
        case DUMP_COMMAND_IMPORTS:
            label = "Import";
            count_label = "Import descriptors";
            index = 1U;
            count = xx_format_get_number_of_imports(format, NULL);
            break;
        case DUMP_COMMAND_RESOURCES:
            label = "Resource";
            count_label = "Resource leaves";
            index = 2U;
            count = xx_format_get_number_of_resources(format, NULL);
            break;
        default:
            return false;
    }

    printf("%s: %llu\n", count_label, (unsigned long long)count);
    if (format->file_type == XX_FILE_TYPE_PE32 ||
        format->file_type == XX_FILE_TYPE_PE64) {
        const xx_pe *pe = (const xx_pe *)format;
        uint32_t rva = 0U;
        uint32_t size = 0U;
        int64_t offset = -1;
        uint64_t address = XX_INVALID_ADDRESS;
        if (index < pe->number_of_data_directories && index < 16U) {
            rva = pe->data_directory_rva[index];
            size = pe->data_directory_size[index];
            if (rva != 0U) {
                offset = xx_format_rel_address_to_offset(format,
                                                         (int64_t)rva, NULL);
                address = xx_format_rel_address_to_address(format,
                                                           (int64_t)rva, NULL);
            }
        }
        printf("%s directory RVA: 0x%08X\n", label, (unsigned)rva);
        if (address == XX_INVALID_ADDRESS)
            printf("%s directory VA:  -\n", label);
        else
            printf("%s directory VA:  0x%llX\n", label,
                   (unsigned long long)address);
        if (offset < 0)
            printf("%s file offset:   -\n", label);
        else
            printf("%s file offset:   0x%llX\n", label,
                   (unsigned long long)offset);
        printf("%s directory size: 0x%08X\n", label, (unsigned)size);
    } else {
        puts("Directory mapping details are currently available for PE files.");
    }
    return true;
}

static int run_command(dump_command command, const char *path) {
    xx_io_device *device = xx_io_file_open(path, "rb");
    xx_file_type_t detected;
    Abstractformat *format;
    bool ok = false;
    if (!device) {
        fprintf(stderr, "Cannot open file: %s\n", path);
        return 1;
    }

    detected = xx_format_get_file_type_device(device);
    (void)xx_io_seek64(device, 0, SEEK_SET);
    format = create_format(device, detected);
    if (!format) {
        fprintf(stderr, "Cannot create a parser for %s.\n",
                file_type_name(detected));
        xx_io_close(device);
        return 1;
    }

    if (detected != XX_FILE_TYPE_UNKNOWN &&
        detected != XX_FILE_TYPE_BINARY &&
        !xx_format_is_valid(format, NULL)) {
        fprintf(stderr, "Detected %s, but validation failed: %s\n",
                file_type_name(detected), path);
        xx_format_free(format);
        xx_io_close(device);
        return 1;
    }

    printf("File:               %s\n", path);
    printf("Detected format:    %s\n\n", file_type_name(detected));
    switch (command) {
        case DUMP_COMMAND_MAP:
            ok = dump_memory_map(format);
            break;
        case DUMP_COMMAND_DATA_STRUCTS:
            ok = dump_data_structs(format);
            break;
        case DUMP_COMMAND_RESOURCES:
        case DUMP_COMMAND_IMPORTS:
        case DUMP_COMMAND_EXPORTS: {
            bool is_pe = detected == XX_FILE_TYPE_PE32 ||
                         detected == XX_FILE_TYPE_PE64;
            if (!is_pe) {
                ok = dump_pe_directory(format, command);
            } else if (command == DUMP_COMMAND_IMPORTS) {
                ok = xxfc_dump_pe_imports(format);
            } else if (command == DUMP_COMMAND_EXPORTS) {
                ok = xxfc_dump_pe_exports(format);
            } else {
                ok = xxfc_dump_pe_resources(format);
            }
            break;
        }
        case DUMP_COMMAND_ARCHIVE_RECORDS:
            ok = dump_archive_records(format);
            break;
        case DUMP_COMMAND_UNKNOWN:
        default:
            break;
    }

    xx_format_free(format);
    xx_io_close(device);
    return ok ? 0 : 2;
}

int main(int argc, char **argv) {
    dump_command command;
    if (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    if (argc != 3) {
        print_usage(stderr, argv[0]);
        return 1;
    }
    command = parse_command(argv[1]);
    if (command == DUMP_COMMAND_UNKNOWN) {
        fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
        print_usage(stderr, argv[0]);
        return 1;
    }
    return run_command(command, argv[2]);
}
