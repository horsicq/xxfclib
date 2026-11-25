/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/executable/elf/xx_elf_dwarf_analysis.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define DWARF_ANALYSIS_LIMIT UINT32_C(1048576)
#define DWARF_ANALYSIS_STRING_LIMIT (UINT32_C(1024) * UINT32_C(1024))
#define DWARF_ANALYSIS_FUNCTION_BYTES_LIMIT (UINT64_C(16) * UINT64_C(1024) * UINT64_C(1024))

#define DW_TAG_REFERENCE_TYPE UINT64_C(0x10)
#define DW_TAG_SUBROUTINE_TYPE UINT64_C(0x15)
#define DW_TAG_UNSPECIFIED_PARAMETERS UINT64_C(0x18)
#define DW_TAG_CLASS_TYPE UINT64_C(0x02)
#define DW_TAG_RVALUE_REFERENCE_TYPE UINT64_C(0x42)

#define DW_AT_ABSTRACT_ORIGIN UINT64_C(0x31)
#define DW_AT_ARTIFICIAL UINT64_C(0x34)
#define DW_AT_DECLARATION UINT64_C(0x3c)
#define DW_AT_EXTERNAL UINT64_C(0x3f)
#define DW_AT_FRAME_BASE UINT64_C(0x40)
#define DW_AT_SPECIFICATION UINT64_C(0x47)
#define DW_AT_LINKAGE_NAME UINT64_C(0x6e)
#define DW_AT_MIPS_LINKAGE_NAME UINT64_C(0x2007)

#define DW_FORM_ADDR UINT64_C(0x01)

#define DW_OP_ADDR UINT8_C(0x03)
#define DW_OP_REG0 UINT8_C(0x50)
#define DW_OP_REG31 UINT8_C(0x6f)
#define DW_OP_BREG0 UINT8_C(0x70)
#define DW_OP_BREG31 UINT8_C(0x8f)
#define DW_OP_REGX UINT8_C(0x90)
#define DW_OP_FBREG UINT8_C(0x91)
#define DW_OP_BREGX UINT8_C(0x92)
#define DW_OP_STACK_VALUE UINT8_C(0x9f)

typedef struct analysis_registered_type {
    char *name;
    xx_elf_dwarf_registered_type_kind_t kind;
} analysis_registered_type;

typedef struct analysis_override {
    uint64_t address;
    char *name;
    char *prototype;
} analysis_override;

typedef struct analysis_builder {
    char *data;
    size_t length;
    size_t capacity;
} analysis_builder;

struct xx_elf_dwarf_analysis {
    xx_elf_dwarf_analysis_type *types;
    uint32_t type_count;
    uint32_t type_capacity;
    xx_elf_dwarf_analysis_function *functions;
    uint32_t function_count;
    uint32_t function_capacity;
    xx_elf_dwarf_analysis_parameter *parameters;
    uint32_t parameter_count;
    uint32_t parameter_capacity;
    xx_elf_dwarf_analysis_variable *variables;
    uint32_t variable_count;
    uint32_t variable_capacity;
    xx_elf_dwarf_analysis_access *accesses;
    uint32_t access_count;
    uint32_t access_capacity;
    analysis_registered_type *registered_types;
    uint32_t registered_type_count;
    uint32_t registered_type_capacity;
    analysis_override *overrides;
    uint32_t override_count;
    uint32_t override_capacity;
};

static char *analysis_strdup(const char *value)
{
    size_t length;
    char *copy;
    if (!value) return NULL;
    length = strlen(value);
    if (length >= DWARF_ANALYSIS_STRING_LIMIT) return NULL;
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

static bool analysis_grow(void **pointer, uint32_t *capacity, uint32_t needed, size_t element_size)
{
    uint32_t next;
    void *grown;
    if (!pointer || !capacity || element_size == 0U || needed > DWARF_ANALYSIS_LIMIT) return false;
    if (needed <= *capacity) return true;
    next = *capacity ? *capacity : 16U;
    while (next < needed) {
        if (next > DWARF_ANALYSIS_LIMIT / 2U) {
            next = DWARF_ANALYSIS_LIMIT;
            break;
        }
        next *= 2U;
    }
    if ((size_t)next > SIZE_MAX / element_size) return false;
    grown = xx_mem_realloc(*pointer, (size_t)next * element_size);
    if (!grown) return false;
    *pointer = grown;
    *capacity = next;
    return true;
}

static bool builder_reserve(analysis_builder *builder, size_t extra)
{
    size_t needed;
    size_t next;
    char *grown;
    if (!builder || extra >= DWARF_ANALYSIS_STRING_LIMIT || builder->length > DWARF_ANALYSIS_STRING_LIMIT - extra - 1U) return false;
    needed = builder->length + extra + 1U;
    if (needed <= builder->capacity) return true;
    next = builder->capacity ? builder->capacity : 64U;
    while (next < needed) {
        if (next > DWARF_ANALYSIS_STRING_LIMIT / 2U) {
            next = DWARF_ANALYSIS_STRING_LIMIT;
            break;
        }
        next *= 2U;
    }
    if (next < needed) return false;
    grown = (char *)xx_mem_realloc(builder->data, next);
    if (!grown) return false;
    builder->data = grown;
    builder->capacity = next;
    return true;
}

static bool builder_append_n(analysis_builder *builder, const char *value, size_t length)
{
    if (!builder || (!value && length != 0U) || !builder_reserve(builder, length)) return false;
    if (length) memcpy(builder->data + builder->length, value, length);
    builder->length += length;
    builder->data[builder->length] = '\0';
    return true;
}

static bool builder_append(analysis_builder *builder, const char *value)
{
    return value && builder_append_n(builder, value, strlen(value));
}

static char *builder_take(analysis_builder *builder)
{
    char *result;
    if (!builder) return NULL;
    if (!builder->data) {
        result = analysis_strdup("");
    } else {
        result = builder->data;
    }
    builder->data = NULL;
    builder->length = 0U;
    builder->capacity = 0U;
    return result;
}

static void builder_discard(analysis_builder *builder)
{
    if (!builder) return;
    xx_mem_free(builder->data);
    memset(builder, 0, sizeof(*builder));
}

static const xx_elf_dwarf_die *analysis_find_die(const xx_elf_dwarf *dwarf, uint64_t offset, uint64_t *index_out)
{
    uint64_t low = 0U;
    uint64_t high = xx_elf_dwarf_get_number_of_dies(dwarf);
    while (low < high) {
        uint64_t middle = low + (high - low) / 2U;
        const xx_elf_dwarf_die *die = xx_elf_dwarf_get_die(dwarf, middle);
        if (!die) return NULL;
        if (die->offset < offset) low = middle + 1U;
        else high = middle;
    }
    if (low < xx_elf_dwarf_get_number_of_dies(dwarf)) {
        const xx_elf_dwarf_die *die = xx_elf_dwarf_get_die(dwarf, low);
        if (die && die->offset == offset) {
            if (index_out) *index_out = low;
            return die;
        }
    }
    return NULL;
}

static const xx_elf_dwarf_attribute *analysis_effective_attribute(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, uint64_t name, unsigned depth)
{
    const xx_elf_dwarf_attribute *attribute;
    const xx_elf_dwarf_attribute *link;
    const xx_elf_dwarf_die *linked;
    if (!dwarf || !die || depth > 16U) return NULL;
    attribute = xx_elf_dwarf_find_die_attribute(dwarf, die, name);
    if (attribute) return attribute;
    link = xx_elf_dwarf_find_die_attribute(dwarf, die, DW_AT_SPECIFICATION);
    if (link && link->kind == XX_ELF_DWARF_VALUE_REFERENCE) {
        linked = analysis_find_die(dwarf, link->unsigned_value, NULL);
        attribute = analysis_effective_attribute(dwarf, linked, name, depth + 1U);
        if (attribute) return attribute;
    }
    link = xx_elf_dwarf_find_die_attribute(dwarf, die, DW_AT_ABSTRACT_ORIGIN);
    if (link && link->kind == XX_ELF_DWARF_VALUE_REFERENCE) {
        linked = analysis_find_die(dwarf, link->unsigned_value, NULL);
        attribute = analysis_effective_attribute(dwarf, linked, name, depth + 1U);
        if (attribute) return attribute;
    }
    return NULL;
}

static const xx_elf_dwarf_die *analysis_effective_source(const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, unsigned depth)
{
    const xx_elf_dwarf_attribute *link;
    const xx_elf_dwarf_die *linked;
    if (!dwarf || !die || depth > 16U) return die;
    link = xx_elf_dwarf_find_die_attribute(dwarf, die, DW_AT_SPECIFICATION);
    if (!link) link = xx_elf_dwarf_find_die_attribute(dwarf, die, DW_AT_ABSTRACT_ORIGIN);
    if (!link || link->kind != XX_ELF_DWARF_VALUE_REFERENCE) return die;
    linked = analysis_find_die(dwarf, link->unsigned_value, NULL);
    return linked ? analysis_effective_source(dwarf, linked, depth + 1U) : die;
}

static bool analysis_attribute_u64(const xx_elf_dwarf_attribute *attribute, uint64_t *value)
{
    if (!attribute || !value) return false;
    if (attribute->kind == XX_ELF_DWARF_VALUE_UNSIGNED || attribute->kind == XX_ELF_DWARF_VALUE_REFERENCE || attribute->kind == XX_ELF_DWARF_VALUE_ADDRESS_INDEX ||
        attribute->kind == XX_ELF_DWARF_VALUE_LOCLIST_INDEX || attribute->kind == XX_ELF_DWARF_VALUE_RNGLIST_INDEX) {
        *value = attribute->unsigned_value;
        return true;
    }
    if (attribute->kind == XX_ELF_DWARF_VALUE_SIGNED && attribute->signed_value >= 0) {
        *value = (uint64_t)attribute->signed_value;
        return true;
    }
    return false;
}

static char *analysis_join(const char *left, const char *right)
{
    analysis_builder builder = {0};
    if (!builder_append(&builder, left) || !builder_append(&builder, right)) {
        builder_discard(&builder);
        return NULL;
    }
    return builder_take(&builder);
}

static char *analysis_render_type(const xx_elf_dwarf *dwarf, uint64_t offset, unsigned depth)
{
    const xx_elf_dwarf_die *die;
    const xx_elf_dwarf_attribute *name;
    const xx_elf_dwarf_attribute *type;
    char *base;
    const char *suffix;
    if (!dwarf || depth > 32U) return analysis_strdup("unknown");
    die = analysis_find_die(dwarf, offset, NULL);
    if (!die) return analysis_strdup("unknown");
    name = analysis_effective_attribute(dwarf, die, XX_DW_AT_NAME, 0U);
    if (name && name->string_value && name->string_value[0]) return analysis_strdup(name->string_value);
    type = analysis_effective_attribute(dwarf, die, XX_DW_AT_TYPE, 0U);
    if (!type || type->kind != XX_ELF_DWARF_VALUE_REFERENCE) {
        if (die->tag == XX_DW_TAG_POINTER_TYPE) return analysis_strdup("void *");
        return analysis_strdup("void");
    }
    base = analysis_render_type(dwarf, type->unsigned_value, depth + 1U);
    if (!base) return NULL;
    switch (die->tag) {
        case XX_DW_TAG_POINTER_TYPE: suffix = " *"; break;
        case DW_TAG_REFERENCE_TYPE: suffix = " &"; break;
        case DW_TAG_RVALUE_REFERENCE_TYPE: suffix = " &&"; break;
        case XX_DW_TAG_CONST_TYPE: suffix = " const"; break;
        case XX_DW_TAG_ARRAY_TYPE: suffix = "[]"; break;
        default: return base;
    }
    {
        char *result = analysis_join(base, suffix);
        xx_mem_free(base);
        return result;
    }
}

static void analysis_free_discovered(xx_elf_dwarf_analysis *analysis)
{
    uint32_t index;
    if (!analysis) return;
    for (index = 0U; index < analysis->type_count; ++index) xx_mem_free((void *)analysis->types[index].name);
    for (index = 0U; index < analysis->function_count; ++index) {
        xx_mem_free((void *)analysis->functions[index].name);
        xx_mem_free((void *)analysis->functions[index].typed_name);
        xx_mem_free((void *)analysis->functions[index].linkage_name);
        xx_mem_free((void *)analysis->functions[index].return_type);
        xx_mem_free((void *)analysis->functions[index].prototype);
    }
    for (index = 0U; index < analysis->parameter_count; ++index) {
        xx_mem_free((void *)analysis->parameters[index].name);
        xx_mem_free((void *)analysis->parameters[index].type_name);
    }
    for (index = 0U; index < analysis->variable_count; ++index) {
        xx_mem_free((void *)analysis->variables[index].name);
        xx_mem_free((void *)analysis->variables[index].type_name);
    }
    xx_mem_free(analysis->types);
    xx_mem_free(analysis->functions);
    xx_mem_free(analysis->parameters);
    xx_mem_free(analysis->variables);
    xx_mem_free(analysis->accesses);
    analysis->types = NULL;
    analysis->functions = NULL;
    analysis->parameters = NULL;
    analysis->variables = NULL;
    analysis->accesses = NULL;
    analysis->type_count = analysis->type_capacity = 0U;
    analysis->function_count = analysis->function_capacity = 0U;
    analysis->parameter_count = analysis->parameter_capacity = 0U;
    analysis->variable_count = analysis->variable_capacity = 0U;
    analysis->access_count = analysis->access_capacity = 0U;
}

static bool analysis_type_tag(uint64_t tag)
{
    return tag == XX_DW_TAG_BASE_TYPE || tag == XX_DW_TAG_STRUCTURE_TYPE || tag == DW_TAG_CLASS_TYPE || tag == XX_DW_TAG_UNION_TYPE || tag == XX_DW_TAG_TYPEDEF;
}

static bool analysis_collision_type_tag(uint64_t tag)
{
    return tag == XX_DW_TAG_STRUCTURE_TYPE || tag == DW_TAG_CLASS_TYPE || tag == XX_DW_TAG_UNION_TYPE;
}

static bool analysis_collect_types(xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf *dwarf, xx_pd_struct *pd)
{
    uint64_t count = xx_elf_dwarf_get_number_of_dies(dwarf);
    uint64_t index;
    for (index = 0U; index < count; ++index) {
        const xx_elf_dwarf_die *die = xx_elf_dwarf_get_die(dwarf, index);
        const xx_elf_dwarf_attribute *name;
        const xx_elf_dwarf_attribute *size;
        xx_elf_dwarf_analysis_type *record;
        if ((index & UINT64_C(0xfff)) == 0U && xx_pd_is_stopped(pd)) return false;
        if (!die || !analysis_type_tag(die->tag)) continue;
        name = analysis_effective_attribute(dwarf, die, XX_DW_AT_NAME, 0U);
        if (!name || !name->string_value || !name->string_value[0]) continue;
        if (!analysis_grow((void **)&analysis->types, &analysis->type_capacity, analysis->type_count + 1U, sizeof(*analysis->types))) return false;
        record = &analysis->types[analysis->type_count];
        memset(record, 0, sizeof(*record));
        ++analysis->type_count;
        record->die_offset = die->offset;
        record->tag = die->tag;
        record->name = analysis_strdup(name->string_value);
        if (!record->name) return false;
        size = analysis_effective_attribute(dwarf, die, XX_DW_AT_BYTE_SIZE, 0U);
        (void)analysis_attribute_u64(size, &record->byte_size);
    }
    return true;
}

static bool analysis_read_uleb(const uint8_t *data, size_t size, size_t *position, uint64_t *value)
{
    uint64_t result = 0U;
    unsigned shift = 0U;
    unsigned index;
    if (!data || !position || !value) return false;
    for (index = 0U; index < 10U && *position < size; ++index) {
        uint8_t byte = data[(*position)++];
        if (shift == 63U && (byte & UINT8_C(0x7e)) != 0U) return false;
        result |= (uint64_t)(byte & UINT8_C(0x7f)) << shift;
        if ((byte & UINT8_C(0x80)) == 0U) {
            *value = result;
            return true;
        }
        shift += 7U;
    }
    return false;
}

static bool analysis_read_sleb(const uint8_t *data, size_t size, size_t *position, int64_t *value)
{
    uint64_t result = 0U;
    unsigned shift = 0U;
    unsigned index;
    uint8_t byte = 0U;
    if (!data || !position || !value) return false;
    for (index = 0U; index < 10U && *position < size; ++index) {
        byte = data[(*position)++];
        if (shift == 63U && (byte & UINT8_C(0x7e)) != 0U && (byte & UINT8_C(0x7f)) != UINT8_C(0x7f)) return false;
        result |= (uint64_t)(byte & UINT8_C(0x7f)) << shift;
        shift += 7U;
        if ((byte & UINT8_C(0x80)) == 0U) {
            if (shift < 64U && (byte & UINT8_C(0x40)) != 0U) result |= UINT64_MAX << shift;
            *value = (int64_t)result;
            return true;
        }
    }
    return false;
}

static const char *analysis_register_name(uint16_t machine, uint32_t number)
{
    static const char *const aarch64[32] = {"x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
                                            "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "sp"};
    if (machine == UINT16_C(183) && number < 32U) return aarch64[number];
    return NULL;
}

static bool analysis_decode_location(const uint8_t *expression, size_t size, uint8_t address_size, bool big_endian, const xx_elf_dwarf_analysis_variable *frame,
                                     xx_elf_dwarf_analysis_variable *output)
{
    size_t position = 0U;
    uint8_t operation;
    uint64_t number = 0U;
    int64_t displacement = 0;
    unsigned index;
    if (!expression || !size || !output) return false;
    operation = expression[position++];
    if (operation >= DW_OP_REG0 && operation <= DW_OP_REG31) {
        output->location_kind = XX_ELF_DWARF_LOCATION_REGISTER;
        output->register_number = operation - DW_OP_REG0;
    } else if (operation >= DW_OP_BREG0 && operation <= DW_OP_BREG31) {
        if (!analysis_read_sleb(expression, size, &position, &displacement)) return false;
        output->location_kind = XX_ELF_DWARF_LOCATION_REGISTER_OFFSET;
        output->register_number = operation - DW_OP_BREG0;
        output->offset = displacement;
    } else if (operation == DW_OP_REGX) {
        if (!analysis_read_uleb(expression, size, &position, &number) || number > UINT32_MAX) return false;
        output->location_kind = XX_ELF_DWARF_LOCATION_REGISTER;
        output->register_number = (uint32_t)number;
    } else if (operation == DW_OP_BREGX) {
        if (!analysis_read_uleb(expression, size, &position, &number) || number > UINT32_MAX || !analysis_read_sleb(expression, size, &position, &displacement))
            return false;
        output->location_kind = XX_ELF_DWARF_LOCATION_REGISTER_OFFSET;
        output->register_number = (uint32_t)number;
        output->offset = displacement;
    } else if (operation == DW_OP_FBREG) {
        if (!analysis_read_sleb(expression, size, &position, &displacement)) return false;
        if (frame && (frame->location_kind == XX_ELF_DWARF_LOCATION_REGISTER || frame->location_kind == XX_ELF_DWARF_LOCATION_REGISTER_OFFSET)) {
            if ((displacement > 0 && frame->offset > INT64_MAX - displacement) || (displacement < 0 && frame->offset < INT64_MIN - displacement)) return false;
            output->location_kind = XX_ELF_DWARF_LOCATION_REGISTER_OFFSET;
            output->register_number = frame->register_number;
            output->offset = frame->offset + displacement;
        } else {
            output->location_kind = XX_ELF_DWARF_LOCATION_FRAME_BASE;
            output->offset = displacement;
        }
    } else if (operation == DW_OP_ADDR) {
        if (address_size == 0U || address_size > 8U || size - position < address_size) return false;
        for (index = 0U; index < address_size; ++index) {
            unsigned shift_index = big_endian ? index : address_size - index - 1U;
            number = (number << 8) | expression[position + shift_index];
        }
        position += address_size;
        output->location_kind = XX_ELF_DWARF_LOCATION_ADDRESS;
        output->address = number;
    } else {
        return false;
    }
    if (position < size && expression[position] == DW_OP_STACK_VALUE) ++position;
    return position == size;
}

static bool analysis_add_parameter(xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, uint32_t function_index)
{
    const xx_elf_dwarf_attribute *name = analysis_effective_attribute(dwarf, die, XX_DW_AT_NAME, 0U);
    const xx_elf_dwarf_attribute *type = analysis_effective_attribute(dwarf, die, XX_DW_AT_TYPE, 0U);
    const xx_elf_dwarf_attribute *artificial = analysis_effective_attribute(dwarf, die, DW_AT_ARTIFICIAL, 0U);
    xx_elf_dwarf_analysis_parameter *parameter;
    uint64_t flag = 0U;
    if (!analysis_grow((void **)&analysis->parameters, &analysis->parameter_capacity, analysis->parameter_count + 1U, sizeof(*analysis->parameters))) return false;
    parameter = &analysis->parameters[analysis->parameter_count];
    memset(parameter, 0, sizeof(*parameter));
    ++analysis->parameter_count;
    parameter->die_offset = die->offset;
    parameter->function_index = function_index;
    parameter->name = analysis_strdup(name && name->string_value ? name->string_value : "");
    if (!parameter->name) return false;
    parameter->type_name = type && type->kind == XX_ELF_DWARF_VALUE_REFERENCE ? analysis_render_type(dwarf, type->unsigned_value, 0U) : analysis_strdup("unknown");
    if (!parameter->type_name) return false;
    parameter->artificial = analysis_attribute_u64(artificial, &flag) && flag != 0U;
    return true;
}

static bool analysis_add_variable(xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, const xx_elf_dwarf_unit *unit,
                                  uint16_t machine, bool big_endian, uint32_t function_index, const xx_elf_dwarf_analysis_variable *frame)
{
    const xx_elf_dwarf_attribute *name = analysis_effective_attribute(dwarf, die, XX_DW_AT_NAME, 0U);
    const xx_elf_dwarf_attribute *type = analysis_effective_attribute(dwarf, die, XX_DW_AT_TYPE, 0U);
    const xx_elf_dwarf_attribute *location = analysis_effective_attribute(dwarf, die, XX_DW_AT_LOCATION, 0U);
    const uint8_t *expression;
    xx_elf_dwarf_analysis_variable parsed;
    if (!name || !name->string_value || !location || location->kind != XX_ELF_DWARF_VALUE_BLOCK || !unit) return true;
    expression = xx_elf_dwarf_get_attribute_block(dwarf, location);
    if (!expression) return true;
    memset(&parsed, 0, sizeof(parsed));
    if (!analysis_decode_location(expression, (size_t)location->data_size, unit->address_size, big_endian, frame, &parsed)) return true;
    if (!analysis_grow((void **)&analysis->variables, &analysis->variable_capacity, analysis->variable_count + 1U, sizeof(*analysis->variables))) return false;
    parsed.die_offset = die->offset;
    parsed.function_index = function_index;
    parsed.name = analysis_strdup(name->string_value);
    if (!parsed.name) return false;
    parsed.type_name = type && type->kind == XX_ELF_DWARF_VALUE_REFERENCE ? analysis_render_type(dwarf, type->unsigned_value, 0U) : analysis_strdup("unknown");
    if (!parsed.type_name) {
        xx_mem_free((void *)parsed.name);
        return false;
    }
    parsed.register_name = analysis_register_name(machine, parsed.register_number);
    analysis->variables[analysis->variable_count++] = parsed;
    return true;
}

static bool analysis_add_function(xx_elf_dwarf_analysis *analysis, const xx_elf *elf, const xx_elf_dwarf *dwarf, const xx_elf_dwarf_die *die, uint64_t die_index)
{
    const xx_elf_dwarf_attribute *low = xx_elf_dwarf_find_die_attribute(dwarf, die, XX_DW_AT_LOW_PC);
    const xx_elf_dwarf_attribute *high = xx_elf_dwarf_find_die_attribute(dwarf, die, XX_DW_AT_HIGH_PC);
    const xx_elf_dwarf_attribute *name;
    const xx_elf_dwarf_attribute *linkage;
    const xx_elf_dwarf_attribute *type;
    const xx_elf_dwarf_attribute *frame_attribute;
    const xx_elf_dwarf_unit *unit;
    const uint8_t *frame_expression;
    xx_elf_dwarf_analysis_variable frame;
    xx_elf_dwarf_analysis_function *function;
    uint64_t address;
    uint64_t high_value = 0U;
    uint64_t count;
    uint64_t index;
    uint32_t function_index;
    if (!analysis_attribute_u64(low, &address)) return true;
    name = analysis_effective_attribute(dwarf, die, XX_DW_AT_NAME, 0U);
    if (!name || !name->string_value || !name->string_value[0]) return true;
    if (!analysis_grow((void **)&analysis->functions, &analysis->function_capacity, analysis->function_count + 1U, sizeof(*analysis->functions))) return false;
    function_index = analysis->function_count;
    function = &analysis->functions[analysis->function_count];
    memset(function, 0, sizeof(*function));
    ++analysis->function_count;
    function->die_offset = die->offset;
    function->address = address;
    function->parameter_index = analysis->parameter_count;
    function->name = analysis_strdup(name->string_value);
    if (!function->name) return false;
    linkage = analysis_effective_attribute(dwarf, die, DW_AT_LINKAGE_NAME, 0U);
    if (!linkage) linkage = analysis_effective_attribute(dwarf, die, DW_AT_MIPS_LINKAGE_NAME, 0U);
    function->linkage_name = analysis_strdup(linkage && linkage->string_value ? linkage->string_value : "");
    if (!function->linkage_name) return false;
    type = analysis_effective_attribute(dwarf, die, XX_DW_AT_TYPE, 0U);
    function->return_type = type && type->kind == XX_ELF_DWARF_VALUE_REFERENCE ? analysis_render_type(dwarf, type->unsigned_value, 0U) : analysis_strdup("void");
    if (!function->return_type) return false;
    if (analysis_attribute_u64(high, &high_value)) {
        if (high->form == DW_FORM_ADDR || high->kind == XX_ELF_DWARF_VALUE_ADDRESS_INDEX) {
            if (high_value >= address) function->size = high_value - address;
        } else if (high_value <= UINT64_MAX - address) {
            function->size = high_value;
        }
    }
    memset(&frame, 0, sizeof(frame));
    unit = xx_elf_dwarf_get_unit(dwarf, die->unit_index);
    frame_attribute = analysis_effective_attribute(dwarf, die, DW_AT_FRAME_BASE, 0U);
    frame_expression = frame_attribute && frame_attribute->kind == XX_ELF_DWARF_VALUE_BLOCK ? xx_elf_dwarf_get_attribute_block(dwarf, frame_attribute) : NULL;
    if (frame_expression && unit) {
        (void)analysis_decode_location(frame_expression, (size_t)frame_attribute->data_size, unit->address_size, elf->data_encoding == XX_ELF_DATA_MSB, NULL, &frame);
    }

    count = xx_elf_dwarf_get_number_of_dies(dwarf);
    for (index = die_index + 1U; index < count; ++index) {
        const xx_elf_dwarf_die *child = xx_elf_dwarf_get_die(dwarf, index);
        if (!child || child->depth <= die->depth) break;
        if (child->depth != die->depth + 1U) continue;
        if (child->tag == XX_DW_TAG_FORMAL_PARAMETER) {
            if (!analysis_add_parameter(analysis, dwarf, child, function_index)) return false;
        } else if (child->tag == XX_DW_TAG_VARIABLE) {
            if (!analysis_add_variable(analysis, dwarf, child, unit, elf->machine, elf->data_encoding == XX_ELF_DATA_MSB, function_index, &frame)) return false;
        }
    }
    function = &analysis->functions[function_index];
    function->parameter_count = analysis->parameter_count - function->parameter_index;
    if (function->parameter_count == 0U) {
        const xx_elf_dwarf_die *source = analysis_effective_source(dwarf, die, 0U);
        uint64_t source_index;
        if (source && source != die && analysis_find_die(dwarf, source->offset, &source_index)) {
            for (index = source_index + 1U; index < count; ++index) {
                const xx_elf_dwarf_die *child = xx_elf_dwarf_get_die(dwarf, index);
                if (!child || child->depth <= source->depth) break;
                if (child->depth == source->depth + 1U && child->tag == XX_DW_TAG_FORMAL_PARAMETER && !analysis_add_parameter(analysis, dwarf, child, function_index))
                    return false;
            }
            function = &analysis->functions[function_index];
            function->parameter_count = analysis->parameter_count - function->parameter_index;
        }
    }
    return true;
}

static bool analysis_name_collides(const xx_elf_dwarf_analysis *analysis, const char *name)
{
    uint32_t index;
    if (!analysis || !name) return false;
    for (index = 0U; index < analysis->type_count; ++index) {
        if (analysis_collision_type_tag(analysis->types[index].tag) && strcmp(analysis->types[index].name, name) == 0) return true;
    }
    for (index = 0U; index < analysis->registered_type_count; ++index) {
        if (analysis->registered_types[index].kind == XX_ELF_DWARF_REGISTERED_DATA_TYPE && strcmp(analysis->registered_types[index].name, name) == 0) return true;
    }
    return false;
}

static char *analysis_typed_name(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function)
{
    analysis_builder builder = {0};
    const unsigned char *source;
    bool previous_separator = false;
    char address[32];
    int written;
    if (!analysis || !function || !function->name) return NULL;
    source = (const unsigned char *)function->name;
    while (*source) {
        char output;
        bool separator;
        if ((*source >= 'a' && *source <= 'z') || (*source >= 'A' && *source <= 'Z') || (*source >= '0' && *source <= '9') || *source == '_') {
            output = (char)*source;
            separator = false;
        } else {
            output = '_';
            separator = true;
        }
        if ((!separator || !previous_separator) && !builder_append_n(&builder, &output, 1U)) {
            builder_discard(&builder);
            return NULL;
        }
        previous_separator = separator;
        ++source;
    }
    while (builder.length > 1U && builder.data[builder.length - 1U] == '_') builder.data[--builder.length] = '\0';
    if (analysis_name_collides(analysis, function->name)) {
        written = snprintf(address, sizeof(address), "_%llx", (unsigned long long)function->address);
        if (written <= 0 || (size_t)written >= sizeof(address) || !builder_append_n(&builder, address, (size_t)written)) {
            builder_discard(&builder);
            return NULL;
        }
    }
    return builder_take(&builder);
}

static char *analysis_prototype(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function)
{
    analysis_builder builder = {0};
    uint32_t index;
    if (!analysis || !function || !function->return_type || !function->typed_name || !builder_append(&builder, function->return_type) || !builder_append(&builder, " ") ||
        !builder_append(&builder, function->typed_name) || !builder_append(&builder, " (")) {
        builder_discard(&builder);
        return NULL;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        const xx_elf_dwarf_analysis_parameter *parameter = &analysis->parameters[function->parameter_index + index];
        if ((index && !builder_append(&builder, ", ")) || !builder_append(&builder, parameter->type_name) ||
            (parameter->name[0] && (!builder_append(&builder, " ") || !builder_append(&builder, parameter->name)))) {
            builder_discard(&builder);
            return NULL;
        }
    }
    if (!builder_append(&builder, ");")) {
        builder_discard(&builder);
        return NULL;
    }
    return builder_take(&builder);
}

static bool analysis_rebuild_names(xx_elf_dwarf_analysis *analysis)
{
    uint32_t index;
    if (!analysis) return false;
    for (index = 0U; index < analysis->function_count; ++index) {
        xx_elf_dwarf_analysis_function *function = &analysis->functions[index];
        char *typed;
        char *prototype;
        xx_mem_free((void *)function->typed_name);
        xx_mem_free((void *)function->prototype);
        function->typed_name = NULL;
        function->prototype = NULL;
        typed = analysis_typed_name(analysis, function);
        if (!typed) return false;
        function->typed_name = typed;
        prototype = analysis_prototype(analysis, function);
        if (!prototype) return false;
        function->prototype = prototype;
    }
    return true;
}

static bool analysis_add_access(xx_elf_dwarf_analysis *analysis, uint64_t address, uint32_t variable_index, xx_elf_dwarf_access_kind_t kind)
{
    xx_elf_dwarf_analysis_access *record;
    uint32_t index;
    for (index = 0U; index < analysis->access_count; ++index) {
        record = &analysis->accesses[index];
        if (record->address == address && record->variable_index == variable_index && record->kind == kind) return true;
    }
    if (!analysis_grow((void **)&analysis->accesses, &analysis->access_capacity, analysis->access_count + 1U, sizeof(*analysis->accesses))) return false;
    record = &analysis->accesses[analysis->access_count++];
    record->address = address;
    record->variable_index = variable_index;
    record->kind = kind;
    return true;
}

static bool analysis_match_memory_access(xx_elf_dwarf_analysis *analysis, uint32_t function_index, uint32_t base_register, int64_t offset, uint64_t address,
                                         xx_elf_dwarf_access_kind_t kind)
{
    uint32_t index;
    for (index = 0U; index < analysis->variable_count; ++index) {
        const xx_elf_dwarf_analysis_variable *variable = &analysis->variables[index];
        if (variable->function_index == function_index && variable->location_kind == XX_ELF_DWARF_LOCATION_REGISTER_OFFSET &&
            variable->register_number == base_register && variable->offset == offset && !analysis_add_access(analysis, address, index, kind))
            return false;
    }
    return true;
}

static uint32_t analysis_u32(const uint8_t *bytes, bool big_endian)
{
    if (big_endian) {
        return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    }
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int64_t analysis_sign_extend(uint64_t value, unsigned width)
{
    uint64_t sign = UINT64_C(1) << (width - 1U);
    return (int64_t)((value ^ sign) - sign);
}

static bool analysis_scan_aarch64_function(xx_elf_dwarf_analysis *analysis, const xx_elf *elf, uint32_t function_index)
{
    const xx_elf_dwarf_analysis_function *function = &analysis->functions[function_index];
    const xx_elf_section_header *section = NULL;
    uint64_t section_index;
    uint64_t relative;
    uint64_t available;
    uint64_t size;
    uint64_t absolute;
    uint8_t *bytes;
    uint64_t at;
    bool big_endian = elf->data_encoding == XX_ELF_DATA_MSB;
    if (function->size == 0U || function->size > DWARF_ANALYSIS_FUNCTION_BYTES_LIMIT || elf->format.base_address < 0) return true;
    for (section_index = 0U; section_index < elf->section_header_count; ++section_index) {
        const xx_elf_section_header *candidate = &elf->section_headers[section_index];
        uint64_t end;
        if (candidate->type == XX_ELF_SECTION_NOBITS || candidate->size == 0U || candidate->address > UINT64_MAX - candidate->size) continue;
        end = candidate->address + candidate->size;
        if (function->address >= candidate->address && function->address < end) {
            section = candidate;
            break;
        }
    }
    if (!section) return true;
    relative = function->address - section->address;
    available = section->size - relative;
    size = function->size < available ? function->size : available;
    if (size < 4U || size > SIZE_MAX || section->offset > UINT64_MAX - relative) return true;
    absolute = (uint64_t)elf->format.base_address;
    if (section->offset > UINT64_MAX - absolute) return false;
    absolute += section->offset;
    if (relative > UINT64_MAX - absolute) return false;
    absolute += relative;
    if (absolute > INT64_MAX) return false;
    bytes = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!bytes) return false;
    if (!xx_io_read_at(elf->format.device, (int64_t)absolute, bytes, (size_t)size)) {
        xx_mem_free(bytes);
        return false;
    }
    for (at = 0U; at + 4U <= size; at += 4U) {
        uint32_t instruction = analysis_u32(bytes + at, big_endian);
        uint32_t base_register;
        int64_t offset;
        xx_elf_dwarf_access_kind_t kind;
        bool matched = false;
        if ((instruction & UINT32_C(0x3b000000)) == UINT32_C(0x39000000)) {
            unsigned scale = instruction >> 30;
            uint32_t immediate = (instruction >> 10) & UINT32_C(0xfff);
            base_register = (instruction >> 5) & UINT32_C(0x1f);
            offset = (int64_t)((uint64_t)immediate << scale);
            kind = (instruction & UINT32_C(0x00400000)) ? XX_ELF_DWARF_ACCESS_READ : XX_ELF_DWARF_ACCESS_WRITE;
            matched = true;
        } else if ((instruction & UINT32_C(0x3b200c00)) == UINT32_C(0x38000000)) {
            uint32_t immediate = (instruction >> 12) & UINT32_C(0x1ff);
            base_register = (instruction >> 5) & UINT32_C(0x1f);
            offset = analysis_sign_extend(immediate, 9U);
            kind = (instruction & UINT32_C(0x00400000)) ? XX_ELF_DWARF_ACCESS_READ : XX_ELF_DWARF_ACCESS_WRITE;
            matched = true;
        } else {
            base_register = 0U;
            offset = 0;
            kind = XX_ELF_DWARF_ACCESS_READ;
        }
        if (matched && !analysis_match_memory_access(analysis, function_index, base_register, offset, function->address + at, kind)) {
            xx_mem_free(bytes);
            return false;
        }
    }
    xx_mem_free(bytes);
    return true;
}

static bool analysis_build(xx_elf_dwarf_analysis *analysis, const xx_elf *elf, const xx_elf_dwarf *dwarf, xx_pd_struct *pd)
{
    uint64_t count;
    uint64_t index;
    if (!analysis || !elf || !dwarf || !elf->section_headers) return false;
    if (!analysis_collect_types(analysis, dwarf, pd)) return false;
    count = xx_elf_dwarf_get_number_of_dies(dwarf);
    for (index = 0U; index < count; ++index) {
        const xx_elf_dwarf_die *die = xx_elf_dwarf_get_die(dwarf, index);
        if ((index & UINT64_C(0xfff)) == 0U && xx_pd_is_stopped(pd)) return false;
        if (die && die->tag == XX_DW_TAG_SUBPROGRAM && !analysis_add_function(analysis, elf, dwarf, die, index)) return false;
    }
    if (!analysis_rebuild_names(analysis)) return false;
    if (elf->machine == UINT16_C(183)) {
        for (index = 0U; index < analysis->function_count; ++index) {
            if (!analysis_scan_aarch64_function(analysis, elf, (uint32_t)index)) return false;
        }
    }
    return true;
}

xx_elf_dwarf_analysis *xx_elf_dwarf_analysis_create(const xx_elf *elf, const xx_elf_dwarf *dwarf, xx_pd_struct *pd)
{
    xx_elf_dwarf_analysis *analysis = (xx_elf_dwarf_analysis *)xx_mem_calloc(1U, sizeof(*analysis));
    if (!analysis) return NULL;
    if (!analysis_build(analysis, elf, dwarf, pd)) {
        xx_elf_dwarf_analysis_free(analysis);
        return NULL;
    }
    return analysis;
}

void xx_elf_dwarf_analysis_free(xx_elf_dwarf_analysis *analysis)
{
    uint32_t index;
    if (!analysis) return;
    analysis_free_discovered(analysis);
    for (index = 0U; index < analysis->registered_type_count; ++index) xx_mem_free(analysis->registered_types[index].name);
    for (index = 0U; index < analysis->override_count; ++index) {
        xx_mem_free(analysis->overrides[index].name);
        xx_mem_free(analysis->overrides[index].prototype);
    }
    xx_mem_free(analysis->registered_types);
    xx_mem_free(analysis->overrides);
    xx_mem_free(analysis);
}

bool xx_elf_dwarf_analysis_reimport(xx_elf_dwarf_analysis *analysis, const xx_elf *elf, const xx_elf_dwarf *dwarf, xx_pd_struct *pd)
{
    if (!analysis || !elf || !dwarf) return false;
    analysis_free_discovered(analysis);
    if (!analysis_build(analysis, elf, dwarf, pd)) {
        analysis_free_discovered(analysis);
        return false;
    }
    return true;
}

uint64_t xx_elf_dwarf_analysis_get_number_of_types(const xx_elf_dwarf_analysis *analysis)
{
    return analysis ? analysis->type_count : 0U;
}

const xx_elf_dwarf_analysis_type *xx_elf_dwarf_analysis_get_type(const xx_elf_dwarf_analysis *analysis, uint64_t index)
{
    return analysis && index < analysis->type_count ? &analysis->types[index] : NULL;
}

uint64_t xx_elf_dwarf_analysis_get_number_of_functions(const xx_elf_dwarf_analysis *analysis)
{
    return analysis ? analysis->function_count : 0U;
}

const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_get_function(const xx_elf_dwarf_analysis *analysis, uint64_t index)
{
    return analysis && index < analysis->function_count ? &analysis->functions[index] : NULL;
}

const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_find_function_at(const xx_elf_dwarf_analysis *analysis, uint64_t address)
{
    uint32_t index;
    if (!analysis) return NULL;
    for (index = 0U; index < analysis->function_count; ++index) {
        if (analysis->functions[index].address == address) return &analysis->functions[index];
    }
    return NULL;
}

const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_find_function_named(const xx_elf_dwarf_analysis *analysis, const char *name)
{
    uint32_t index;
    if (!analysis || !name) return NULL;
    for (index = 0U; index < analysis->function_count; ++index) {
        if (strcmp(analysis->functions[index].name, name) == 0 || strcmp(analysis->functions[index].typed_name, name) == 0) return &analysis->functions[index];
    }
    return NULL;
}

uint64_t xx_elf_dwarf_analysis_get_number_of_parameters(const xx_elf_dwarf_analysis *analysis)
{
    return analysis ? analysis->parameter_count : 0U;
}

const xx_elf_dwarf_analysis_parameter *xx_elf_dwarf_analysis_get_parameter(const xx_elf_dwarf_analysis *analysis, uint64_t index)
{
    return analysis && index < analysis->parameter_count ? &analysis->parameters[index] : NULL;
}

uint64_t xx_elf_dwarf_analysis_get_number_of_variables(const xx_elf_dwarf_analysis *analysis)
{
    return analysis ? analysis->variable_count : 0U;
}

const xx_elf_dwarf_analysis_variable *xx_elf_dwarf_analysis_get_variable(const xx_elf_dwarf_analysis *analysis, uint64_t index)
{
    return analysis && index < analysis->variable_count ? &analysis->variables[index] : NULL;
}

const xx_elf_dwarf_analysis_variable *xx_elf_dwarf_analysis_find_variable(const xx_elf_dwarf_analysis *analysis, uint32_t function_index, const char *name,
                                                                          uint32_t occurrence)
{
    uint32_t index;
    if (!analysis || !name || function_index >= analysis->function_count) return NULL;
    for (index = 0U; index < analysis->variable_count; ++index) {
        const xx_elf_dwarf_analysis_variable *variable = &analysis->variables[index];
        if (variable->function_index == function_index && strcmp(variable->name, name) == 0) {
            if (occurrence == 0U) return variable;
            --occurrence;
        }
    }
    return NULL;
}

uint64_t xx_elf_dwarf_analysis_get_number_of_accesses(const xx_elf_dwarf_analysis *analysis)
{
    return analysis ? analysis->access_count : 0U;
}

const xx_elf_dwarf_analysis_access *xx_elf_dwarf_analysis_get_access(const xx_elf_dwarf_analysis *analysis, uint64_t index)
{
    return analysis && index < analysis->access_count ? &analysis->accesses[index] : NULL;
}

bool xx_elf_dwarf_analysis_register_type(xx_elf_dwarf_analysis *analysis, const char *name, xx_elf_dwarf_registered_type_kind_t kind)
{
    analysis_registered_type *record;
    uint32_t index;
    if (!analysis || !name || !name[0] || (kind != XX_ELF_DWARF_REGISTERED_DATA_TYPE && kind != XX_ELF_DWARF_REGISTERED_FUNCTION_TYPE)) return false;
    for (index = 0U; index < analysis->registered_type_count; ++index) {
        if (strcmp(analysis->registered_types[index].name, name) == 0) {
            analysis->registered_types[index].kind = kind;
            return analysis_rebuild_names(analysis);
        }
    }
    if (!analysis_grow((void **)&analysis->registered_types, &analysis->registered_type_capacity, analysis->registered_type_count + 1U,
                       sizeof(*analysis->registered_types)))
        return false;
    record = &analysis->registered_types[analysis->registered_type_count];
    record->name = analysis_strdup(name);
    if (!record->name) return false;
    record->kind = kind;
    ++analysis->registered_type_count;
    return analysis_rebuild_names(analysis);
}

bool xx_elf_dwarf_analysis_set_function_override(xx_elf_dwarf_analysis *analysis, uint64_t address, const char *name, const char *prototype)
{
    analysis_override *record = NULL;
    char *name_copy;
    char *prototype_copy;
    uint32_t index;
    if (!analysis || !name || !name[0] || !prototype || !prototype[0]) return false;
    name_copy = analysis_strdup(name);
    prototype_copy = analysis_strdup(prototype);
    if (!name_copy || !prototype_copy) {
        xx_mem_free(name_copy);
        xx_mem_free(prototype_copy);
        return false;
    }
    for (index = 0U; index < analysis->override_count; ++index) {
        if (analysis->overrides[index].address == address) {
            record = &analysis->overrides[index];
            break;
        }
    }
    if (!record) {
        if (!analysis_grow((void **)&analysis->overrides, &analysis->override_capacity, analysis->override_count + 1U, sizeof(*analysis->overrides))) {
            xx_mem_free(name_copy);
            xx_mem_free(prototype_copy);
            return false;
        }
        record = &analysis->overrides[analysis->override_count++];
        memset(record, 0, sizeof(*record));
        record->address = address;
    }
    xx_mem_free(record->name);
    xx_mem_free(record->prototype);
    record->name = name_copy;
    record->prototype = prototype_copy;
    return true;
}

bool xx_elf_dwarf_analysis_clear_function_override(xx_elf_dwarf_analysis *analysis, uint64_t address)
{
    uint32_t index;
    if (!analysis) return false;
    for (index = 0U; index < analysis->override_count; ++index) {
        if (analysis->overrides[index].address == address) {
            xx_mem_free(analysis->overrides[index].name);
            xx_mem_free(analysis->overrides[index].prototype);
            if (index + 1U < analysis->override_count) {
                memmove(&analysis->overrides[index], &analysis->overrides[index + 1U], (size_t)(analysis->override_count - index - 1U) * sizeof(*analysis->overrides));
            }
            --analysis->override_count;
            return true;
        }
    }
    return false;
}

static const analysis_override *analysis_find_override(const xx_elf_dwarf_analysis *analysis, uint64_t address)
{
    uint32_t index;
    if (!analysis) return NULL;
    for (index = 0U; index < analysis->override_count; ++index) {
        if (analysis->overrides[index].address == address) return &analysis->overrides[index];
    }
    return NULL;
}

const char *xx_elf_dwarf_analysis_get_effective_function_name(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function)
{
    const analysis_override *override;
    if (!analysis || !function) return NULL;
    override = analysis_find_override(analysis, function->address);
    return override ? override->name : function->typed_name;
}

const char *xx_elf_dwarf_analysis_get_effective_function_prototype(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function)
{
    const analysis_override *override;
    if (!analysis || !function) return NULL;
    override = analysis_find_override(analysis, function->address);
    return override ? override->prototype : function->prototype;
}
