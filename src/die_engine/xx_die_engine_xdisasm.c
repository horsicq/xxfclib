/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DIE's heuristic scripts inspect complete instruction strings and lengths.
 * The vendored x86 Capstone backend is shared with the native reference; its
 * private namespace and xx_rt callbacks keep this library Qt/CRT independent.
 */
#include "xx_die_engine_xdisasm.h"
#include "xx_die_engine_capstone_symbols.h"
#include <capstone/capstone.h>

typedef struct {
    csh handles[3];
    cs_insn *instructions[3];
} DisasmContext;

static void uppercase(char *destination, size_t capacity, const char *source)
{
    size_t i;
    for (i = 0; source[i] && i + 1 < capacity; ++i) {
        unsigned char byte = (unsigned char)source[i];
        destination[i] = (char)(byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte);
    }
    destination[i] = 0;
}

static DisasmContext *context(DieFile *file, int bits, int *index)
{
    DisasmContext *result = (DisasmContext *)file->pDisasmContext;
    cs_mode mode;
    if (bits != 16 && bits != 32 && bits != 64) return NULL;
    *index = bits == 16 ? 0 : bits == 32 ? 1 : 2;
    mode = bits == 16 ? CS_MODE_16 : bits == 32 ? CS_MODE_32 : CS_MODE_64;
    if (!result) {
        result = (DisasmContext *)xx_rt_calloc(1, sizeof(*result));
        if (!result) return NULL;
        file->pDisasmContext = result;
    }
    if (!result->handles[*index]) {
        if (cs_open(CS_ARCH_X86, mode, &result->handles[*index]) != CS_ERR_OK) return NULL;
        if (cs_option(result->handles[*index], CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK) {
            cs_close(&result->handles[*index]);
            return NULL;
        }
        result->instructions[*index] = cs_malloc(result->handles[*index]);
        if (!result->instructions[*index]) {
            cs_close(&result->handles[*index]);
            return NULL;
        }
    }
    return result;
}

void xdisasm_close(DieFile *file)
{
    DisasmContext *state = (DisasmContext *)file->pDisasmContext;
    int i;
    if (!state) return;
    for (i = 0; i < 3; ++i) {
        if (state->instructions[i]) cs_free(state->instructions[i], 1);
        if (state->handles[i]) cs_close(&state->handles[i]);
    }
    xx_rt_free(state);
    file->pDisasmContext = NULL;
}

XDisasmResult xdisasm_at(DieFile *file, cd_i64 offset, int bits, cd_u64 address)
{
    XDisasmResult result = {0};
    DisasmContext *state;
    cs_insn *instruction;
    /* Fifteen bytes is the x86 instruction limit, not an I/O chunk size.
     * read_at assembles this decoder state using the file's captured capacity. */
    unsigned char bytes[15];
    const uint8_t *code = bytes;
    size_t size;
    uint64_t cursor = address;
    int index;
    unsigned int i;

    if (!file || offset < 0 || offset >= file->nSize) return result;
    size = (size_t)(file->nSize - offset > 15 ? 15 : file->nSize - offset);
    if (!die_file_read_at(file, offset, bytes, size)) return result;
    state = context(file, bits, &index);
    if (!state) return result;
    instruction = state->instructions[index];
    if (!cs_disasm_iter(state->handles[index], &code, &size, &cursor, instruction)) {
        /* The reference bridge emits an Intel-format DB byte and leaves its
         * next-address field zero when Capstone cannot decode an instruction. */
        result.nSize = 1;
        result.nTargetBits = -1;
        uppercase(result.sMnemonic, sizeof(result.sMnemonic), "db");
        if (bytes[0] < 10) x_snprintf(result.sOperands, sizeof(result.sOperands), "%u", (unsigned)bytes[0]);
        else x_snprintf(result.sOperands, sizeof(result.sOperands), "0X%X", (unsigned)bytes[0]);
        return result;
    }

    result.nSize = instruction->size;
    uppercase(result.sMnemonic, sizeof(result.sMnemonic), instruction->mnemonic);
    uppercase(result.sOperands, sizeof(result.sOperands), instruction->op_str);
    for (i = 0; i < instruction->detail->groups_count; ++i) {
        if (instruction->detail->groups[i] == CS_GRP_BRANCH_RELATIVE) {
            unsigned int j;
            for (j = 0; j < instruction->detail->x86.op_count; ++j) {
                if (instruction->detail->x86.operands[j].type == X86_OP_IMM) {
                    result.bRelative = 1;
                    result.nTargetBits = bits == 64 ? 64 : 32;
                    result.nRelative = (cd_i64)((cd_u64)instruction->detail->x86.operands[j].imm - address - result.nSize);
                    break;
                }
            }
            break;
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
    cd_u64 next = address + (cd_u64)result->nSize;
    if (result->nTargetBits == -1) return 0;
    if (result->bRelative) {
        next += (cd_u64)result->nRelative;
        if (result->nTargetBits == 32) next &= 0xffffffff;
    }
    return next;
}
