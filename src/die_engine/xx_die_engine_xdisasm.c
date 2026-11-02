/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DIE's heuristic scripts inspect complete instruction strings and lengths.
 * Decode and format one instruction at a time with the stateless cdisasm API.
 */
#include "xx_die_engine_xdisasm.h"

#include <cdisasm/cdisasm_x86.h>
#include <cdisasm/cdisasm_format.h>

static void uppercase(char *destination, size_t capacity, const char *source)
{
    size_t i;
    for (i = 0; source[i] && i + 1 < capacity; ++i) {
        unsigned char byte = (unsigned char)source[i];
        destination[i] = (char)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte);
    }
    destination[i] = 0;
}

static const char *script_stack_mnemonic(cdisasm_x86_name_id name)
{
    /* DIE scripts depend on Capstone's Intel spellings for the all-register
     * stack instructions. Use the decoded operand width (including 0x66),
     * while keeping cdisasm's native Intel formatter unchanged. These
     * instructions have no operands; Capstone omits ignored prefixes. */
    switch (name) {
        case CDISASM_X86_NAME_PUSHA: return "PUSHAW";
        case CDISASM_X86_NAME_PUSHAD: return "PUSHAL";
        case CDISASM_X86_NAME_POPA: return "POPAW";
        case CDISASM_X86_NAME_POPAD: return "POPAL";
        default: return NULL;
    }
}

static XDisasmResult invalid_byte(unsigned char byte)
{
    XDisasmResult result = {0};

    result.nSize = 1;
    result.bInvalid = 1;
    if (byte < 10) {
        x_snprintf(result.sInstruction, sizeof(result.sInstruction), "DB %u", (unsigned)byte);
    } else {
        x_snprintf(result.sInstruction, sizeof(result.sInstruction), "DB 0X%X", (unsigned)byte);
    }
    return result;
}

static int script_decode_flags(cdisasm_mode mode, cdisasm_x86_decode_flags *flags)
{
    static const uint32_t reduced_bits[] = {
        CDISASM_X86_DECODE_BIT_BITMANIP,
        CDISASM_X86_DECODE_BIT_SMX,
        CDISASM_X86_DECODE_BIT_SYSTEM,
        CDISASM_X86_DECODE_BIT_CET,
        CDISASM_X86_DECODE_BIT_STATE,
        CDISASM_X86_DECODE_BIT_SECURITY,
        CDISASM_X86_DECODE_BIT_UNDOCUMENTED,
        CDISASM_X86_DECODE_BIT_VMX,
        CDISASM_X86_DECODE_BIT_SVM,
        CDISASM_X86_DECODE_BIT_BMI1,
        CDISASM_X86_DECODE_BIT_BMI2,
        CDISASM_X86_DECODE_BIT_VTX
    };
    size_t i;

    if (cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, mode, flags) != CDISASM_STATUS_OK) return 0;

    /* Qt DIE links XCapstone's CAPSTONE_X86_REDUCE target. It retains scalar
     * extensions and privileged/system instructions, but excludes x87 and
     * vector families. Keep an all-zero mask for base-only cdisasm builds. */
    if (!cdisasm_decode_flags_test_bit(flags, CDISASM_X86_DECODE_BIT_SYSTEM)) return 1;
    cdisasm_decode_flags_reset(flags);
    for (i = 0; i < sizeof(reduced_bits) / sizeof(reduced_bits[0]); ++i) {
        cdisasm_decode_flags_set_bit(flags, reduced_bits[i]);
    }
    return 1;
}

XDisasmResult xdisasm_at(xx_io_device *file, cd_i64 offset, int bits, cd_u64 address)
{
    XDisasmResult result = {0};
    cdisasm_x86_instruction instruction;
    cdisasm_x86_decode_flags flags;
    cdisasm_mode mode;
    unsigned char bytes[CDISASM_MAX_INSTRUCTION_SIZE];
    char formatted[sizeof(result.sInstruction)];
    size_t size;
    size_t formatted_size;
    cd_i64 file_size;
    uint32_t decoded_size;
    uint8_t i;
    const char *stack_mnemonic;

    if (!file) return result;
    file_size = xx_io_total_size(file);
    if (offset < 0 || offset >= file_size) return result;
    if (bits != 16 && bits != 32 && bits != 64) return result;

    mode = bits == 16 ? CDISASM_MODE_16 : bits == 32 ? CDISASM_MODE_32 : CDISASM_MODE_64;
    size = (size_t)(file_size - offset > CDISASM_MAX_INSTRUCTION_SIZE ? CDISASM_MAX_INSTRUCTION_SIZE : file_size - offset);
    if (!xx_io_read_at(file, offset, bytes, size)) return result;

    if (!script_decode_flags(mode, &flags)) return result;
    decoded_size = cdisasm_x86_decode(CDISASM_CPU_X86, mode, bytes, size, address, &flags, &instruction);
    if (!decoded_size) return invalid_byte(bytes[0]);

    formatted_size = cdisasm_x86_format_mode(&instruction, mode, CDISASM_FORMAT_SYNTAX_INTEL, formatted, sizeof(formatted));
    if (!formatted_size || formatted_size >= sizeof(formatted)) {
        /* A recognized form without text must still advance as an invalid
         * byte, just as the old bridge did when it could not disassemble. */
        return invalid_byte(bytes[0]);
    }

    result.nSize = (int)decoded_size;
    stack_mnemonic = script_stack_mnemonic(instruction.name_id);
    uppercase(result.sInstruction, sizeof(result.sInstruction), stack_mnemonic ? stack_mnemonic : formatted);

    if (instruction.opcode_groups & CDISASM_GROUP_RELATIVE_BRANCH) {
        for (i = 0; i < instruction.operand_count; ++i) {
            const cdisasm_x86_operand *operand = &instruction.opcode[i];
            if (operand->type == CDISASM_OPERAND_IMMEDIATE && (operand->flags & CDISASM_OPERAND_FLAG_PC_RELATIVE)) {
                size_t end;
                cd_u64 target = address + (cd_u64)result.nSize + operand->address;
                if (bits == 16) target &= 0xffff;
                else if (bits == 32) target &= 0xffffffff;

                /* A relative transfer has one trailing immediate. Keep any
                 * textual instruction prefix while using the script API's
                 * historical uppercase, absolute-hex target spelling. */
                end = 0;
                while (result.sInstruction[end]) ++end;
                while (end && result.sInstruction[end - 1] != ' ') --end;
                if (end) {
                    x_snprintf(result.sInstruction + end, sizeof(result.sInstruction) - end, "0X%llX", (unsigned long long)target);
                }
                break;
            }
        }
    }
    return result;
}

XDisasmResult xdisasm(xx_io_device *file, cd_i64 offset, int bits)
{
    return xdisasm_at(file, offset, bits, 0);
}

cd_u64 xdisasm_next_address(const XDisasmResult *result, cd_u64 address)
{
    if (result->bInvalid) return 0;
    /* The script API walks encoded instructions sequentially, including
     * direct branches. Keep the resolved branch target in the text only. */
    return address + (cd_u64)result->nSize;
}
