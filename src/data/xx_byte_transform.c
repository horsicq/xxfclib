/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxfclib/data/xx_byte_transform.h"
bool xx_byte_transform_apply(void *data, size_t length, uint64_t phase, const xx_byte_transform *op)
{
    if (!op || (!data && length) || op->kind < XX_BYTE_FILL || op->kind > XX_BYTE_SWAP_ENDIAN) return false;
    unsigned char *bytes = data;
    if (op->kind == XX_BYTE_FILL) {
        if (!op->pattern || !op->pattern_length) return false;
        size_t at = (size_t)(phase % op->pattern_length);
        for (size_t i = 0; i < length; ++i) {
            bytes[i] = op->pattern[at];
            if (++at == op->pattern_length) at = 0;
        }
        return true;
    }
    unsigned int width = op->width;
    if ((width != 1 && width != 2 && width != 4 && width != 8) || length % width) return false;
    unsigned int bits = width * 8, rotation = (unsigned int)(op->operand % bits);
    uint64_t mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    for (size_t at = 0; at < length; at += width) {
        if (op->kind == XX_BYTE_SWAP_ENDIAN) {
            for (unsigned int i = 0; i < width / 2; ++i) {
                unsigned char tmp = bytes[at + i];
                bytes[at + i] = bytes[at + width - 1 - i];
                bytes[at + width - 1 - i] = tmp;
            }
            continue;
        }
        uint64_t value = 0;
        for (unsigned int i = 0; i < width; ++i) value |= (uint64_t)bytes[at + i] << (8 * (op->big_endian ? width - 1 - i : i));
        switch (op->kind) {
            case XX_BYTE_XOR: value ^= op->operand; break;
            case XX_BYTE_AND: value &= op->operand; break;
            case XX_BYTE_OR: value |= op->operand; break;
            case XX_BYTE_NOT: value = ~value; break;
            case XX_BYTE_ADD: value += op->operand; break;
            case XX_BYTE_SUBTRACT: value -= op->operand; break;
            case XX_BYTE_ROTATE_LEFT:
                if (rotation) value = ((value << rotation) | (value >> (bits - rotation))) & mask;
                break;
            case XX_BYTE_ROTATE_RIGHT:
                if (rotation) value = ((value >> rotation) | (value << (bits - rotation))) & mask;
                break;
            default: return false;
        }
        for (unsigned int i = 0; i < width; ++i) bytes[at + i] = (unsigned char)(value >> (8 * (op->big_endian ? width - 1 - i : i)));
    }
    return true;
}
