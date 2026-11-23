/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PYC_H
#define XXFCLIB_FORMAT_PYC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_pyc {
    Abstractformat format;
    uint16_t magic;
    char version[24];
    uint32_t flags, header_size;
    int64_t marshal_offset, marshal_size;
    xx_list_s constants; /* Owned UTF-8 char *; free through destroy/free. */
    bool analyzed;
    void *analysis; /* Owned bounded code-object analysis; use the accessors below. */
} xx_pyc;

/** One Python VM register from the stable analysis profile. */
typedef struct xx_pyc_register_s {
    const char *name;
    uint16_t bit_size;
    uint16_t byte_offset;
    bool is_program_counter;
    bool is_stack_pointer;
    bool is_base_pointer;
    bool is_argument;
    bool is_return_value;
} xx_pyc_register;

/** A marshal code object's executable-byte extent. Strings are borrowed from
 * the reader and remain valid until xx_pyc_destroy/xx_pyc_free. Offsets are
 * absolute device offsets and addresses are relative to the PYC base, matching
 * Python bytecode jump targets and Radare2's PYC address space. */
typedef struct xx_pyc_code_object_s {
    const char *name;
    const char *qualified_name;
    const char *filename;
    int64_t offset;
    uint64_t address;
    uint32_t size;
    uint32_t argument_count;
    uint32_t keyword_only_argument_count;
    uint32_t local_count;
    uint32_t stack_size;
    uint32_t flags;
    uint32_t first_line;
    uint32_t parent_index; /* UINT32_MAX for the module code object. */
} xx_pyc_code_object;

typedef enum xx_pyc_instruction_kind_e {
    XX_PYC_INSTRUCTION_OTHER = 0,
    XX_PYC_INSTRUCTION_LOAD,
    XX_PYC_INSTRUCTION_STORE,
    XX_PYC_INSTRUCTION_CALL,
    XX_PYC_INSTRUCTION_JUMP,
    XX_PYC_INSTRUCTION_CONDITIONAL_JUMP,
    XX_PYC_INSTRUCTION_RETURN
} xx_pyc_instruction_kind_t;

/** A single bounded Python bytecode decode. The text buffers are always NUL
 * terminated. jump_address is UINT64_MAX when the instruction has no jump. */
typedef struct xx_pyc_instruction_s {
    uint8_t opcode;
    uint8_t size;
    bool has_argument;
    uint32_t argument;
    uint64_t address;
    uint64_t jump_address;
    uint64_t fail_address;
    xx_pyc_instruction_kind_t kind;
    char mnemonic[32];
    char operand[256];
} xx_pyc_instruction;

XXFC_API void xx_pyc_init(xx_pyc *pyc, xx_io_device *device, int64_t base_address);
XXFC_API xx_pyc *xx_pyc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pyc_destroy(xx_pyc *pyc);
XXFC_API void xx_pyc_free(xx_pyc *pyc);
XXFC_API bool xx_pyc_is_known_magic(uint16_t magic);
XXFC_API bool xx_pyc_check_magic(const uint8_t *data, size_t size);
/* Inspection preserves a recognized header/version even when its marshal
 * body is damaged. Strict callbacks and carving require a complete module. */
XXFC_API bool xx_pyc_analyze(xx_pyc *pyc, xx_pd_struct *pd);
XXFC_API bool xx_pyc_const_present(const xx_pyc *pyc, const char *value);
/* Static Python VM register profile. */
XXFC_API size_t xx_pyc_get_number_of_registers(void);
XXFC_API const xx_pyc_register *xx_pyc_get_register(size_t index);
XXFC_API const char *xx_pyc_get_register_profile(void);
/* Code-object analysis is produced by xx_pyc_analyze and is bounded by the
 * same object/depth/memory limits as the marshal reader. */
XXFC_API uint64_t xx_pyc_get_number_of_code_objects(xx_pyc *pyc, xx_pd_struct *pd);
XXFC_API const xx_pyc_code_object *xx_pyc_get_code_object(xx_pyc *pyc, uint64_t index, xx_pd_struct *pd);
XXFC_API int64_t xx_pyc_get_entry_point(xx_pyc *pyc, xx_pd_struct *pd);
/* Decode from the code bytes in the input device, or from caller supplied
 * bytes for overlays/patch previews. code_offset is relative to the selected
 * code object. decode_bytes uses address as the first byte's PYC-relative
 * address and does not retain the supplied buffer. */
XXFC_API bool xx_pyc_decode_instruction(xx_pyc *pyc, uint64_t code_index, uint32_t code_offset, xx_pyc_instruction *output, xx_pd_struct *pd);
XXFC_API bool xx_pyc_decode_bytes(xx_pyc *pyc, uint64_t code_index, uint64_t address, const uint8_t *data, size_t size,
                                  xx_pyc_instruction *output, xx_pd_struct *pd);
XXFC_API bool xx_pyc_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_pyc_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_pyc_get_format_size(Abstractformat *format, xx_pd_struct *pd);
static inline Abstractformat *xx_pyc_to_format(xx_pyc *pyc)
{
    return pyc ? &pyc->format : NULL;
}
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pyc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pyc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pyc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pyc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pyc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
