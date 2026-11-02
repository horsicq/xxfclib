/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/executable/dotnet/xx_dotnet_cil.h"
#include "xxfclib/rt/xx_rt.h"

static uint32_t little32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool input_valid(const uint8_t *code, size_t length)
{
    return length <= XX_DOTNET_CIL_MAX_CODE && (code || !length);
}

const char *xx_dotnet_cil_status_string(xx_dotnet_cil_status status)
{
    switch (status) {
    case XX_DOTNET_CIL_OK: return "valid CIL";
    case XX_DOTNET_CIL_INVALID_ARGUMENT: return "invalid CIL argument or extent";
    case XX_DOTNET_CIL_TRUNCATED: return "truncated CIL instruction";
    case XX_DOTNET_CIL_INVALID_OPCODE: return "unsupported or invalid CIL opcode";
    case XX_DOTNET_CIL_SWITCH_LIMIT: return "CIL switch exceeds bound";
    case XX_DOTNET_CIL_CAPACITY: return "CIL boundary map is too small";
    case XX_DOTNET_CIL_NOT_BRANCH: return "instruction is not a relative branch or switch";
    case XX_DOTNET_CIL_TARGET_INDEX: return "invalid CIL branch target index";
    case XX_DOTNET_CIL_INVALID_TARGET: return "branch target is not an instruction boundary";
    case XX_DOTNET_CIL_INVALID_SPAN: return "replacement span is not instruction aligned";
    case XX_DOTNET_CIL_BRANCH_IN_SPAN: return "branch enters replacement interior";
    }
    return "unknown CIL status";
}

int xx_dotnet_cil_operand_width(unsigned op, bool prefixed)
{
    if (op > 0xff) return -1;
    if (prefixed) {
        if (op <= 5 || op == 15 || op == 17 || op == 19 || op == 20 ||
            op == 23 || op == 24 || op == 26 || op == 29 || op == 30) return 0;
        if (op == 6 || op == 7 || op == 21 || op == 22 || op == 28) return 4;
        if (op >= 9 && op <= 14) return 2;
        if (op == 18 || op == 25) return 1;
        return -1;
    }
    if (op <= 13 || (op >= 20 && op <= 30) || op == 37 || op == 38 || op == 42 ||
        (op >= 70 && op <= 110) || op == 118 || op == 122 ||
        (op >= 130 && op <= 139) || op == 142 || (op >= 144 && op <= 162) ||
        (op >= 179 && op <= 186) || op == 195 || (op >= 209 && op <= 220) || op == 223 || op == 224) return 0;
    if ((op >= 14 && op <= 19) || op == 31 || (op >= 43 && op <= 55) || op == 222) return 1;
    if (op == 32 || op == 34 || (op >= 39 && op <= 41) || (op >= 56 && op <= 68) ||
        (op >= 111 && op <= 117) || op == 121 || (op >= 123 && op <= 129) ||
        op == 140 || op == 141 || op == 143 || (op >= 163 && op <= 165) ||
        op == 194 || op == 198 || op == 208 || op == 221) return 4;
    if (op == 33 || op == 35) return 8;
    if (op == 69) return -2;
    return -1;
}

bool xx_dotnet_cil_is_branch(uint32_t opcode)
{
    return (opcode >= 0x2b && opcode <= 0x44) || opcode == 0xdd || opcode == 0xde;
}

xx_dotnet_cil_status xx_dotnet_cil_decode(const uint8_t *code,
    size_t length, uint32_t offset, xx_dotnet_cil_instruction *instruction)
{
    xx_dotnet_cil_instruction value = {0};
    uint32_t at = offset, op, count = 0;
    bool prefixed;
    int width;
    if (instruction) xx_rt_memset(instruction, 0, sizeof(*instruction));
    if (!instruction || !input_valid(code, length)) return XX_DOTNET_CIL_INVALID_ARGUMENT;
    if (at >= length) return XX_DOTNET_CIL_TRUNCATED;
    op = code[at++];
    prefixed = op == 0xfe;
    if (prefixed) {
        if (at >= length) return XX_DOTNET_CIL_TRUNCATED;
        op = code[at++];
    }
    width = xx_dotnet_cil_operand_width(op, prefixed);
    if (width == -1) return XX_DOTNET_CIL_INVALID_OPCODE;
    if (width == -2) {
        if (length - at < 4) return XX_DOTNET_CIL_TRUNCATED;
        count = little32(code + at);
        if (count > XX_DOTNET_CIL_MAX_SWITCH) return XX_DOTNET_CIL_SWITCH_LIMIT;
        width = 4 + (int)count * 4;
    }
    if ((uint32_t)width > length - at) return XX_DOTNET_CIL_TRUNCATED;
    value.offset = offset;
    value.opcode = prefixed ? 0xfe00u + op : op;
    value.operand = at;
    value.operand_width = (uint32_t)width;
    value.width = at - offset + (uint32_t)width;
    value.value = width == 4 ? little32(code + at) : 0;
    value.target_count = xx_dotnet_cil_is_branch(value.opcode) ? 1 : value.opcode == 0x45 ? count : 0;
    *instruction = value;
    return XX_DOTNET_CIL_OK;
}

static int64_t signed_operand(const uint8_t *data, uint32_t width)
{
    uint32_t value = width == 1 ? data[0] : little32(data);
    if (width == 1) return value >= 0x80 ? (int64_t)value - 0x100 : (int64_t)value;
    return value >= UINT32_C(0x80000000) ? (int64_t)value - INT64_C(0x100000000) : (int64_t)value;
}

xx_dotnet_cil_status xx_dotnet_cil_target(const uint8_t *code,
    size_t length, uint32_t offset, uint32_t target_index, int64_t *target)
{
    xx_dotnet_cil_instruction instruction;
    xx_dotnet_cil_status status;
    const uint8_t *operand;
    if (target) *target = 0;
    if (!target) return XX_DOTNET_CIL_INVALID_ARGUMENT;
    status = xx_dotnet_cil_decode(code, length, offset, &instruction);
    if (status != XX_DOTNET_CIL_OK) return status;
    if (!xx_dotnet_cil_is_branch(instruction.opcode) && instruction.opcode != 0x45)
        return XX_DOTNET_CIL_NOT_BRANCH;
    if (target_index >= instruction.target_count) return XX_DOTNET_CIL_TARGET_INDEX;
    operand = code + instruction.operand;
    *target = (int64_t)offset + instruction.width +
        (instruction.opcode == 0x45 ? signed_operand(operand + 4 + target_index * 4, 4) :
            signed_operand(operand, instruction.operand_width));
    return XX_DOTNET_CIL_OK;
}

/* Comparing integer representations avoids pointer arithmetic across unrelated
 * caller allocations. Reject a scratch map that would overwrite input code. */
static bool overlaps(const uint8_t *code, size_t length, const uint8_t *map)
{
    uintptr_t a = (uintptr_t)code, b = (uintptr_t)map;
    return a <= b ? b - a < length : a - b < length;
}

xx_dotnet_cil_status xx_dotnet_cil_validate(const uint8_t *code,
    size_t length, uint8_t *boundary_map, size_t map_capacity)
{
    xx_dotnet_cil_instruction instruction;
    xx_dotnet_cil_status status;
    uint32_t at = 0;
    if (!input_valid(code, length)) return XX_DOTNET_CIL_INVALID_ARGUMENT;
    if (map_capacity < length) return XX_DOTNET_CIL_CAPACITY;
    if (length && (!boundary_map || overlaps(code, length, boundary_map)))
        return XX_DOTNET_CIL_INVALID_ARGUMENT;
    if (length) xx_rt_memset(boundary_map, 0, length);
    while (at < length) {
        status = xx_dotnet_cil_decode(code, length, at, &instruction);
        if (status != XX_DOTNET_CIL_OK) return status;
        boundary_map[at] = 1;
        at += instruction.width;
    }
    at = 0;
    while (at < length) {
        uint32_t index;
        status = xx_dotnet_cil_decode(code, length, at, &instruction);
        if (status != XX_DOTNET_CIL_OK) return status;
        for (index = 0; index < instruction.target_count; ++index) {
            int64_t target;
            status = xx_dotnet_cil_target(code, length, at, index, &target);
            if (status != XX_DOTNET_CIL_OK) return status;
            if (target < 0 || target >= (int64_t)length || !boundary_map[(size_t)target])
                return XX_DOTNET_CIL_INVALID_TARGET;
        }
        at += instruction.width;
    }
    return XX_DOTNET_CIL_OK;
}

xx_dotnet_cil_status xx_dotnet_cil_safe_span(const uint8_t *code,
    size_t length, uint32_t start, uint32_t end,
    uint8_t *boundary_map, size_t map_capacity)
{
    xx_dotnet_cil_instruction instruction;
    xx_dotnet_cil_status status;
    uint32_t at = 0;
    if (start >= end || end > length) return XX_DOTNET_CIL_INVALID_SPAN;
    status = xx_dotnet_cil_validate(code, length, boundary_map, map_capacity);
    if (status != XX_DOTNET_CIL_OK) return status;
    if (!boundary_map[start] || (end < length && !boundary_map[end])) return XX_DOTNET_CIL_INVALID_SPAN;
    while (at < length) {
        uint32_t index;
        status = xx_dotnet_cil_decode(code, length, at, &instruction);
        if (status != XX_DOTNET_CIL_OK) return status;
        for (index = 0; index < instruction.target_count; ++index) {
            int64_t target;
            status = xx_dotnet_cil_target(code, length, at, index, &target);
            if (status != XX_DOTNET_CIL_OK) return status;
            if (target > start && target < end) return XX_DOTNET_CIL_BRANCH_IN_SPAN;
        }
        at += instruction.width;
    }
    return XX_DOTNET_CIL_OK;
}
