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

void xdisasm_close(DieFile *file)
{
    /* cdisasm has no decoder handles or allocations to release. */
    if (file) file->pDisasmContext = NULL;
}

XDisasmResult xdisasm_at(DieFile *file, cd_i64 offset, int bits, cd_u64 address)
{
    XDisasmResult result = {0};
    cdisasm_x86_instruction instruction;
    cdisasm_x86_decode_flags flags;
    cdisasm_mode mode;
    unsigned char bytes[CDISASM_MAX_INSTRUCTION_SIZE];
    char formatted[sizeof(result.sInstruction)];
    size_t size;
    size_t formatted_size;
    uint32_t decoded_size;
    uint8_t i;

    if (!file || offset < 0 || offset >= file->nSize) return result;
    if (bits != 16 && bits != 32 && bits != 64) return result;

    mode = bits == 16 ? CDISASM_MODE_16 : bits == 32 ? CDISASM_MODE_32 : CDISASM_MODE_64;
    size = (size_t)(file->nSize - offset > CDISASM_MAX_INSTRUCTION_SIZE
                        ? CDISASM_MAX_INSTRUCTION_SIZE : file->nSize - offset);
    if (!die_file_read_at(file, offset, bytes, size)) return result;

    /* The unrestricted x86 profile keeps the DIE script's architecture-wide
     * decoding. The mode-specific mask also works in a base-only build. */
    if (cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, mode, &flags)
        != CDISASM_STATUS_OK) return result;
    decoded_size = cdisasm_x86_decode(CDISASM_CPU_X86, mode, bytes, size,
                                      address, &flags, &instruction);
    if (!decoded_size) return invalid_byte(bytes[0]);

    formatted_size = cdisasm_x86_format_mode(&instruction, mode,
                                               CDISASM_FORMAT_SYNTAX_INTEL,
                                               formatted, sizeof(formatted));
    if (!formatted_size || formatted_size >= sizeof(formatted)) {
        /* A recognized form without text must still advance as an invalid
         * byte, just as the old bridge did when it could not disassemble. */
        return invalid_byte(bytes[0]);
    }

    result.nSize = (int)decoded_size;
    uppercase(result.sInstruction, sizeof(result.sInstruction), formatted);

    if (instruction.opcode_groups & CDISASM_GROUP_RELATIVE_BRANCH) {
        for (i = 0; i < instruction.operand_count; ++i) {
            const cdisasm_x86_operand *operand = &instruction.opcode[i];
            if (operand->type == CDISASM_OPERAND_IMMEDIATE
                && (operand->flags & CDISASM_OPERAND_FLAG_PC_RELATIVE)) {
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
                    x_snprintf(result.sInstruction + end,
                               sizeof(result.sInstruction) - end, "0X%llX",
                               (unsigned long long)target);
                }
                break;
            }
        }
    }
    return result;
}

XDisasmResult xdisasm(DieFile *file, cd_i64 offset, int bits)
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
