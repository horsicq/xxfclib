/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "pe_dump.h"

#include <xxfclib/xxfclib.h>

#include <limits.h>
#include <stdio.h>

#define PE_DIRECTORY_EXPORT 0U
#define PE_DIRECTORY_IMPORT 1U
#define PE_DIRECTORY_RESOURCE 2U
#define PE_MAX_DESCRIPTORS UINT64_C(65536)
#define PE_MAX_ITEMS UINT64_C(1000000)
#define PE_MAX_STRING 4096U
#define PE_MAX_RESOURCE_DEPTH 32U

typedef struct pe_dump_context_s {
    Abstractformat *format;
    xx_pe *pe;
    xx_io_device *device;
    const xx_memory_map *map;
    int64_t total_size;
} pe_dump_context;

static bool pe_context_init(pe_dump_context *context,
                            Abstractformat *format) {
    if (!context || !format ||
        !xx_format_handle_base_info(format, NULL) ||
        (format->file_type != XX_FILE_TYPE_PE32 &&
         format->file_type != XX_FILE_TYPE_PE64))
        return false;
    context->format = format;
    context->pe = (xx_pe *)format;
    context->device = format->device;
    context->map = xx_format_get_memory_map(
        format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL);
    context->total_size = context->device
                              ? xx_io_total_size(context->device)
                              : -1;
    return context->map && context->total_size >= 0;
}

static int64_t pe_rva_range_offset(const pe_dump_context *context,
                                   uint64_t rva, uint64_t size) {
    uint64_t address;
    int64_t offset;
    if (!context || !context->map || !context->device || size == 0U ||
        rva > (uint64_t)INT64_MAX || size > (uint64_t)INT64_MAX)
        return -1;
    address = xx_memory_map_relative_address_to_address(
        context->map, (int64_t)rva);
    if (address == XX_INVALID_ADDRESS ||
        !xx_memory_map_is_physical_address_range(context->map, address,
                                                  (int64_t)size))
        return -1;
    offset = xx_memory_map_relative_address_to_offset(context->map,
                                                       (int64_t)rva);
    if (offset < 0 || offset > context->total_size ||
        size > (uint64_t)(context->total_size - offset))
        return -1;
    return offset;
}

static char *pe_read_ascii(const pe_dump_context *context, uint64_t rva) {
    char *result;
    size_t index;
    if (pe_rva_range_offset(context, rva, 1U) < 0) return NULL;
    result = (char *)xx_mem_alloc(PE_MAX_STRING + 1U);
    if (!result) return NULL;
    for (index = 0U; index < PE_MAX_STRING; ++index) {
        int64_t offset;
        uint8_t value;
        if (rva > UINT64_MAX - index ||
            (offset = pe_rva_range_offset(context, rva + index, 1U)) < 0) {
            xx_mem_free(result);
            return NULL;
        }
        value = xx_io_get_u8(context->device, offset);
        result[index] = (char)value;
        if (value == 0U) return result;
    }
    result[PE_MAX_STRING] = '\0';
    return result;
}

static void pe_print_directory(const pe_dump_context *context,
                               const char *label, uint32_t index,
                               const char *count_label, uint64_t count) {
    uint32_t rva = 0U;
    uint32_t size = 0U;
    int64_t offset = -1;
    uint64_t address = XX_INVALID_ADDRESS;
    if (index < context->pe->number_of_data_directories && index < 16U) {
        rva = context->pe->data_directory_rva[index];
        size = context->pe->data_directory_size[index];
        if (rva != 0U) {
            offset = xx_memory_map_relative_address_to_offset(
                context->map, (int64_t)rva);
            address = xx_memory_map_relative_address_to_address(
                context->map, (int64_t)rva);
        }
    }
    printf("%s: %llu\n", count_label, (unsigned long long)count);
    printf("%s directory RVA:  0x%08X\n", label, (unsigned)rva);
    if (address == XX_INVALID_ADDRESS)
        printf("%s directory VA:   -\n", label);
    else
        printf("%s directory VA:   0x%llX\n", label,
               (unsigned long long)address);
    if (offset < 0)
        printf("%s file offset:    -\n", label);
    else
        printf("%s file offset:    0x%llX\n", label,
               (unsigned long long)offset);
    printf("%s directory size: 0x%08X\n\n", label, (unsigned)size);
}

bool xxfc_dump_pe_imports(Abstractformat *format) {
    pe_dump_context context;
    uint32_t directory_rva;
    uint32_t directory_size;
    uint64_t descriptor_limit;
    uint64_t descriptor_index;
    uint64_t symbol_budget = PE_MAX_ITEMS;
    uint64_t total_symbols = 0U;
    if (!pe_context_init(&context, format)) {
        fprintf(stderr, "Detailed imports require a valid PE file.\n");
        return false;
    }
    pe_print_directory(&context, "Import", PE_DIRECTORY_IMPORT,
                       "Import descriptors",
                       xx_format_get_number_of_imports(format, NULL));
    directory_rva = context.pe->data_directory_rva[PE_DIRECTORY_IMPORT];
    directory_size = context.pe->data_directory_size[PE_DIRECTORY_IMPORT];
    if (directory_rva == 0U || directory_size < 20U) return true;
    descriptor_limit = directory_size / 20U;
    if (descriptor_limit > PE_MAX_DESCRIPTORS)
        descriptor_limit = PE_MAX_DESCRIPTORS;

    for (descriptor_index = 0U; descriptor_index < descriptor_limit;
         ++descriptor_index) {
        uint64_t descriptor_rva =
            (uint64_t)directory_rva + descriptor_index * 20U;
        int64_t descriptor_offset =
            pe_rva_range_offset(&context, descriptor_rva, 20U);
        uint32_t original_thunk;
        uint32_t timestamp;
        uint32_t forwarder_chain;
        uint32_t name_rva;
        uint32_t first_thunk;
        uint32_t thunk_rva;
        uint64_t symbol_index;
        char *dll_name;
        if (descriptor_offset < 0) break;
        original_thunk = xx_io_get_u32(context.device, descriptor_offset,
                                       false);
        timestamp = xx_io_get_u32(context.device, descriptor_offset + 4,
                                  false);
        forwarder_chain = xx_io_get_u32(context.device,
                                        descriptor_offset + 8, false);
        name_rva = xx_io_get_u32(context.device, descriptor_offset + 12,
                                 false);
        first_thunk = xx_io_get_u32(context.device, descriptor_offset + 16,
                                    false);
        if (original_thunk == 0U && timestamp == 0U &&
            forwarder_chain == 0U && name_rva == 0U && first_thunk == 0U)
            break;
        dll_name = pe_read_ascii(&context, name_rva);
        printf("[%llu] %s  OFT=0x%08X IAT=0x%08X timestamp=0x%08X\n",
               (unsigned long long)descriptor_index,
               dll_name ? dll_name : "(invalid DLL name)",
               (unsigned)original_thunk, (unsigned)first_thunk,
               (unsigned)timestamp);
        xx_mem_free(dll_name);

        thunk_rva = original_thunk ? original_thunk : first_thunk;
        for (symbol_index = 0U;
             thunk_rva != 0U && symbol_budget != 0U; ++symbol_index) {
            uint64_t width = xx_pe_is_64(context.pe) ? 8U : 4U;
            uint64_t entry_rva = (uint64_t)thunk_rva + symbol_index * width;
            uint64_t iat_rva = (uint64_t)first_thunk + symbol_index * width;
            int64_t entry_offset =
                pe_rva_range_offset(&context, entry_rva, width);
            uint64_t value;
            uint64_t ordinal_flag = xx_pe_is_64(context.pe)
                                        ? UINT64_C(0x8000000000000000)
                                        : UINT64_C(0x80000000);
            if (entry_offset < 0) {
                puts("    (invalid thunk table range)");
                break;
            }
            value = width == 8U
                        ? xx_io_get_u64(context.device, entry_offset, false)
                        : xx_io_get_u32(context.device, entry_offset, false);
            if (value == 0U) break;
            --symbol_budget;
            ++total_symbols;
            if ((value & ordinal_flag) != 0U) {
                printf("    [%llu] ordinal #%u  thunk=0x%llX iat=0x%llX\n",
                       (unsigned long long)symbol_index,
                       (unsigned)(value & UINT64_C(0xffff)),
                       (unsigned long long)entry_rva,
                       (unsigned long long)iat_rva);
            } else if (value <= UINT32_MAX) {
                int64_t name_offset = pe_rva_range_offset(
                    &context, value, 2U);
                if (name_offset >= 0) {
                    uint16_t hint = xx_io_get_u16(context.device, name_offset,
                                                  false);
                    char *symbol_name = pe_read_ascii(&context, value + 2U);
                    printf("    [%llu] %s  hint=%u thunk=0x%llX iat=0x%llX\n",
                           (unsigned long long)symbol_index,
                           symbol_name ? symbol_name : "(invalid symbol name)",
                           (unsigned)hint, (unsigned long long)entry_rva,
                           (unsigned long long)iat_rva);
                    xx_mem_free(symbol_name);
                } else {
                    printf("    [%llu] invalid name RVA 0x%llX\n",
                           (unsigned long long)symbol_index,
                           (unsigned long long)value);
                }
            } else {
                printf("    [%llu] invalid import value 0x%llX\n",
                       (unsigned long long)symbol_index,
                       (unsigned long long)value);
            }
        }
        putchar('\n');
        if (symbol_budget == 0U) {
            fprintf(stderr, "Stopped after %llu imported symbols.\n",
                    (unsigned long long)PE_MAX_ITEMS);
            return false;
        }
    }
    printf("Total imported symbols: %llu\n",
           (unsigned long long)total_symbols);
    return true;
}

static void pe_print_export(const pe_dump_context *context,
                            uint64_t ordinal, uint32_t function_rva,
                            const char *name, uint32_t directory_rva,
                            uint32_t directory_size) {
    uint64_t address = XX_INVALID_ADDRESS;
    int64_t offset = -1;
    char *forwarder = NULL;
    if (function_rva != 0U) {
        address = xx_memory_map_relative_address_to_address(
            context->map, (int64_t)function_rva);
        offset = xx_memory_map_relative_address_to_offset(
            context->map, (int64_t)function_rva);
        if ((uint64_t)function_rva >= directory_rva &&
            (uint64_t)function_rva <
                (uint64_t)directory_rva + directory_size)
            forwarder = pe_read_ascii(context, function_rva);
    }
    printf("  ordinal=%llu rva=0x%08X",
           (unsigned long long)ordinal, (unsigned)function_rva);
    if (address != XX_INVALID_ADDRESS)
        printf(" va=0x%llX", (unsigned long long)address);
    if (offset >= 0)
        printf(" offset=0x%llX", (unsigned long long)offset);
    if (name) printf(" name=%s", name);
    if (forwarder) printf(" forwarder=%s", forwarder);
    putchar('\n');
    xx_mem_free(forwarder);
}

bool xxfc_dump_pe_exports(Abstractformat *format) {
    pe_dump_context context;
    uint32_t directory_rva;
    uint32_t directory_size;
    int64_t directory_offset;
    uint32_t ordinal_base;
    uint32_t function_count;
    uint32_t name_count;
    uint32_t functions_rva;
    uint32_t names_rva;
    uint32_t ordinals_rva;
    int64_t functions_offset;
    int64_t names_offset;
    int64_t ordinals_offset;
    uint8_t *named = NULL;
    uint32_t index;
    if (!pe_context_init(&context, format)) {
        fprintf(stderr, "Detailed exports require a valid PE file.\n");
        return false;
    }
    pe_print_directory(&context, "Export", PE_DIRECTORY_EXPORT,
                       "Export functions",
                       xx_format_get_number_of_exports(format, NULL));
    directory_rva = context.pe->data_directory_rva[PE_DIRECTORY_EXPORT];
    directory_size = context.pe->data_directory_size[PE_DIRECTORY_EXPORT];
    if (directory_rva == 0U || directory_size < 40U) return true;
    directory_offset = pe_rva_range_offset(&context, directory_rva, 40U);
    if (directory_offset < 0) {
        fprintf(stderr, "Invalid PE export directory.\n");
        return false;
    }
    ordinal_base = xx_io_get_u32(context.device, directory_offset + 16,
                                 false);
    function_count = xx_io_get_u32(context.device, directory_offset + 20,
                                   false);
    name_count = xx_io_get_u32(context.device, directory_offset + 24, false);
    functions_rva = xx_io_get_u32(context.device, directory_offset + 28,
                                  false);
    names_rva = xx_io_get_u32(context.device, directory_offset + 32, false);
    ordinals_rva = xx_io_get_u32(context.device, directory_offset + 36,
                                 false);
    if (function_count > PE_MAX_ITEMS || name_count > PE_MAX_ITEMS) {
        fprintf(stderr, "PE export table exceeds the sample safety limit.\n");
        return false;
    }
    if (function_count == 0U) return true;
    functions_offset = pe_rva_range_offset(
        &context, functions_rva, (uint64_t)function_count * 4U);
    if (functions_offset < 0) {
        fprintf(stderr, "Invalid PE export address table.\n");
        return false;
    }
    names_offset = name_count
                       ? pe_rva_range_offset(
                             &context, names_rva, (uint64_t)name_count * 4U)
                       : -1;
    ordinals_offset = name_count
                          ? pe_rva_range_offset(
                                &context, ordinals_rva,
                                (uint64_t)name_count * 2U)
                          : -1;
    if (name_count && (names_offset < 0 || ordinals_offset < 0)) {
        fprintf(stderr, "Invalid PE export name tables.\n");
        return false;
    }
    named = (uint8_t *)xx_mem_alloc(function_count);
    if (!named) return false;
    xx_mem_zero(named, function_count);

    for (index = 0U; index < name_count; ++index) {
        uint32_t name_rva = xx_io_get_u32(context.device,
                                          names_offset + (int64_t)index * 4,
                                          false);
        uint16_t function_index = xx_io_get_u16(
            context.device, ordinals_offset + (int64_t)index * 2, false);
        char *name = pe_read_ascii(&context, name_rva);
        if (function_index < function_count) {
            uint32_t function_rva = xx_io_get_u32(
                context.device,
                functions_offset + (int64_t)function_index * 4, false);
            named[function_index] = 1U;
            pe_print_export(&context,
                            (uint64_t)ordinal_base + function_index,
                            function_rva,
                            name ? name : "(invalid export name)",
                            directory_rva, directory_size);
        } else {
            printf("  invalid name ordinal index=%u name=%s\n",
                   (unsigned)function_index,
                   name ? name : "(invalid export name)");
        }
        xx_mem_free(name);
    }
    for (index = 0U; index < function_count; ++index) {
        if (!named[index]) {
            uint32_t function_rva = xx_io_get_u32(
                context.device, functions_offset + (int64_t)index * 4,
                false);
            pe_print_export(&context, (uint64_t)ordinal_base + index,
                            function_rva, NULL, directory_rva,
                            directory_size);
        }
    }
    xx_mem_free(named);
    return true;
}

static const char *pe_resource_type_name(uint32_t id) {
    switch (id) {
        case 1U: return "CURSOR";
        case 2U: return "BITMAP";
        case 3U: return "ICON";
        case 4U: return "MENU";
        case 5U: return "DIALOG";
        case 6U: return "STRING";
        case 7U: return "FONTDIR";
        case 8U: return "FONT";
        case 9U: return "ACCELERATOR";
        case 10U: return "RCDATA";
        case 11U: return "MESSAGETABLE";
        case 12U: return "GROUP_CURSOR";
        case 14U: return "GROUP_ICON";
        case 16U: return "VERSION";
        case 17U: return "DLGINCLUDE";
        case 19U: return "PLUGPLAY";
        case 20U: return "VXD";
        case 21U: return "ANI_CURSOR";
        case 22U: return "ANI_ICON";
        case 23U: return "HTML";
        case 24U: return "MANIFEST";
        default: return NULL;
    }
}

static char *pe_utf16_resource_name(const pe_dump_context *context,
                                    uint32_t root_rva, uint32_t directory_size,
                                    uint32_t relative) {
    int64_t length_offset;
    uint16_t length;
    char *result;
    size_t output = 0U;
    uint32_t index;
    uint64_t byte_count;
    if (relative > directory_size || directory_size - relative < 2U ||
        (uint64_t)root_rva + relative > UINT32_MAX)
        return NULL;
    length_offset = pe_rva_range_offset(
        context, (uint64_t)root_rva + relative, 2U);
    if (length_offset < 0) return NULL;
    length = xx_io_get_u16(context->device, length_offset, false);
    byte_count = (uint64_t)length * 2U;
    if (byte_count > directory_size - relative - 2U ||
        (byte_count != 0U &&
         pe_rva_range_offset(context,
                             (uint64_t)root_rva + relative + 2U,
                             byte_count) < 0))
        return NULL;
    result = (char *)xx_mem_alloc((size_t)length * 3U + 1U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint16_t value = xx_io_get_u16(
            context->device, length_offset + 2 + (int64_t)index * 2,
            false);
        uint32_t codepoint = value;
        if (value >= 0xd800U && value <= 0xdbffU && index + 1U < length) {
            uint16_t low = xx_io_get_u16(
                context->device,
                length_offset + 2 + (int64_t)(index + 1U) * 2, false);
            if (low >= 0xdc00U && low <= 0xdfffU) {
                codepoint = UINT32_C(0x10000) +
                            (((uint32_t)value - UINT32_C(0xd800)) << 10) +
                            ((uint32_t)low - UINT32_C(0xdc00));
                ++index;
            }
        }
        if (codepoint < 0x80U) {
            result[output++] = (char)codepoint;
        } else if (codepoint < 0x800U) {
            result[output++] = (char)(0xc0U | (codepoint >> 6));
            result[output++] = (char)(0x80U | (codepoint & 0x3fU));
        } else if (codepoint < 0x10000U) {
            result[output++] = (char)(0xe0U | (codepoint >> 12));
            result[output++] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
            result[output++] = (char)(0x80U | (codepoint & 0x3fU));
        } else {
            result[output++] = (char)(0xf0U | (codepoint >> 18));
            result[output++] = (char)(0x80U | ((codepoint >> 12) & 0x3fU));
            result[output++] = (char)(0x80U | ((codepoint >> 6) & 0x3fU));
            result[output++] = (char)(0x80U | (codepoint & 0x3fU));
        }
    }
    result[output] = '\0';
    return result;
}

static void pe_print_indent(uint32_t depth) {
    uint32_t index;
    for (index = 0U; index < depth; ++index) fputs("  ", stdout);
}

static bool pe_dump_resource_directory(
    const pe_dump_context *context, uint32_t root_rva,
    uint32_t directory_size, uint32_t relative, uint32_t depth,
    uint32_t *ancestors, uint64_t *budget, uint64_t *leaves) {
    int64_t directory_offset;
    uint32_t entry_count;
    uint32_t index;
    if (!context || !ancestors || !budget || !leaves || *budget == 0U ||
        depth > PE_MAX_RESOURCE_DEPTH || relative > directory_size ||
        directory_size - relative < 16U ||
        (uint64_t)root_rva + relative > UINT32_MAX)
        return false;
    directory_offset = pe_rva_range_offset(
        context, (uint64_t)root_rva + relative, 16U);
    if (directory_offset < 0) return false;
    entry_count =
        (uint32_t)xx_io_get_u16(context->device, directory_offset + 12,
                                false) +
        xx_io_get_u16(context->device, directory_offset + 14, false);
    if (entry_count > (directory_size - relative - 16U) / 8U)
        return false;
    ancestors[depth] = relative;

    for (index = 0U; index < entry_count; ++index) {
        int64_t entry_offset;
        uint32_t name_value;
        uint32_t child_value;
        uint32_t child_relative;
        char *name = NULL;
        const char *level = depth == 0U ? "Type"
                            : depth == 1U ? "Name"
                            : depth == 2U ? "Language"
                                          : "Level";
        if (*budget == 0U) return false;
        --*budget;
        entry_offset = pe_rva_range_offset(
            context, (uint64_t)root_rva + relative + 16U +
                         (uint64_t)index * 8U,
            8U);
        if (entry_offset < 0) return false;
        name_value = xx_io_get_u32(context->device, entry_offset, false);
        child_value = xx_io_get_u32(context->device, entry_offset + 4,
                                    false);
        child_relative = child_value & UINT32_C(0x7fffffff);
        pe_print_indent(depth);
        printf("%s ", level);
        if (name_value & UINT32_C(0x80000000)) {
            name = pe_utf16_resource_name(
                context, root_rva, directory_size,
                name_value & UINT32_C(0x7fffffff));
            printf("\"%s\"", name ? name : "(invalid name)");
        } else {
            uint32_t id = name_value & UINT32_C(0x7fffffff);
            const char *type_name =
                depth == 0U ? pe_resource_type_name(id) : NULL;
            if (type_name)
                printf("%s(%u)", type_name, (unsigned)id);
            else
                printf("#%u", (unsigned)id);
        }
        if (child_value & UINT32_C(0x80000000)) {
            uint32_t ancestor_index;
            bool cycle = false;
            puts(" -> directory");
            for (ancestor_index = 0U; ancestor_index <= depth;
                 ++ancestor_index) {
                if (ancestors[ancestor_index] == child_relative) {
                    cycle = true;
                    break;
                }
            }
            xx_mem_free(name);
            if (cycle) {
                pe_print_indent(depth + 1U);
                puts("(resource directory cycle)");
                return false;
            }
            if (!pe_dump_resource_directory(
                    context, root_rva, directory_size, child_relative,
                    depth + 1U, ancestors, budget, leaves))
                return false;
        } else {
            int64_t data_entry_offset;
            uint32_t data_rva;
            uint32_t data_size;
            uint32_t code_page;
            int64_t data_offset;
            uint64_t data_address;
            if (child_relative > directory_size ||
                directory_size - child_relative < 16U) {
                puts(" -> invalid data entry");
                xx_mem_free(name);
                return false;
            }
            data_entry_offset = pe_rva_range_offset(
                context, (uint64_t)root_rva + child_relative, 16U);
            if (data_entry_offset < 0) {
                puts(" -> invalid data entry");
                xx_mem_free(name);
                return false;
            }
            data_rva = xx_io_get_u32(context->device, data_entry_offset,
                                     false);
            data_size = xx_io_get_u32(context->device,
                                      data_entry_offset + 4, false);
            code_page = xx_io_get_u32(context->device,
                                      data_entry_offset + 8, false);
            data_offset = data_rva
                              ? pe_rva_range_offset(
                                    context, data_rva,
                                    data_size ? data_size : 1U)
                              : -1;
            data_address = data_rva
                               ? xx_memory_map_relative_address_to_address(
                                     context->map, (int64_t)data_rva)
                               : XX_INVALID_ADDRESS;
            printf(" -> data rva=0x%08X size=%u codepage=%u",
                   (unsigned)data_rva, (unsigned)data_size,
                   (unsigned)code_page);
            if (data_address != XX_INVALID_ADDRESS)
                printf(" va=0x%llX", (unsigned long long)data_address);
            if (data_offset >= 0)
                printf(" offset=0x%llX", (unsigned long long)data_offset);
            putchar('\n');
            ++*leaves;
            xx_mem_free(name);
        }
    }
    return true;
}

bool xxfc_dump_pe_resources(Abstractformat *format) {
    pe_dump_context context;
    uint32_t directory_rva;
    uint32_t directory_size;
    uint32_t ancestors[PE_MAX_RESOURCE_DEPTH + 1U];
    uint64_t budget = PE_MAX_ITEMS;
    uint64_t leaves = 0U;
    if (!pe_context_init(&context, format)) {
        fprintf(stderr, "Detailed resources require a valid PE file.\n");
        return false;
    }
    pe_print_directory(&context, "Resource", PE_DIRECTORY_RESOURCE,
                       "Resource leaves",
                       xx_format_get_number_of_resources(format, NULL));
    directory_rva = context.pe->data_directory_rva[PE_DIRECTORY_RESOURCE];
    directory_size = context.pe->data_directory_size[PE_DIRECTORY_RESOURCE];
    if (directory_rva == 0U || directory_size < 16U) return true;
    if (!pe_dump_resource_directory(&context, directory_rva,
                                    directory_size, 0U, 0U, ancestors,
                                    &budget, &leaves)) {
        fprintf(stderr, "Invalid or cyclic PE resource tree.\n");
        return false;
    }
    printf("\nTotal resource leaves: %llu\n",
           (unsigned long long)leaves);
    return true;
}
