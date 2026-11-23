/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ips.c @brief IPS and IPS32 patch parser, applier and creator. */

#include "xxfclib/formats/patch/ips/xx_ips.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include <string.h>

#define XX_IPS_MAX_RECORD_SIZE 0xFFFFU

static void set_err(char *error, size_t cap, const char *msg)
{
    if (error && cap > 0) {
        snprintf(error, cap, "%s", msg ? msg : "Unknown error");
    }
}

xx_ips_patch *xx_ips_patch_create(xx_ips_type type)
{
    xx_ips_patch *p = (xx_ips_patch *)xx_mem_alloc(sizeof(xx_ips_patch));
    if (!p) return NULL;
    xx_mem_zero(p, sizeof(*p));
    p->type = type;
    return p;
}

void xx_ips_patch_free(xx_ips_patch *patch)
{
    if (!patch) return;
    if (patch->records) {
        for (size_t i = 0; i < patch->record_count; ++i) {
            if (patch->records[i].data) {
                xx_mem_free(patch->records[i].data);
            }
        }
        xx_mem_free(patch->records);
    }
    xx_mem_free(patch);
}

static bool ensure_capacity(xx_ips_patch *patch, size_t needed)
{
    if (patch->record_capacity >= needed) return true;
    size_t cap = patch->record_capacity == 0 ? 16 : patch->record_capacity * 2;
    if (cap < needed) cap = needed;
    xx_ips_record *r = (xx_ips_record *)xx_mem_realloc(patch->records, cap * sizeof(xx_ips_record));
    if (!r) return false;
    patch->records = r;
    patch->record_capacity = cap;
    return true;
}

bool xx_ips_patch_add_record(xx_ips_patch *patch, uint64_t offset, const uint8_t *data, uint32_t size)
{
    if (!patch || size == 0 || !data) return false;
    if (!ensure_capacity(patch, patch->record_count + 1)) return false;

    uint8_t *copy = (uint8_t *)xx_mem_alloc(size);
    if (!copy) return false;
    xx_rt_memcpy(copy, data, size);

    xx_ips_record *rec = &patch->records[patch->record_count++];
    rec->offset = offset;
    rec->size = size;
    rec->rle_size = 0;
    rec->rle_value = 0;
    rec->data = copy;
    rec->is_rle = false;
    return true;
}

bool xx_ips_patch_add_rle_record(xx_ips_patch *patch, uint64_t offset, uint8_t value, uint32_t rle_size)
{
    if (!patch || rle_size == 0) return false;
    if (!ensure_capacity(patch, patch->record_count + 1)) return false;

    xx_ips_record *rec = &patch->records[patch->record_count++];
    rec->offset = offset;
    rec->size = 0;
    rec->rle_size = rle_size;
    rec->rle_value = value;
    rec->data = NULL;
    rec->is_rle = true;
    return true;
}

void xx_ips_patch_set_truncation(xx_ips_patch *patch, uint64_t truncate_length)
{
    if (!patch) return;
    patch->has_truncation = true;
    patch->truncate_length = truncate_length;
}

xx_ips_patch *xx_ips_parse_buffer(const uint8_t *data, size_t size, char *error, size_t error_cap)
{
    if (!data || size < 8) {
        set_err(error, error_cap, "Patch file is too small or invalid");
        return NULL;
    }

    xx_ips_type type;
    size_t cursor = 0;

    if (size >= 5 && memcmp(data, "PATCH", 5) == 0) {
        type = XX_IPS_TYPE_IPS;
        cursor = 5;
    } else if (size >= 5 && memcmp(data, "IPS32", 5) == 0) {
        type = XX_IPS_TYPE_IPS32;
        cursor = 5;
    } else {
        set_err(error, error_cap, "Invalid patch header magic (expected PATCH or IPS32)");
        return NULL;
    }

    xx_ips_patch *patch = xx_ips_patch_create(type);
    if (!patch) {
        set_err(error, error_cap, "Out of memory allocating patch structure");
        return NULL;
    }

    bool trailer_found = false;

    if (type == XX_IPS_TYPE_IPS) {
        while (cursor + 3 <= size) {
            uint32_t offset = ((uint32_t)data[cursor] << 16) |
                              ((uint32_t)data[cursor + 1] << 8) |
                              (uint32_t)data[cursor + 2];
            cursor += 3;

            if (offset == 0x454F46U) { /* "EOF" */
                trailer_found = true;
                size_t remain = size - cursor;
                if (remain == 3) {
                    uint32_t trunc = ((uint32_t)data[cursor] << 16) |
                                     ((uint32_t)data[cursor + 1] << 8) |
                                     (uint32_t)data[cursor + 2];
                    cursor += 3;
                    xx_ips_patch_set_truncation(patch, trunc);
                } else if (remain != 0) {
                    set_err(error, error_cap, "Corrupt IPS trailer: trailing bytes after EOF");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                break;
            }

            if (cursor + 2 > size) {
                set_err(error, error_cap, "Truncated IPS record size field");
                xx_ips_patch_free(patch);
                return NULL;
            }

            uint16_t rec_size = ((uint16_t)data[cursor] << 8) | (uint16_t)data[cursor + 1];
            cursor += 2;

            if (rec_size > 0) {
                if (cursor + rec_size > size) {
                    set_err(error, error_cap, "Truncated IPS record payload");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                if (!xx_ips_patch_add_record(patch, offset, data + cursor, rec_size)) {
                    set_err(error, error_cap, "Failed to record IPS data");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                cursor += rec_size;
            } else {
                /* RLE record */
                if (cursor + 3 > size) {
                    set_err(error, error_cap, "Truncated IPS RLE record header");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                uint16_t rle_size = ((uint16_t)data[cursor] << 8) | (uint16_t)data[cursor + 1];
                uint8_t rle_val = data[cursor + 2];
                cursor += 3;

                if (!xx_ips_patch_add_rle_record(patch, offset, rle_val, rle_size)) {
                    set_err(error, error_cap, "Failed to record IPS RLE data");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
            }
        }
    } else {
        /* IPS32 */
        while (cursor + 4 <= size) {
            uint32_t offset = ((uint32_t)data[cursor] << 24) |
                              ((uint32_t)data[cursor + 1] << 16) |
                              ((uint32_t)data[cursor + 2] << 8) |
                              (uint32_t)data[cursor + 3];
            cursor += 4;

            if (offset == 0x45454F46U) { /* "EEOF" */
                trailer_found = true;
                size_t remain = size - cursor;
                if (remain == 4) {
                    uint32_t trunc = ((uint32_t)data[cursor] << 24) |
                                     ((uint32_t)data[cursor + 1] << 16) |
                                     ((uint32_t)data[cursor + 2] << 8) |
                                     (uint32_t)data[cursor + 3];
                    cursor += 4;
                    xx_ips_patch_set_truncation(patch, trunc);
                } else if (remain != 0) {
                    set_err(error, error_cap, "Corrupt IPS32 trailer: trailing bytes after EEOF");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                break;
            }

            if (cursor + 2 > size) {
                set_err(error, error_cap, "Truncated IPS32 record size field");
                xx_ips_patch_free(patch);
                return NULL;
            }

            uint16_t rec_size = ((uint16_t)data[cursor] << 8) | (uint16_t)data[cursor + 1];
            cursor += 2;

            if (rec_size > 0) {
                if (cursor + rec_size > size) {
                    set_err(error, error_cap, "Truncated IPS32 record payload");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                if (!xx_ips_patch_add_record(patch, offset, data + cursor, rec_size)) {
                    set_err(error, error_cap, "Failed to record IPS32 data");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                cursor += rec_size;
            } else {
                /* RLE */
                if (cursor + 3 > size) {
                    set_err(error, error_cap, "Truncated IPS32 RLE record header");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
                uint16_t rle_size = ((uint16_t)data[cursor] << 8) | (uint16_t)data[cursor + 1];
                uint8_t rle_val = data[cursor + 2];
                cursor += 3;

                if (!xx_ips_patch_add_rle_record(patch, offset, rle_val, rle_size)) {
                    set_err(error, error_cap, "Failed to record IPS32 RLE data");
                    xx_ips_patch_free(patch);
                    return NULL;
                }
            }
        }
    }

    if (!trailer_found) {
        set_err(error, error_cap, "Missing patch EOF/EEOF trailer");
        xx_ips_patch_free(patch);
        return NULL;
    }

    return patch;
}

xx_ips_patch *xx_ips_parse_device(xx_io_device *dev, int64_t base_offset, xx_pd_struct *pd, char *error, size_t error_cap)
{
    (void)pd;
    if (!dev || base_offset < 0) {
        set_err(error, error_cap, "Invalid device or offset");
        return NULL;
    }
    int64_t total = xx_io_total_size(dev);
    if (total <= base_offset) {
        set_err(error, error_cap, "Device is empty or offset beyond end");
        return NULL;
    }
    int64_t size64 = total - base_offset;
    if (size64 > (int64_t)(100 * 1024 * 1024)) {
        set_err(error, error_cap, "Patch file exceeds 100 MiB limit");
        return NULL;
    }

    uint8_t *buf = (uint8_t *)xx_mem_alloc((size_t)size64);
    if (!buf) {
        set_err(error, error_cap, "Out of memory buffering patch file");
        return NULL;
    }

    if (xx_io_seek64(dev, base_offset, SEEK_SET) != 0 ||
        xx_io_read(dev, buf, (size_t)size64) != (ssize_t)size64) {
        set_err(error, error_cap, "Failed to read patch file from device");
        xx_mem_free(buf);
        return NULL;
    }

    xx_ips_patch *patch = xx_ips_parse_buffer(buf, (size_t)size64, error, error_cap);
    xx_mem_free(buf);
    return patch;
}

bool xx_ips_apply_buffer(const xx_ips_patch *patch, uint8_t **data, size_t *size, char *error, size_t error_cap)
{
    if (!patch || !data || !size) {
        set_err(error, error_cap, "Invalid arguments to xx_ips_apply_buffer");
        return false;
    }

    uint8_t *cur_data = *data;
    size_t cur_size = *size;

    for (size_t i = 0; i < patch->record_count; ++i) {
        const xx_ips_record *rec = &patch->records[i];
        uint64_t needed_end = rec->is_rle ? (rec->offset + rec->rle_size) : (rec->offset + rec->size);

        if (needed_end > (uint64_t)SIZE_MAX) {
            set_err(error, error_cap, "Record extends beyond memory limits");
            return false;
        }

        if ((size_t)needed_end > cur_size) {
            size_t new_size = (size_t)needed_end;
            uint8_t *reallocated = (uint8_t *)xx_mem_realloc(cur_data, new_size);
            if (!reallocated) {
                set_err(error, error_cap, "Out of memory expanding target buffer");
                *data = cur_data;
                *size = cur_size;
                return false;
            }
            /* Zero-fill any gap between old size and record offset */
            if (rec->offset > (uint64_t)cur_size) {
                xx_rt_memset(reallocated + cur_size, 0, (size_t)(rec->offset - (uint64_t)cur_size));
            }
            cur_data = reallocated;
            cur_size = new_size;
        }

        if (rec->is_rle) {
            xx_rt_memset(cur_data + rec->offset, rec->rle_value, rec->rle_size);
        } else {
            xx_rt_memcpy(cur_data + rec->offset, rec->data, rec->size);
        }
    }

    if (patch->has_truncation) {
        if ((uint64_t)cur_size > patch->truncate_length) {
            cur_size = (size_t)patch->truncate_length;
            uint8_t *truncated = (uint8_t *)xx_mem_realloc(cur_data, cur_size == 0 ? 1 : cur_size);
            if (truncated) cur_data = truncated;
        }
    }

    *data = cur_data;
    *size = cur_size;
    return true;
}

bool xx_ips_apply_device(const xx_ips_patch *patch, xx_io_device *target, xx_pd_struct *pd, char *error, size_t error_cap)
{
    (void)pd;
    if (!patch || !target) {
        set_err(error, error_cap, "Invalid device or patch");
        return false;
    }

    int64_t cur_len = xx_io_total_size(target);
    if (cur_len < 0) cur_len = 0;

    for (size_t i = 0; i < patch->record_count; ++i) {
        const xx_ips_record *rec = &patch->records[i];
        if (rec->offset > (uint64_t)cur_len) {
            /* Zero-fill gap */
            size_t gap = (size_t)(rec->offset - (uint64_t)cur_len);
            uint8_t zeroes[1024] = {0};
            if (xx_io_seek64(target, cur_len, SEEK_SET) != 0) return false;
            while (gap > 0) {
                size_t n = gap > sizeof(zeroes) ? sizeof(zeroes) : gap;
                if (xx_io_write(target, zeroes, n) != (ssize_t)n) return false;
                gap -= n;
            }
            cur_len = (int64_t)rec->offset;
        }

        if (xx_io_seek64(target, (int64_t)rec->offset, SEEK_SET) != 0) return false;

        if (rec->is_rle) {
            uint8_t val = rec->rle_value;
            uint8_t rle_buf[1024];
            memset(rle_buf, val, sizeof(rle_buf));
            size_t rem = rec->rle_size;
            while (rem > 0) {
                size_t n = rem > sizeof(rle_buf) ? sizeof(rle_buf) : rem;
                if (xx_io_write(target, rle_buf, n) != (ssize_t)n) return false;
                rem -= n;
            }
            if ((int64_t)(rec->offset + rec->rle_size) > cur_len) {
                cur_len = (int64_t)(rec->offset + rec->rle_size);
            }
        } else {
            if (xx_io_write(target, rec->data, rec->size) != (ssize_t)rec->size) return false;
            if ((int64_t)(rec->offset + rec->size) > cur_len) {
                cur_len = (int64_t)(rec->offset + rec->size);
            }
        }
    }

    if (patch->has_truncation && (uint64_t)cur_len > patch->truncate_length) {
        if (!xx_io_resize(target, (int64_t)patch->truncate_length)) {
            set_err(error, error_cap, "Failed to truncate output device");
            return false;
        }
    }

    return true;
}

bool xx_ips_write_device(const xx_ips_patch *patch, xx_io_device *dest, xx_pd_struct *pd, char *error, size_t error_cap)
{
    (void)pd;
    if (!patch || !dest) {
        set_err(error, error_cap, "Invalid arguments to xx_ips_write_device");
        return false;
    }

    const char *magic = (patch->type == XX_IPS_TYPE_IPS32) ? "IPS32" : "PATCH";
    if (xx_io_write(dest, (const uint8_t *)magic, 5) != 5) {
        set_err(error, error_cap, "Failed writing patch header magic");
        return false;
    }

    for (size_t i = 0; i < patch->record_count; ++i) {
        const xx_ips_record *rec = &patch->records[i];

        if (patch->type == XX_IPS_TYPE_IPS) {
            uint8_t off_bytes[3];
            off_bytes[0] = (uint8_t)((rec->offset >> 16) & 0xFFU);
            off_bytes[1] = (uint8_t)((rec->offset >> 8) & 0xFFU);
            off_bytes[2] = (uint8_t)(rec->offset & 0xFFU);
            if (xx_io_write(dest, off_bytes, 3) != 3) return false;
        } else {
            uint8_t off_bytes[4];
            off_bytes[0] = (uint8_t)((rec->offset >> 24) & 0xFFU);
            off_bytes[1] = (uint8_t)((rec->offset >> 16) & 0xFFU);
            off_bytes[2] = (uint8_t)((rec->offset >> 8) & 0xFFU);
            off_bytes[3] = (uint8_t)(rec->offset & 0xFFU);
            if (xx_io_write(dest, off_bytes, 4) != 4) return false;
        }

        if (rec->is_rle) {
            uint8_t rle_hdr[3];
            rle_hdr[0] = (uint8_t)((rec->rle_size >> 8) & 0xFFU);
            rle_hdr[1] = (uint8_t)(rec->rle_size & 0xFFU);
            rle_hdr[2] = rec->rle_value;

            uint8_t zero_size[2] = {0, 0};
            if (xx_io_write(dest, zero_size, 2) != 2) return false;
            if (xx_io_write(dest, rle_hdr, 3) != 3) return false;
        } else {
            uint8_t size_bytes[2];
            size_bytes[0] = (uint8_t)((rec->size >> 8) & 0xFFU);
            size_bytes[1] = (uint8_t)(rec->size & 0xFFU);
            if (xx_io_write(dest, size_bytes, 2) != 2) return false;
            if (xx_io_write(dest, rec->data, rec->size) != (ssize_t)rec->size) return false;
        }
    }

    if (patch->type == XX_IPS_TYPE_IPS) {
        if (xx_io_write(dest, (const uint8_t *)"EOF", 3) != 3) return false;
        if (patch->has_truncation) {
            uint8_t trunc_bytes[3];
            trunc_bytes[0] = (uint8_t)((patch->truncate_length >> 16) & 0xFFU);
            trunc_bytes[1] = (uint8_t)((patch->truncate_length >> 8) & 0xFFU);
            trunc_bytes[2] = (uint8_t)(patch->truncate_length & 0xFFU);
            if (xx_io_write(dest, trunc_bytes, 3) != 3) return false;
        }
    } else {
        if (xx_io_write(dest, (const uint8_t *)"EEOF", 4) != 4) return false;
        if (patch->has_truncation) {
            uint8_t trunc_bytes[4];
            trunc_bytes[0] = (uint8_t)((patch->truncate_length >> 24) & 0xFFU);
            trunc_bytes[1] = (uint8_t)((patch->truncate_length >> 16) & 0xFFU);
            trunc_bytes[2] = (uint8_t)((patch->truncate_length >> 8) & 0xFFU);
            trunc_bytes[3] = (uint8_t)(patch->truncate_length & 0xFFU);
            if (xx_io_write(dest, trunc_bytes, 4) != 4) return false;
        }
    }

    return true;
}

xx_ips_patch *xx_ips_create_from_buffers(const uint8_t *orig, size_t orig_size,
                                         const uint8_t *mod, size_t mod_size,
                                         bool force_ips32, char *error, size_t error_cap)
{
    bool need_ips32 = force_ips32;
    xx_ips_patch *patch = xx_ips_patch_create(XX_IPS_TYPE_IPS);
    if (!patch) {
        set_err(error, error_cap, "Out of memory creating patch structure");
        return NULL;
    }

    size_t pos = 0;
    while (pos < mod_size) {
        /* Skip identical bytes */
        if (pos < orig_size && orig[pos] == mod[pos]) {
            ++pos;
            continue;
        }

        size_t diff_start = pos;
        /* Find diff run end: stop if there is a match of >= 8 contiguous bytes */
        while (pos < mod_size) {
            if (pos < orig_size && orig[pos] == mod[pos]) {
                size_t match_len = 0;
                while (pos + match_len < mod_size && pos + match_len < orig_size &&
                       orig[pos + match_len] == mod[pos + match_len]) {
                    ++match_len;
                }
                if (match_len >= 8) {
                    break;
                }
                pos += match_len;
            } else {
                ++pos;
            }
        }
        size_t diff_end = pos;

        /* Emit records for diff_start .. diff_end */
        size_t cur = diff_start;
        while (cur < diff_end) {
            uint64_t rec_offset = (uint64_t)cur;
            if (rec_offset >= 0x01000000ULL || rec_offset == 0x454F46ULL) {
                need_ips32 = true;
            }

            /* Check if cur starts a run of repeated identical bytes */
            uint8_t val = mod[cur];
            size_t rle_len = 0;
            while (cur + rle_len < diff_end && mod[cur + rle_len] == val && rle_len < XX_IPS_MAX_RECORD_SIZE) {
                ++rle_len;
            }

            /* Use RLE when shorter:
             * Standalone chunk: RLE is 8 bytes in IPS, raw is 5 + N. When N >= 4, RLE is shorter.
             * If not all identical, but starts with >= 16 repeated bytes: split into RLE.
             */
            if (rle_len >= 4 && (rle_len == (diff_end - cur) || rle_len >= 16)) {
                xx_ips_patch_add_rle_record(patch, rec_offset, val, (uint32_t)rle_len);
                cur += rle_len;
            } else {
                /* Emit raw record up to min(XX_IPS_MAX_RECORD_SIZE, remaining) */
                size_t chunk = diff_end - cur;
                if (chunk > XX_IPS_MAX_RECORD_SIZE) chunk = XX_IPS_MAX_RECORD_SIZE;

                /* If there is a long repeated run within chunk, stop before it so RLE can take over */
                for (size_t look = 1; look < chunk; ++look) {
                    if (chunk - look >= 16) {
                        uint8_t b = mod[cur + look];
                        size_t rep = 0;
                        while (look + rep < chunk && mod[cur + look + rep] == b) ++rep;
                        if (rep >= 16) {
                            chunk = look;
                            break;
                        }
                    }
                }

                xx_ips_patch_add_record(patch, rec_offset, mod + cur, (uint32_t)chunk);
                cur += chunk;
            }
        }
    }

    if (mod_size < orig_size) {
        xx_ips_patch_set_truncation(patch, mod_size);
        if (mod_size >= 0x01000000ULL) {
            need_ips32 = true;
        }
    }

    if (need_ips32) {
        patch->type = XX_IPS_TYPE_IPS32;
    }

    return patch;
}

xx_ips_patch *xx_ips_create_from_devices(xx_io_device *orig, xx_io_device *mod,
                                         bool force_ips32, xx_pd_struct *pd,
                                         char *error, size_t error_cap)
{
    (void)pd;
    if (!orig || !mod) {
        set_err(error, error_cap, "Invalid device input to xx_ips_create_from_devices");
        return NULL;
    }

    int64_t orig_sz = xx_io_total_size(orig);
    int64_t mod_sz = xx_io_total_size(mod);
    if (orig_sz < 0) orig_sz = 0;
    if (mod_sz < 0) mod_sz = 0;

    uint8_t *orig_buf = (orig_sz > 0) ? (uint8_t *)xx_mem_alloc((size_t)orig_sz) : NULL;
    uint8_t *mod_buf = (mod_sz > 0) ? (uint8_t *)xx_mem_alloc((size_t)mod_sz) : NULL;

    if (orig_sz > 0 && (!orig_buf || xx_io_seek64(orig, 0, SEEK_SET) != 0 ||
        xx_io_read(orig, orig_buf, (size_t)orig_sz) != (ssize_t)orig_sz)) {
        if (orig_buf) xx_mem_free(orig_buf);
        if (mod_buf) xx_mem_free(mod_buf);
        set_err(error, error_cap, "Failed to read original device");
        return NULL;
    }

    if (mod_sz > 0 && (!mod_buf || xx_io_seek64(mod, 0, SEEK_SET) != 0 ||
        xx_io_read(mod, mod_buf, (size_t)mod_sz) != (ssize_t)mod_sz)) {
        if (orig_buf) xx_mem_free(orig_buf);
        if (mod_buf) xx_mem_free(mod_buf);
        set_err(error, error_cap, "Failed to read modified device");
        return NULL;
    }

    xx_ips_patch *p = xx_ips_create_from_buffers(orig_buf, (size_t)orig_sz,
                                                 mod_buf, (size_t)mod_sz,
                                                 force_ips32, error, error_cap);
    if (orig_buf) xx_mem_free(orig_buf);
    if (mod_buf) xx_mem_free(mod_buf);
    return p;
}
