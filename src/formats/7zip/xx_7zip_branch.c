/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_7zip_branch.h"
#include "xx_7zip_defs.h"
#include "xxfclib/memory/xx_memory.h"

typedef struct xx_7zip_bcj2_decoder_s {
    const uint8_t *main_data;
    const uint8_t *call_data;
    const uint8_t *jump_data;
    const uint8_t *range_data;
    size_t main_size;
    size_t call_size;
    size_t jump_size;
    size_t range_size;
    size_t main_pos;
    size_t call_pos;
    size_t jump_pos;
    size_t range_pos;
    uint32_t range;
    uint32_t code;
    uint32_t ip;
    uint16_t probabilities[258];
    uint8_t previous;
} xx_7zip_bcj2_decoder;

static uint32_t xx_7zip_branch_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint32_t xx_7zip_branch_read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static void xx_7zip_branch_write_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static void xx_7zip_branch_write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24U);
    p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U);
    p[3] = (uint8_t)value;
}

bool xx_7zip_branch_properties_supported(size_t properties_size) {
    return properties_size == 0U || properties_size == 4U;
}

static uint32_t xx_7zip_branch_start_ip(const uint8_t *properties,
                                         size_t properties_size) {
    return properties_size == 4U ? xx_7zip_branch_read_le32(properties) : 0U;
}

bool xx_7zip_branch_method_supported(uint64_t method, size_t properties_size) {
    if (!xx_7zip_branch_properties_supported(properties_size)) return false;
    switch (method) {
        case XX_7ZIP_METHOD_BCJ:
        case XX_7ZIP_METHOD_PPC:
        case XX_7ZIP_METHOD_IA64:
        case XX_7ZIP_METHOD_ARM:
        case XX_7ZIP_METHOD_ARMT:
        case XX_7ZIP_METHOD_SPARC:
        case XX_7ZIP_METHOD_ARM64:
        case XX_7ZIP_METHOD_RISCV:
            return true;
        default:
            return false;
    }
}

static bool xx_7zip_x86_ms_byte(uint8_t value) {
    return ((uint8_t)(value + 1U) & 0xFEU) == 0U;
}

static void xx_7zip_filter_bcj(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos = 0U;
    size_t limit;
    uint32_t mask = 0U;
    if (!data || size < 5U) return;
    limit = size - 4U;
    ip += 5U;
    while (pos < limit) {
        size_t candidate = pos;
        size_t distance;
        uint32_t value;
        uint32_t current;
        while (candidate < limit && (data[candidate] & 0xFEU) != 0xE8U) ++candidate;
        distance = candidate - pos;
        pos = candidate;
        if (candidate >= limit) return;
        if (distance > 2U) {
            mask = 0U;
        } else {
            mask >>= (unsigned)distance;
            if (mask != 0U && (mask > 4U || mask == 3U ||
                xx_7zip_x86_ms_byte(data[candidate + (mask >> 1U) + 1U]))) {
                mask = (mask >> 1U) | 4U;
                ++pos;
                continue;
            }
        }
        if (!xx_7zip_x86_ms_byte(data[candidate + 4U])) {
            mask = (mask >> 1U) | 4U;
            ++pos;
            continue;
        }
        value = xx_7zip_branch_read_le32(data + candidate + 1U);
        current = ip + (uint32_t)candidate;
        value -= current;
        if (mask != 0U) {
            unsigned shift = (mask & 6U) << 2U;
            if (xx_7zip_x86_ms_byte((uint8_t)(value >> shift))) {
                value ^= ((UINT32_C(0x100) << shift) - 1U);
                value -= current;
            }
            mask = 0U;
        }
        data[candidate + 1U] = (uint8_t)value;
        data[candidate + 2U] = (uint8_t)(value >> 8U);
        data[candidate + 3U] = (uint8_t)(value >> 16U);
        data[candidate + 4U] = (uint8_t)(0U - ((value >> 24U) & 1U));
        pos = candidate + 5U;
    }
}

static void xx_7zip_filter_arm(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if (data[pos + 3U] != 0xEBU) continue;
        value = xx_7zip_branch_read_le32(data + pos);
        value = (value << 2U) - (ip + (uint32_t)pos + 8U);
        value = (value >> 2U) & UINT32_C(0x00FFFFFF);
        xx_7zip_branch_write_le32(data + pos, value | UINT32_C(0xEB000000));
    }
}

static void xx_7zip_filter_armt(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 2U) {
        uint32_t high = (uint32_t)(data[pos + 1U] ^ 8U);
        uint32_t low = data[pos + 3U];
        uint32_t value;
        if ((high & low) < UINT32_C(0xF8)) continue;
        value = (high << 19U) | ((uint32_t)(low & 7U) << 8U) |
                ((uint32_t)data[pos] << 11U) | data[pos + 2U];
        value -= (ip + (uint32_t)pos + 4U) >> 1U;
        data[pos] = (uint8_t)(value >> 11U);
        data[pos + 1U] = (uint8_t)(0xF0U | ((value >> 19U) & 7U));
        data[pos + 2U] = (uint8_t)value;
        data[pos + 3U] = (uint8_t)(0xF8U | (value >> 8U));
        pos += 2U;
    }
}

static void xx_7zip_filter_ppc(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if ((data[pos] & 0xFCU) != 0x48U || (data[pos + 3U] & 3U) != 1U) continue;
        value = xx_7zip_branch_read_be32(data + pos);
        value = (value - (ip + (uint32_t)pos)) & UINT32_C(0x03FFFFFF);
        xx_7zip_branch_write_be32(data + pos, value | UINT32_C(0x48000000));
    }
}

static void xx_7zip_filter_sparc(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if (!((data[pos] == 0x40U && (data[pos + 1U] & 0xC0U) == 0U) ||
              (data[pos] == 0x7FU && data[pos + 1U] >= 0xC0U))) continue;
        value = xx_7zip_branch_read_be32(data + pos);
        value = (value << 2U) - (ip + (uint32_t)pos);
        value = ((value & UINT32_C(0x01FFFFFF)) - UINT32_C(0x01000000)) ^
                UINT32_C(0xFF000000);
        value = (value >> 2U) | UINT32_C(0x40000000);
        xx_7zip_branch_write_be32(data + pos, value);
    }
}

static void xx_7zip_filter_ia64(uint8_t *data, size_t size, uint32_t ip) {
    size_t bundle;
    if (!data || size < 16U) return;
    for (bundle = 0U; bundle + 16U <= size; bundle += 16U) {
        unsigned slot = (UINT32_C(0x334B0000) >> (data[bundle] & 0x1EU)) & 3U;
        if (slot == 0U) continue;
        ++slot;
        do {
            uint8_t *p;
            uint32_t raw;
            uint32_t value;
            p = data + bundle + (size_t)slot * 5U - 8U;
            if (((p[3] >> slot) & 15U) != 5U ||
                ((((uint32_t)p[-1] | ((uint32_t)p[0] << 8U)) >> slot) & 0x70U) != 0U) continue;
            raw = xx_7zip_branch_read_le32(p);
            value = raw >> slot;
            value = (value & UINT32_C(0xFFFFF)) | ((value & UINT32_C(0x800000)) >> 3U);
            value = (value << 4U) - (ip + (uint32_t)bundle);
            value = (value >> 4U) & UINT32_C(0x1FFFFF);
            value = (value + UINT32_C(0x700000)) & UINT32_C(0x8FFFFF);
            raw &= ~(UINT32_C(0x8FFFFF) << slot);
            raw |= value << slot;
            xx_7zip_branch_write_le32(p, raw);
        } while (++slot <= 4U);
    }
}

static void xx_7zip_filter_arm64(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    const uint32_t flag = UINT32_C(1) << 20U;
    const uint32_t adrp_mask = (UINT32_C(1) << 24U) - (flag << 1U);
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value = xx_7zip_branch_read_le32(data + pos);
        uint32_t pc = ip + (uint32_t)pos;
        if (((value - UINT32_C(0x94000000)) & UINT32_C(0xFC000000)) == 0U) {
            value = (value - (pc >> 2U)) & UINT32_C(0x03FFFFFF);
            xx_7zip_branch_write_le32(data + pos, UINT32_C(0x94000000) | value);
        } else {
            uint32_t page_value = value - UINT32_C(0x90000000);
            uint32_t target;
            if ((page_value & UINT32_C(0x9F000000)) != 0U) continue;
            page_value += flag;
            if ((page_value & adrp_mask) != 0U) continue;
            target = (page_value & UINT32_C(0xFFFFFFE0)) | (page_value >> 26U);
            target -= (pc >> 9U) & ~UINT32_C(7);
            value &= UINT32_C(0x1F);
            value |= UINT32_C(0x90000000);
            value |= target << 26U;
            value |= UINT32_C(0x00FFFFE0) &
                     ((target & ((flag << 1U) - 1U)) - flag);
            xx_7zip_branch_write_le32(data + pos, value);
        }
    }
}

/* RISC-V's converter has three call forms: JAL and two AUIPC-based pairs.
 * The encoded representation stores their target as an absolute address; this
 * decoder restores the PC-relative instruction fields. */
static uint16_t xx_7zip_branch_read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static bool xx_7zip_riscv_jal_register(uint32_t instruction) {
    uint32_t rd = (instruction >> 7U) & 31U;
    return (instruction & 0x7FU) == 0x6FU && (rd == 1U || rd == 5U);
}

static bool xx_7zip_riscv_pair_a(uint16_t first, uint32_t tag) {
    return (((tag - 3U) ^ ((uint32_t)first << 8U)) & UINT32_C(0x000F8003)) == 0U;
}

static bool xx_7zip_riscv_pair_b(uint16_t first, uint32_t register_bits) {
    uint32_t normalized = (uint32_t)first - UINT32_C(0x00003288);
    return (normalized << 18U) < (register_bits & UINT32_C(0x1D));
}

static uint32_t xx_7zip_riscv_jal_immediate(uint32_t instruction) {
    return ((instruction >> 20U) & UINT32_C(0x7FE)) |
           ((instruction >> 9U) & UINT32_C(0x800)) |
           (instruction & UINT32_C(0xFF000)) |
           ((instruction >> 11U) & UINT32_C(0x100000));
}

static uint32_t xx_7zip_riscv_jal_with_immediate(uint32_t instruction,
                                                   uint32_t immediate) {
    return (instruction & UINT32_C(0x00000FFF)) |
           ((immediate << 11U) & UINT32_C(0x80000000)) |
           ((immediate << 20U) & UINT32_C(0x7FE00000)) |
           ((immediate << 9U) & UINT32_C(0x00100000)) |
           (immediate & UINT32_C(0x000FF000));
}

static void xx_7zip_filter_riscv(uint8_t *data, size_t size, uint32_t ip) {
    size_t pos;
    if (!data || size < 8U) return;
    for (pos = 0U; pos + 8U <= size; pos += 2U) {
        uint16_t first = xx_7zip_branch_read_le16(data + pos);
        uint32_t instruction;
        uint32_t pc;
        if ((first & 3U) != 3U) continue;
        instruction = xx_7zip_branch_read_le32(data + pos);
        pc = ip + (uint32_t)pos;
        if (xx_7zip_riscv_jal_register(instruction)) {
            uint32_t absolute = xx_7zip_riscv_jal_immediate(instruction);
            uint32_t relative = absolute - pc;
            xx_7zip_branch_write_le32(data + pos,
                xx_7zip_riscv_jal_with_immediate(instruction, relative));
            pos += 2U;
            continue;
        }
        if ((instruction & 0x7FU) != 0x17U) continue;
        if (((uint32_t)first & UINT32_C(0xE80)) == 0U) {
            uint32_t register_bits = instruction >> 27U;
            if (xx_7zip_riscv_pair_b(first, register_bits)) {
                uint32_t absolute = xx_7zip_branch_read_be32(data + pos + 4U);
                uint32_t second = instruction >> 12U;
                uint32_t first_out;
                absolute -= pc;
                first_out = (register_bits << 7U) | UINT32_C(0x17);
                first_out += (absolute + UINT32_C(0x800)) & UINT32_C(0xFFFFF000);
                second |= absolute << 20U;
                xx_7zip_branch_write_le32(data + pos, first_out);
                xx_7zip_branch_write_le32(data + pos + 4U, second);
                pos += 6U;
            }
        } else {
            uint32_t second = xx_7zip_branch_read_le32(data + pos + 4U);
            if (xx_7zip_riscv_pair_a(first, second)) {
                uint32_t first_out = (instruction & UINT32_C(0xFFFFF000)) |
                                     (second >> 20U);
                uint32_t second_out = (second << 12U) | UINT32_C(0x117);
                xx_7zip_branch_write_le32(data + pos, first_out);
                xx_7zip_branch_write_le32(data + pos + 4U, second_out);
                pos += 6U;
            }
        }
    }
}

bool xx_7zip_branch_decode(uint64_t method, const uint8_t *properties,
                           size_t properties_size, const uint8_t *input,
                           size_t input_size, uint8_t *output,
                           size_t output_size) {
    uint32_t ip;
    if ((!properties && properties_size) || (!input && input_size) ||
        (!output && output_size) || input_size != output_size ||
        !xx_7zip_branch_method_supported(method, properties_size)) return false;
    if (output_size) xx_mem_copy(output, input, output_size);
    ip = xx_7zip_branch_start_ip(properties, properties_size);
    switch (method) {
        case XX_7ZIP_METHOD_BCJ: xx_7zip_filter_bcj(output, output_size, ip); break;
        case XX_7ZIP_METHOD_PPC: xx_7zip_filter_ppc(output, output_size, ip); break;
        case XX_7ZIP_METHOD_IA64: xx_7zip_filter_ia64(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARM: xx_7zip_filter_arm(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARMT: xx_7zip_filter_armt(output, output_size, ip); break;
        case XX_7ZIP_METHOD_SPARC: xx_7zip_filter_sparc(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARM64: xx_7zip_filter_arm64(output, output_size, ip); break;
        case XX_7ZIP_METHOD_RISCV: xx_7zip_filter_riscv(output, output_size, ip); break;
        default: return false;
    }
    return true;
}

static bool xx_7zip_bcj2_read_range_byte(xx_7zip_bcj2_decoder *decoder,
                                          uint8_t *value) {
    if (!decoder || !value || decoder->range_pos >= decoder->range_size) return false;
    *value = decoder->range_data[decoder->range_pos++];
    return true;
}

static bool xx_7zip_bcj2_bit(xx_7zip_bcj2_decoder *decoder, uint16_t *probability,
                              bool *bit) {
    uint32_t bound;
    uint8_t next;
    if (!decoder || !probability || !bit) return false;
    if (decoder->range < UINT32_C(1) << 24U) {
        if (!xx_7zip_bcj2_read_range_byte(decoder, &next)) return false;
        decoder->range <<= 8U;
        decoder->code = (decoder->code << 8U) | next;
    }
    bound = (decoder->range >> 11U) * *probability;
    if (decoder->code < bound) {
        decoder->range = bound;
        *probability = (uint16_t)(*probability + ((2048U - *probability) >> 5U));
        *bit = false;
    } else {
        decoder->range -= bound;
        decoder->code -= bound;
        *probability = (uint16_t)(*probability - (*probability >> 5U));
        *bit = true;
    }
    return true;
}

bool xx_7zip_bcj2_decode(const uint8_t *const inputs[4],
                         const size_t input_sizes[4], const uint8_t *properties,
                         size_t properties_size, uint8_t *output,
                         size_t output_size) {
    xx_7zip_bcj2_decoder decoder;
    size_t output_pos = 0U;
    size_t i;
    uint8_t first;
    if (!inputs || !input_sizes || (!properties && properties_size) ||
        (!output && output_size) || (!inputs[0] && input_sizes[0]) ||
        (!inputs[1] && input_sizes[1]) || (!inputs[2] && input_sizes[2]) ||
        (!inputs[3] && input_sizes[3]) || !xx_7zip_branch_properties_supported(properties_size) ||
        input_sizes[3] < 5U || (input_sizes[1] & 3U) != 0U ||
        (input_sizes[2] & 3U) != 0U) return false;
    xx_mem_zero(&decoder, sizeof(decoder));
    decoder.main_data = inputs[0]; decoder.main_size = input_sizes[0];
    decoder.call_data = inputs[1]; decoder.call_size = input_sizes[1];
    decoder.jump_data = inputs[2]; decoder.jump_size = input_sizes[2];
    decoder.range_data = inputs[3]; decoder.range_size = input_sizes[3];
    decoder.ip = xx_7zip_branch_start_ip(properties, properties_size);
    for (i = 0U; i < 258U; ++i) decoder.probabilities[i] = 1024U;
    if (!xx_7zip_bcj2_read_range_byte(&decoder, &first) || first != 0U) return false;
    for (i = 0U; i < 4U; ++i) {
        uint8_t next;
        if (!xx_7zip_bcj2_read_range_byte(&decoder, &next)) return false;
        decoder.code = (decoder.code << 8U) | next;
    }
    if (decoder.code == UINT32_MAX) return false;
    decoder.range = UINT32_MAX;
    while (decoder.main_pos < decoder.main_size) {
        uint8_t opcode = decoder.main_data[decoder.main_pos++];
        bool branch = opcode == 0xE8U || opcode == 0xE9U ||
                      (decoder.previous == 0x0FU && (opcode & 0xF0U) == 0x80U);
        if (output_pos >= output_size) return false;
        output[output_pos++] = opcode;
        ++decoder.ip;
        if (branch) {
            uint16_t *probability = opcode == 0xE8U
                ? &decoder.probabilities[2U + decoder.previous]
                : &decoder.probabilities[opcode == 0xE9U ? 1U : 0U];
            bool converted;
            if (!xx_7zip_bcj2_bit(&decoder, probability, &converted)) return false;
            if (converted) {
                const uint8_t *target;
                size_t *target_pos;
                size_t target_size;
                uint32_t value;
                if (opcode == 0xE8U) {
                    target = decoder.call_data; target_pos = &decoder.call_pos;
                    target_size = decoder.call_size;
                } else {
                    target = decoder.jump_data; target_pos = &decoder.jump_pos;
                    target_size = decoder.jump_size;
                }
                if (*target_pos > target_size || target_size - *target_pos < 4U ||
                    output_size - output_pos < 4U) return false;
                value = xx_7zip_branch_read_be32(target + *target_pos);
                *target_pos += 4U;
                decoder.ip += 4U;
                value -= decoder.ip;
                xx_7zip_branch_write_le32(output + output_pos, value);
                output_pos += 4U;
                decoder.previous = (uint8_t)(value >> 24U);
                continue;
            }
        }
        decoder.previous = opcode;
    }
    return output_pos == output_size && decoder.call_pos == decoder.call_size &&
           decoder.jump_pos == decoder.jump_size && decoder.code == 0U;
}
