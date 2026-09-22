/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xx_crc_internal.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_pd.h"
#include <string.h>
#include <ctype.h>

/* Complete catalogue of standard RevEng / Williams CRC models */
static const xx_crc_model _CRC_MODELS[] = {
    {0, 0, 0, false, false, 0, "UNKNOWN"},

    /* CRC-8 */
    {8, 0x07, 0x00, false, false, 0x00, "CRC-8"},
    {8, 0x9B, 0xFF, false, false, 0x00, "CRC-8/CDMA2000"},
    {8, 0x39, 0x00, true,  true,  0x00, "CRC-8/DARC"},
    {8, 0xD5, 0x00, false, false, 0x00, "CRC-8/DVB-S2"},
    {8, 0x1D, 0xFF, true,  true,  0x00, "CRC-8/EBU"},
    {8, 0x1D, 0xFD, false, false, 0x00, "CRC-8/I-CODE"},
    {8, 0x07, 0x00, false, false, 0x55, "CRC-8/ITU"},
    {8, 0x31, 0x00, true,  true,  0x00, "CRC-8/MAXIM"},
    {8, 0x07, 0xFF, true,  true,  0x00, "CRC-8/ROHC"},
    {8, 0x9B, 0x00, true,  true,  0x00, "CRC-8/WCDMA"},
    {8, 0xA7, 0x00, true,  true,  0x00, "CRC-8/BLUETOOTH"},
    {8, 0x2F, 0xFF, false, false, 0xFF, "CRC-8/AUTOSAR"},

    /* CRC-16 */
    {16, 0x8005, 0x0000, true,  true,  0x0000, "CRC-16/ARC"},
    {16, 0x1021, 0xFFFF, false, false, 0x0000, "CRC-16/CCITT-FALSE"},
    {16, 0x1021, 0x1D0F, false, false, 0x0000, "CRC-16/AUG-CCITT"},
    {16, 0x8005, 0x0000, false, false, 0x0000, "CRC-16/BUYPASS"},
    {16, 0xC867, 0xFFFF, false, false, 0x0000, "CRC-16/CDMA2000"},
    {16, 0x8005, 0x800D, false, false, 0x0000, "CRC-16/DDS-110"},
    {16, 0x0589, 0x0000, false, false, 0x0001, "CRC-16/DECT-R"},
    {16, 0x0589, 0x0000, false, false, 0x0000, "CRC-16/DECT-X"},
    {16, 0x3D65, 0x0000, true,  true,  0xFFFF, "CRC-16/DNP"},
    {16, 0x3D65, 0x0000, false, false, 0xFFFF, "CRC-16/EN-13757"},
    {16, 0x1021, 0xFFFF, false, false, 0xFFFF, "CRC-16/GENIBUS"},
    {16, 0x8005, 0x0000, true,  true,  0xFFFF, "CRC-16/MAXIM"},
    {16, 0x1021, 0xFFFF, true,  true,  0x0000, "CRC-16/MCRF4XX"},
    {16, 0x8005, 0xFFFF, true,  true,  0x0000, "CRC-16/MODBUS"},
    {16, 0x1021, 0xB2AA, true,  true,  0x0000, "CRC-16/RIELLO"},
    {16, 0x8BB7, 0x0000, false, false, 0x0000, "CRC-16/T10-DIF"},
    {16, 0xA097, 0x0000, false, false, 0x0000, "CRC-16/TELEDISK"},
    {16, 0x1021, 0x89EC, true,  true,  0x0000, "CRC-16/TMS37157"},
    {16, 0x8005, 0xFFFF, true,  true,  0xFFFF, "CRC-16/USB"},
    {16, 0x1021, 0xFFFF, true,  true,  0xFFFF, "CRC-16/X-25"},
    {16, 0x1021, 0x0000, false, false, 0x0000, "CRC-16/XMODEM"},
    {16, 0x1021, 0x0000, true,  true,  0x0000, "CRC-16/KERMIT"},

    /* CRC-32 */
    {32, 0x04C11DB7, 0xFFFFFFFF, true,  true,  0xFFFFFFFF, "CRC-32"},
    {32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0xFFFFFFFF, "CRC-32/BZIP2"},
    {32, 0x1EDC6F41, 0xFFFFFFFF, true,  true,  0xFFFFFFFF, "CRC-32C"},
    {32, 0xA833982B, 0xFFFFFFFF, true,  true,  0xFFFFFFFF, "CRC-32D"},
    {32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0x00000000, "CRC-32/MPEG-2"},
    {32, 0x04C11DB7, 0x00000000, false, false, 0xFFFFFFFF, "CRC-32/POSIX"},
    {32, 0x814141AB, 0x00000000, false, false, 0x00000000, "CRC-32Q"},
    {32, 0x04C11DB7, 0xFFFFFFFF, true,  true,  0x00000000, "CRC-32/JAMCRC"},
    {32, 0x000000AF, 0x00000000, false, false, 0x00000000, "CRC-32/XFER"},

    /* CRC-64 */
    {64, 0x42F0E1EBA9EA3693ULL, 0x0000000000000000ULL, false, false, 0x0000000000000000ULL, "CRC-64/ECMA-182"},
    {64, 0x000000000000001BULL, 0xFFFFFFFFFFFFFFFFULL, true,  true,  0xFFFFFFFFFFFFFFFFULL, "CRC-64/GO-ISO"},
    {64, 0x42F0E1EBA9EA3693ULL, 0xFFFFFFFFFFFFFFFFULL, false, false, 0xFFFFFFFFFFFFFFFFULL, "CRC-64/WE"},
    {64, 0x42F0E1EBA9EA3693ULL, 0xFFFFFFFFFFFFFFFFULL, true,  true,  0xFFFFFFFFFFFFFFFFULL, "CRC-64/XZ"},
};

#define _CRC_MODELS_COUNT (sizeof(_CRC_MODELS) / sizeof(_CRC_MODELS[0]))

const xx_crc_model *xx_crc_get_model(xx_crc_type_t type) {
    if ((size_t)type < _CRC_MODELS_COUNT) {
        return &_CRC_MODELS[type];
    }
    return NULL;
}

const char *xx_crc_type_to_string(xx_crc_type_t type) {
    const xx_crc_model *m = xx_crc_get_model(type);
    return m ? m->name : "UNKNOWN";
}

static void _normalize_name(const char *src, char *dst, size_t dst_size) {
    size_t d = 0;
    for (size_t s = 0; src && src[s] && d + 1 < dst_size; ++s) {
        char c = src[s];
        if (c != '-' && c != '_' && c != '/' && c != ' ' && c != '.') {
            dst[d++] = (char)xx_rt_ascii_tolower((unsigned char)c);
        }
    }
    dst[d] = '\0';
}

xx_crc_type_t xx_crc_string_to_type(const char *name) {
    if (!name || !name[0]) {
        return XX_CRC_TYPE_UNKNOWN;
    }
    char norm_search[64];
    _normalize_name(name, norm_search, sizeof(norm_search));

    for (size_t i = 1; i < _CRC_MODELS_COUNT; ++i) {
        char norm_entry[64];
        _normalize_name(_CRC_MODELS[i].name, norm_entry, sizeof(norm_entry));
        if (xx_rt_strcmp(norm_search, norm_entry) == 0) {
            return (xx_crc_type_t)i;
        }
    }

    /* Common aliases */
    if (xx_rt_strcmp(norm_search, "crc8smbus") == 0) return XX_CRC_TYPE_CRC8;
    if (xx_rt_strcmp(norm_search, "crc16") == 0) return XX_CRC_TYPE_CRC16_ARC;
    if (xx_rt_strcmp(norm_search, "crc16arc") == 0) return XX_CRC_TYPE_CRC16_ARC;
    if (xx_rt_strcmp(norm_search, "crc32") == 0) return XX_CRC_TYPE_CRC32;
    if (xx_rt_strcmp(norm_search, "crc32pkzip") == 0) return XX_CRC_TYPE_CRC32;
    if (xx_rt_strcmp(norm_search, "crc32isohdlc") == 0) return XX_CRC_TYPE_CRC32;
    if (xx_rt_strcmp(norm_search, "crc64") == 0) return XX_CRC_TYPE_CRC64_XZ;
    if (xx_rt_strcmp(norm_search, "crc64xz") == 0) return XX_CRC_TYPE_CRC64_XZ;

    return XX_CRC_TYPE_UNKNOWN;
}

/* Build 256-entry lookup table for a given model */
static void xx_crc_build_table(const xx_crc_model *model, uint64_t table[256]) {
    uint8_t width = model->width;
    uint64_t mask = xx_crc_mask(width);

    if (model->refin) {
        uint64_t rpoly = xx_crc_reflect(model->poly, width);
        for (int i = 0; i < 256; ++i) {
            uint64_t reg = (uint64_t)i;
            for (int bit = 0; bit < 8; ++bit) {
                if (reg & 1ULL) {
                    reg = (reg >> 1) ^ rpoly;
                } else {
                    reg >>= 1;
                }
            }
            table[i] = reg & mask;
        }
    } else {
        uint64_t topbit = 1ULL << (width - 1);
        int shift = (int)width - 8;
        for (int i = 0; i < 256; ++i) {
            uint64_t reg = (shift >= 0) ? (((uint64_t)i << shift) & mask) : (((uint64_t)i >> -shift) & mask);
            for (int bit = 0; bit < 8; ++bit) {
                if (reg & topbit) {
                    reg = ((reg << 1) ^ model->poly) & mask;
                } else {
                    reg = (reg << 1) & mask;
                }
            }
            table[i] = reg & mask;
        }
    }
}

/* ========================================================================= */
/* --- Streaming Context Implementation                                  --- */
/* ========================================================================= */

bool xx_crc_context_init(xx_crc_context *ctx, const xx_crc_model *model) {
    if (!ctx || !model || model->width == 0 || model->width > 64) {
        return false;
    }
    ctx->model = *model;
    xx_crc_build_table(&ctx->model, ctx->table);
    xx_crc_context_reset(ctx);
    ctx->initialized = true;
    return true;
}

bool xx_crc_context_init_type(xx_crc_context *ctx, xx_crc_type_t type) {
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m || m->width == 0) {
        return false;
    }
    return xx_crc_context_init(ctx, m);
}

void xx_crc_context_reset(xx_crc_context *ctx) {
    if (!ctx) return;
    uint8_t width = ctx->model.width;
    uint64_t mask = xx_crc_mask(width);
    if (ctx->model.refin) {
        ctx->state = xx_crc_reflect(ctx->model.init, width) & mask;
    } else {
        ctx->state = ctx->model.init & mask;
    }
}

void xx_crc_context_update(xx_crc_context *ctx, const void *data, size_t size) {
    if (!ctx || !ctx->initialized || !data || size == 0) {
        return;
    }
    const uint8_t *p = (const uint8_t *)data;
    uint8_t width = ctx->model.width;
    uint64_t mask = xx_crc_mask(width);
    uint64_t reg = ctx->state;

    if (ctx->model.refin) {
        for (size_t i = 0; i < size; ++i) {
            uint8_t idx = (uint8_t)((reg ^ p[i]) & 0xFF);
            reg = ((reg >> 8) ^ ctx->table[idx]) & mask;
        }
    } else {
        int shift = (int)width - 8;
        for (size_t i = 0; i < size; ++i) {
            uint8_t idx = (uint8_t)(((reg >> shift) ^ p[i]) & 0xFF);
            reg = ((reg << 8) ^ ctx->table[idx]) & mask;
        }
    }
    ctx->state = reg;
}

uint64_t xx_crc_context_final(const xx_crc_context *ctx) {
    if (!ctx || !ctx->initialized) {
        return 0;
    }
    uint8_t width = ctx->model.width;
    uint64_t mask = xx_crc_mask(width);
    uint64_t reg = ctx->state;

    if (ctx->model.refin) {
        if (!ctx->model.refout) {
            reg = xx_crc_reflect(reg, width);
        }
    } else {
        if (ctx->model.refout) {
            reg = xx_crc_reflect(reg, width);
        }
    }
    return (reg ^ ctx->model.xorout) & mask;
}

/* ========================================================================= */
/* --- Calculation Engine                                                --- */
/* ========================================================================= */

uint64_t xx_crc_calculate(const xx_crc_model *model, const void *data, size_t size) {
    if (!model || model->width == 0 || model->width > 64) {
        return 0;
    }
    xx_crc_context ctx;
    if (!xx_crc_context_init(&ctx, model)) {
        return 0;
    }
    if (data && size > 0) {
        xx_crc_context_update(&ctx, data, size);
    }
    return xx_crc_context_final(&ctx);
}

bool xx_crc_calculate_by_type(xx_crc_type_t type, const void *data, size_t size, uint64_t *out_crc) {
    if (!out_crc) {
        return false;
    }
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m || m->width == 0) {
        return false;
    }
    switch (m->width) {
        case 8:
            *out_crc = xx_crc8(type, data, size);
            break;
        case 16:
            *out_crc = xx_crc16(type, data, size);
            break;
        case 32:
            *out_crc = xx_crc32(type, data, size);
            break;
        case 64:
            *out_crc = xx_crc64(type, data, size);
            break;
        default:
            *out_crc = xx_crc_calculate(m, data, size);
            break;
    }
    return true;
}

uint8_t xx_crc8(xx_crc_type_t type, const void *data, size_t size) {
    if (xx_crc8_has_fast(type)) {
        return xx_crc8_fast(type, data, size);
    }
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m) return 0;
    return (uint8_t)xx_crc_calculate(m, data, size);
}

uint16_t xx_crc16(xx_crc_type_t type, const void *data, size_t size) {
    if (xx_crc16_has_fast(type)) {
        return xx_crc16_fast(type, data, size);
    }
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m) return 0;
    return (uint16_t)xx_crc_calculate(m, data, size);
}

uint32_t xx_crc32(xx_crc_type_t type, const void *data, size_t size) {
    if (xx_crc32_has_fast(type)) {
        return xx_crc32_fast(type, data, size);
    }
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m) return 0;
    return (uint32_t)xx_crc_calculate(m, data, size);
}

uint64_t xx_crc64(xx_crc_type_t type, const void *data, size_t size) {
    if (xx_crc64_has_fast(type)) {
        return xx_crc64_fast(type, data, size);
    }
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m) return 0;
    return xx_crc_calculate(m, data, size);
}

/* ========================================================================= */
/* --- Device Calculation                                                --- */
/* ========================================================================= */

bool xx_crc_calculate_device(xx_io_device *dev, int64_t offset, int64_t size,
                             const xx_crc_model *model, xx_pd_struct *pd, uint64_t *out_crc) {
    if (!dev || !model || !out_crc) {
        return false;
    }

    int64_t dev_size = xx_io_total_size(dev);
    if (offset < 0) {
        offset = 0;
    }
    if (offset > dev_size && dev_size >= 0) {
        return false;
    }

    int64_t total_to_read = size;
    if (total_to_read < 0) {
        if (dev_size < offset) {
            return false;
        }
        total_to_read = dev_size - offset;
    } else if (dev_size >= 0 && total_to_read > dev_size - offset) {
        return false;
    }

    if (xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }

    xx_crc_context ctx;
    if (!xx_crc_context_init(&ctx, model)) {
        return false;
    }

    uint8_t buffer[8192];
    int64_t remaining = total_to_read;
    int64_t processed = 0;

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, (uint64_t)total_to_read, "Calculating CRC");
    }

    while (remaining > 0) {
        if (pd && xx_pd_is_stopped(pd)) {
            if (pd_level >= 0) {
                xx_pd_leave_level(pd, pd_level);
            }
            return false;
        }

        size_t chunk = (remaining > (int64_t)sizeof(buffer)) ? sizeof(buffer) : (size_t)remaining;
        ssize_t n = xx_io_read(dev, buffer, chunk);
        if (n <= 0 || (size_t)n > chunk) {
            break;
        }

        xx_crc_context_update(&ctx, buffer, (size_t)n);
        remaining -= n;
        processed += n;

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)processed);
        }
    }

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    if (remaining != 0) {
        return false;
    }
    *out_crc = xx_crc_context_final(&ctx);
    return true;
}

bool xx_crc_calculate_device_by_type(xx_io_device *dev, int64_t offset, int64_t size,
                                     xx_crc_type_t type, xx_pd_struct *pd, uint64_t *out_crc) {
    const xx_crc_model *m = xx_crc_get_model(type);
    if (!m) {
        return false;
    }
    return xx_crc_calculate_device(dev, offset, size, m, pd, out_crc);
}

bool xx_crc_verify_device(xx_io_device *dev, int64_t offset, int64_t size,
                          xx_crc_type_t type, uint64_t expected_crc,
                          xx_pd_struct *pd) {
    uint64_t actual_crc = 0;
    if (!xx_crc_calculate_device_by_type(dev, offset, size, type, pd, &actual_crc)) {
        return false;
    }
    return (actual_crc == expected_crc);
}

bool xx_crc_copy_device_verify(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                               xx_io_device *dst_dev, xx_crc_type_t type,
                               uint64_t expected_crc, bool *out_match, xx_pd_struct *pd) {
    if (out_match) {
        *out_match = false;
    }
    if (!src_dev || !dst_dev || size < 0) {
        return false;
    }
    if (size == 0) {
        bool match = (expected_crc == 0);
        if (out_match) *out_match = match;
        return match;
    }

    if (src_offset >= 0) {
        if (xx_io_seek64(src_dev, src_offset, SEEK_SET) != 0) {
            return false;
        }
    }

    xx_crc_context ctx;
    if (!xx_crc_context_init_type(&ctx, type)) {
        return false;
    }

    int pd_level = -1;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, (uint64_t)size, "Streaming device with CRC verification");
    }

    uint8_t buffer[8192];
    int64_t remaining = size;
    int64_t processed = 0;
    bool success = true;

    while (remaining > 0) {
        if (pd && xx_pd_is_stopped(pd)) {
            success = false;
            break;
        }

        size_t chunk = (remaining > (int64_t)sizeof(buffer)) ? sizeof(buffer) : (size_t)remaining;
        ssize_t n_read = xx_io_read(src_dev, buffer, chunk);
        if (n_read <= 0 || (size_t)n_read > chunk) {
            success = false;
            break;
        }

        ssize_t n_written = xx_io_write(dst_dev, buffer, (size_t)n_read);
        if (n_written != n_read) {
            success = false;
            break;
        }

        xx_crc_context_update(&ctx, buffer, (size_t)n_read);
        remaining -= n_read;
        processed += n_read;

        if (pd && pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, (uint64_t)processed);
        }
    }

    if (pd && pd_level >= 0) {
        xx_pd_leave_level(pd, pd_level);
    }

    uint64_t actual_crc = xx_crc_context_final(&ctx);
    bool matched = (actual_crc == expected_crc);
    if (out_match) {
        *out_match = matched;
    }

    return success && (remaining == 0) && matched;
}
