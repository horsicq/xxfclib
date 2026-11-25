/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ELF_DWARF_ANALYSIS_H
#define XXFCLIB_ELF_DWARF_ANALYSIS_H

#include "xxfclib/formats/executable/elf/xx_elf_dwarf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_elf_dwarf_location_kind_e {
    XX_ELF_DWARF_LOCATION_NONE = 0,
    XX_ELF_DWARF_LOCATION_REGISTER,
    XX_ELF_DWARF_LOCATION_REGISTER_OFFSET,
    XX_ELF_DWARF_LOCATION_FRAME_BASE,
    XX_ELF_DWARF_LOCATION_ADDRESS
} xx_elf_dwarf_location_kind_t;

typedef enum xx_elf_dwarf_access_kind_e {
    XX_ELF_DWARF_ACCESS_READ = 1,
    XX_ELF_DWARF_ACCESS_WRITE = 2
} xx_elf_dwarf_access_kind_t;

typedef enum xx_elf_dwarf_registered_type_kind_e {
    XX_ELF_DWARF_REGISTERED_DATA_TYPE = 0,
    XX_ELF_DWARF_REGISTERED_FUNCTION_TYPE = 1
} xx_elf_dwarf_registered_type_kind_t;

typedef struct xx_elf_dwarf_analysis_type {
    uint64_t die_offset;
    uint64_t tag;
    uint64_t byte_size;
    const char *name;
} xx_elf_dwarf_analysis_type;

typedef struct xx_elf_dwarf_analysis_parameter {
    uint64_t die_offset;
    uint32_t function_index;
    const char *name;
    const char *type_name;
    bool artificial;
} xx_elf_dwarf_analysis_parameter;

typedef struct xx_elf_dwarf_analysis_function {
    uint64_t die_offset;
    uint64_t address;
    uint64_t size;
    uint32_t parameter_index;
    uint32_t parameter_count;
    const char *name;
    const char *typed_name;
    const char *linkage_name;
    const char *return_type;
    const char *prototype;
} xx_elf_dwarf_analysis_function;

typedef struct xx_elf_dwarf_analysis_variable {
    uint64_t die_offset;
    uint32_t function_index;
    uint32_t register_number;
    int64_t offset;
    uint64_t address;
    xx_elf_dwarf_location_kind_t location_kind;
    const char *name;
    const char *type_name;
    const char *register_name;
} xx_elf_dwarf_analysis_variable;

typedef struct xx_elf_dwarf_analysis_access {
    uint64_t address;
    uint32_t variable_index;
    xx_elf_dwarf_access_kind_t kind;
} xx_elf_dwarf_analysis_access;

typedef struct xx_elf_dwarf_analysis xx_elf_dwarf_analysis;

/** Build a stable function/type/variable model from a parsed ELF DWARF image.
 * The analysis copies all exposed strings.  elf and dwarf are only borrowed
 * for this call and may be released after creation. */
XXFC_API xx_elf_dwarf_analysis *xx_elf_dwarf_analysis_create(const xx_elf *elf, const xx_elf_dwarf *dwarf, xx_pd_struct *pd);
XXFC_API void xx_elf_dwarf_analysis_free(xx_elf_dwarf_analysis *analysis);

/** Replace imported DWARF records while preserving registered types and user
 * function overrides.  elf must describe the same image as dwarf. */
XXFC_API bool xx_elf_dwarf_analysis_reimport(xx_elf_dwarf_analysis *analysis, const xx_elf *elf, const xx_elf_dwarf *dwarf, xx_pd_struct *pd);

XXFC_API uint64_t xx_elf_dwarf_analysis_get_number_of_types(const xx_elf_dwarf_analysis *analysis);
XXFC_API const xx_elf_dwarf_analysis_type *xx_elf_dwarf_analysis_get_type(const xx_elf_dwarf_analysis *analysis, uint64_t index);
XXFC_API uint64_t xx_elf_dwarf_analysis_get_number_of_functions(const xx_elf_dwarf_analysis *analysis);
XXFC_API const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_get_function(const xx_elf_dwarf_analysis *analysis, uint64_t index);
XXFC_API const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_find_function_at(const xx_elf_dwarf_analysis *analysis, uint64_t address);
XXFC_API const xx_elf_dwarf_analysis_function *xx_elf_dwarf_analysis_find_function_named(const xx_elf_dwarf_analysis *analysis, const char *name);
XXFC_API uint64_t xx_elf_dwarf_analysis_get_number_of_parameters(const xx_elf_dwarf_analysis *analysis);
XXFC_API const xx_elf_dwarf_analysis_parameter *xx_elf_dwarf_analysis_get_parameter(const xx_elf_dwarf_analysis *analysis, uint64_t index);
XXFC_API uint64_t xx_elf_dwarf_analysis_get_number_of_variables(const xx_elf_dwarf_analysis *analysis);
XXFC_API const xx_elf_dwarf_analysis_variable *xx_elf_dwarf_analysis_get_variable(const xx_elf_dwarf_analysis *analysis, uint64_t index);
XXFC_API const xx_elf_dwarf_analysis_variable *xx_elf_dwarf_analysis_find_variable(const xx_elf_dwarf_analysis *analysis, uint32_t function_index, const char *name,
                                                                                   uint32_t occurrence);
XXFC_API uint64_t xx_elf_dwarf_analysis_get_number_of_accesses(const xx_elf_dwarf_analysis *analysis);
XXFC_API const xx_elf_dwarf_analysis_access *xx_elf_dwarf_analysis_get_access(const xx_elf_dwarf_analysis *analysis, uint64_t index);

/** Add an analysis type known to the caller.  Data-type names participate in
 * function-name collision repair; function types deliberately do not. */
XXFC_API bool xx_elf_dwarf_analysis_register_type(xx_elf_dwarf_analysis *analysis, const char *name, xx_elf_dwarf_registered_type_kind_t kind);

/** Install a user-owned function signature.  Overrides are keyed by address,
 * survive reimport and take precedence over imported names/prototypes. */
XXFC_API bool xx_elf_dwarf_analysis_set_function_override(xx_elf_dwarf_analysis *analysis, uint64_t address, const char *name, const char *prototype);
XXFC_API bool xx_elf_dwarf_analysis_clear_function_override(xx_elf_dwarf_analysis *analysis, uint64_t address);
XXFC_API const char *xx_elf_dwarf_analysis_get_effective_function_name(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function);
XXFC_API const char *xx_elf_dwarf_analysis_get_effective_function_prototype(const xx_elf_dwarf_analysis *analysis, const xx_elf_dwarf_analysis_function *function);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ELF_DWARF_ANALYSIS_H */
