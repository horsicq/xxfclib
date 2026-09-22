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
#include "xxfclib/formats/rar/xx_rar.h"
#include "xx_rar_defs.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/aes/xx_aes.h"
#include "xxfclib/algo/blake2/xx_blake2.h"
#include "xxfclib/algo/rar/xx_rarx.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Reserved device names and OS entropy live behind the io platform layer. */
#include "../../io/platforms/xx_io_platform.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const uint8_t g_xx_rar4_signature[XX_RAR4_SIGNATURE_SIZE] = {
    0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00
};
static const uint8_t g_xx_rar5_signature[XX_RAR5_SIGNATURE_SIZE] = {
    0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00
};

typedef struct xx_rar_block_s {
    xx_rar_version_t version;
    int64_t offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t data_size;
    int64_t next_offset;
    uint64_t type;
    uint64_t flags;
    uint64_t extra_size;
    int64_t header_size_rel;
    int64_t type_rel;
    int64_t flags_rel;
    int64_t extra_size_rel;
    int64_t data_size_rel;
    uint8_t header_size_width;
    uint8_t type_width;
    uint8_t flags_width;
    uint8_t extra_size_width;
    uint8_t data_size_width;
} xx_rar_block;

typedef struct xx_rar_file_info_s {
    xx_rar_block block;
    uint64_t unpacked_size;
    uint64_t attributes;
    uint64_t timestamp;
    uint64_t compression_info;
    uint32_t data_crc;
    uint32_t file_flags;
    uint32_t method;
    uint32_t unpack_version;
    uint32_t host_os;
    xx_rarx_method_t rarx_method;
    uint64_t window_size;
    int64_t name_offset;
    size_t name_size;
    bool has_crc;
    bool has_blake2;
    uint8_t blake2[32];
    bool is_folder;
    bool is_encrypted;
    bool is_split;
    bool unknown_size;
    bool name_is_utf8;
    bool rar4_unicode_name;
    bool solid;
    uint8_t crypto_version;
    uint8_t crypto_flags;
    uint8_t crypto_log;
    uint8_t crypto_salt[16];
    uint8_t crypto_iv[16];
    uint8_t crypto_check[12];
} xx_rar_file_info;

static bool xx_rar_normalize_codec(xx_rar_file_info *info) {
    uint64_t exponent;
    if (!info) return false;
    info->rarx_method = 0;
    info->window_size = 0;
    if (info->block.version == XX_RAR_VERSION_4) {
        info->solid = (info->file_flags & XX_RAR4_FILE_FLAG_SOLID) != 0;
        if (info->method == XX_RAR4_METHOD_STORE || info->is_folder) return true;
        if (info->method < 0x31U || info->method > 0x35U) return false;
        if (info->unpack_version <= 15U) {
            info->rarx_method = XX_RARX_METHOD_15;
            info->window_size = 64U * 1024U;
        } else if (info->unpack_version <= 26U) {
            info->rarx_method = XX_RARX_METHOD_20;
            info->window_size = 0x10000ULL << ((info->file_flags >> 5) & 7U);
        } else if (info->unpack_version <= 40U) {
            info->rarx_method = XX_RARX_METHOD_29;
            info->window_size = 0x10000ULL << ((info->file_flags >> 5) & 7U);
        } else {
            return false;
        }
        return true;
    }

    info->solid = (info->compression_info & XX_RAR5_COMP_SOLID) != 0;
    if (info->method == XX_RAR5_METHOD_STORE || info->is_folder) return true;
    if (info->method > 5U) return false;
    switch (info->compression_info & XX_RAR5_COMP_VERSION_MASK) {
        case 0: info->rarx_method = XX_RARX_METHOD_50; break;
        case 1: info->rarx_method = XX_RARX_METHOD_70; break;
        default: return false;
    }
    exponent = (info->compression_info >> XX_RAR5_COMP_DICTIONARY_SHIFT) &
               XX_RAR5_COMP_DICTIONARY_MASK;
    info->window_size = 0x20000ULL << exponent;
    if (info->rarx_method == XX_RARX_METHOD_70 &&
        (info->compression_info & 0x8000U) != 0) {
        if (exponent == 0) return false;
        info->window_size = 3ULL * (0x20000ULL << (exponent - 1));
    }
    return true;
}

static void xx_rar_vtable_destroy(Abstractformat *self);

static bool xx_rar_range_valid(int64_t device_size, int64_t offset, int64_t size) {
    return device_size >= 0 && offset >= 0 && size >= 0 &&
           offset <= device_size && size <= device_size - offset;
}

static bool xx_rar_add_i64(int64_t left, int64_t right, int64_t *result) {
    if (!result || right < 0 || left < 0 || left > INT64_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool xx_rar_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0;
    if (!device || (!buffer && size != 0) || offset < 0) {
        return false;
    }
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0 || (size_t)got > size - done) {
            return false;
        }
        done += (size_t)got;
    }
    return true;
}

static void xx_rar_secure_clear(void *buffer, size_t size) {
    volatile uint8_t *bytes = (volatile uint8_t *)buffer;
    if (bytes) while (size--) *bytes++ = 0;
}

static bool xx_rar_read_member_at(xx_io_device *device, int64_t offset,
                                  uint8_t *buffer, size_t size, xx_pd_struct *pd) {
    size_t done = 0;
    if (offset < 0 || (uint64_t)size > (uint64_t)(INT64_MAX-offset)) return false;
    while (done < size) {
        size_t n = size-done > 65536U ? 65536U : size-done;
        if (xx_pd_is_stopped(pd) ||
            !xx_rar_read_exact_at(device, offset+(int64_t)done, buffer+done, n)) return false;
        done += n;
    }
    return !xx_pd_is_stopped(pd);
}

static bool xx_rar_read_vint(xx_io_device *device, int64_t *position, int64_t limit,
                             uint64_t *value, uint8_t *width) {
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t count = 0;

    if (!device || !position || !value || *position < 0 || limit < *position) {
        return false;
    }
    while (count < 10) {
        uint8_t byte_value = 0;
        if (*position >= limit || !xx_rar_read_exact_at(device, *position, &byte_value, 1)) {
            return false;
        }
        (*position)++;
        count++;
        if (count == 10 && (byte_value & 0xFEU) != 0) {
            return false;
        }
        result |= ((uint64_t)(byte_value & 0x7FU)) << shift;
        if ((byte_value & 0x80U) == 0) {
            *value = result;
            if (width) {
                *width = count;
            }
            return true;
        }
        shift += 7;
    }
    return false;
}

static bool xx_rar_verify_header_crc(xx_io_device *device, const xx_rar_block *block) {
    uint64_t calculated = 0;
    if (!device || !block) {
        return false;
    }
    if (block->version == XX_RAR_VERSION_4) {
        uint16_t expected;
        if (block->header_size < 7) {
            return false;
        }
        expected = xx_io_get_u16(device, block->offset, false);
        if (!xx_crc_calculate_device_by_type(device, block->offset + 2,
                                             block->header_size - 2,
                                             XX_CRC_TYPE_CRC32, NULL,
                                             &calculated)) {
            return false;
        }
        return expected == (uint16_t)calculated;
    }
    if (block->version == XX_RAR_VERSION_5) {
        uint32_t expected;
        if (block->header_size < 7) {
            return false;
        }
        expected = xx_io_get_u32(device, block->offset, false);
        if (!xx_crc_calculate_device_by_type(device, block->offset + 4,
                                             block->header_size - 4,
                                             XX_CRC_TYPE_CRC32, NULL,
                                             &calculated)) {
            return false;
        }
        return expected == (uint32_t)calculated;
    }
    return false;
}

static bool xx_rar_find_signature(Abstractformat *self, xx_rar_version_t *version,
                                  int64_t *signature_offset) {
    int64_t device_size;
    int64_t available;
    size_t scan_size;
    uint8_t *buffer;
    bool found = false;

    if (!self || !self->device || !version || !signature_offset || self->base_address < 0) {
        return false;
    }
    device_size = xx_io_total_size(self->device);
    if (device_size < 0 || self->base_address > device_size) {
        return false;
    }
    available = device_size - self->base_address;
    if (available < (int64_t)XX_RAR4_SIGNATURE_SIZE) {
        return false;
    }
    scan_size = (size_t)((available > (int64_t)(XX_RAR_MAX_SFX_SIZE + XX_RAR5_SIGNATURE_SIZE))
                             ? (XX_RAR_MAX_SFX_SIZE + XX_RAR5_SIGNATURE_SIZE)
                             : available);
    buffer = (uint8_t *)xx_mem_alloc(scan_size);
    if (!buffer) {
        return false;
    }
    if (!xx_rar_read_exact_at(self->device, self->base_address, buffer, scan_size)) {
        xx_mem_free(buffer);
        return false;
    }
    for (size_t i = 0; i + XX_RAR4_SIGNATURE_SIZE <= scan_size; ++i) {
        if (i + XX_RAR5_SIGNATURE_SIZE <= scan_size &&
            xx_mem_compare(buffer + i, g_xx_rar5_signature, XX_RAR5_SIGNATURE_SIZE) == 0) {
            *version = XX_RAR_VERSION_5;
            *signature_offset = self->base_address + (int64_t)i;
            found = true;
            break;
        }
        if (xx_mem_compare(buffer + i, g_xx_rar4_signature, XX_RAR4_SIGNATURE_SIZE) == 0) {
            *version = XX_RAR_VERSION_4;
            *signature_offset = self->base_address + (int64_t)i;
            found = true;
            break;
        }
    }
    xx_mem_free(buffer);
    return found;
}

static bool xx_rar_parse_block4(xx_io_device *device, int64_t device_size,
                                int64_t offset, xx_rar_block *block) {
    uint16_t flags;
    uint16_t header_size;
    uint8_t type;
    uint64_t data_size = 0;
    int64_t header_end;
    int64_t next_offset;

    if (!device || !block || !xx_rar_range_valid(device_size, offset, 7)) {
        return false;
    }
    type = (uint8_t)xx_io_get_u8(device, offset + 2);
    flags = xx_io_get_u16(device, offset + 3, false);
    header_size = xx_io_get_u16(device, offset + 5, false);
    if (header_size < 7 || !xx_rar_range_valid(device_size, offset, header_size)) {
        return false;
    }
    if ((flags & XX_RAR4_FLAG_LONG_BLOCK) != 0) {
        if (header_size < 11) {
            return false;
        }
        data_size = xx_io_get_u32(device, offset + 7, false);
    }
    if ((type == XX_RAR4_HEADER_FILE || type == XX_RAR4_HEADER_SERVICE) &&
        (flags & XX_RAR4_FILE_FLAG_LARGE) != 0) {
        if (header_size < 40) {
            return false;
        }
        data_size |= ((uint64_t)xx_io_get_u32(device, offset + 32, false)) << 32;
    }
    if (data_size > (uint64_t)INT64_MAX ||
        !xx_rar_add_i64(offset, header_size, &header_end) ||
        !xx_rar_add_i64(header_end, (int64_t)data_size, &next_offset) ||
        next_offset > device_size) {
        return false;
    }

    xx_mem_zero(block, sizeof(*block));
    block->version = XX_RAR_VERSION_4;
    block->offset = offset;
    block->header_size = header_size;
    block->data_offset = header_end;
    block->data_size = (int64_t)data_size;
    block->next_offset = next_offset;
    block->type = type;
    block->flags = flags;
    block->header_size_rel = 5;
    block->type_rel = 2;
    block->flags_rel = 3;
    block->header_size_width = 2;
    block->type_width = 1;
    block->flags_width = 2;
    return xx_rar_verify_header_crc(device, block);
}

static bool xx_rar_parse_block5(xx_io_device *device, int64_t device_size,
                                int64_t offset, xx_rar_block *block) {
    int64_t position;
    int64_t header_data_offset;
    int64_t header_end;
    int64_t next_offset;
    uint64_t header_data_size;
    uint64_t type;
    uint64_t flags;
    uint64_t extra_size = 0;
    uint64_t data_size = 0;
    uint8_t size_width = 0;
    uint8_t type_width = 0;
    uint8_t flags_width = 0;
    uint8_t extra_width = 0;
    uint8_t data_width = 0;
    int64_t type_rel;
    int64_t flags_rel;
    int64_t extra_rel = -1;
    int64_t data_rel = -1;

    if (!device || !block || !xx_rar_range_valid(device_size, offset, 7)) {
        return false;
    }
    position = offset + 4;
    if (!xx_rar_read_vint(device, &position, device_size, &header_data_size, &size_width) ||
        size_width > 3 || header_data_size == 0 || header_data_size > XX_RAR5_MAX_HEADER_SIZE) {
        return false;
    }
    header_data_offset = position;
    if (header_data_size > (uint64_t)INT64_MAX ||
        !xx_rar_add_i64(header_data_offset, (int64_t)header_data_size, &header_end) ||
        header_end > device_size) {
        return false;
    }
    type_rel = position - offset;
    if (!xx_rar_read_vint(device, &position, header_end, &type, &type_width)) {
        return false;
    }
    flags_rel = position - offset;
    if (!xx_rar_read_vint(device, &position, header_end, &flags, &flags_width)) {
        return false;
    }
    if ((flags & XX_RAR5_BLOCK_FLAG_EXTRA) != 0) {
        extra_rel = position - offset;
        if (!xx_rar_read_vint(device, &position, header_end, &extra_size, &extra_width)) {
            return false;
        }
    }
    if ((flags & XX_RAR5_BLOCK_FLAG_DATA) != 0) {
        data_rel = position - offset;
        if (!xx_rar_read_vint(device, &position, header_end, &data_size, &data_width)) {
            return false;
        }
    }
    if (extra_size > (uint64_t)(header_end - position) || data_size > (uint64_t)INT64_MAX ||
        !xx_rar_add_i64(header_end, (int64_t)data_size, &next_offset) ||
        next_offset > device_size) {
        return false;
    }

    xx_mem_zero(block, sizeof(*block));
    block->version = XX_RAR_VERSION_5;
    block->offset = offset;
    block->header_size = header_end - offset;
    block->data_offset = header_end;
    block->data_size = (int64_t)data_size;
    block->next_offset = next_offset;
    block->type = type;
    block->flags = flags;
    block->extra_size = extra_size;
    block->header_size_rel = 4;
    block->type_rel = type_rel;
    block->flags_rel = flags_rel;
    block->extra_size_rel = extra_rel;
    block->data_size_rel = data_rel;
    block->header_size_width = size_width;
    block->type_width = type_width;
    block->flags_width = flags_width;
    block->extra_size_width = extra_width;
    block->data_size_width = data_width;
    return xx_rar_verify_header_crc(device, block);
}

static bool xx_rar_parse_block(xx_io_device *device, int64_t device_size,
                               xx_rar_version_t version, int64_t offset,
                               xx_rar_block *block) {
    if (version == XX_RAR_VERSION_4) {
        return xx_rar_parse_block4(device, device_size, offset, block);
    }
    if (version == XX_RAR_VERSION_5) {
        return xx_rar_parse_block5(device, device_size, offset, block);
    }
    return false;
}

static bool xx_rar5_parse_file_extras(xx_io_device *device, int64_t extra_offset,
                                       uint64_t extra_size, xx_rar_file_info *info) {
    int64_t position = extra_offset;
    int64_t end;

    if (!device || !info || extra_size > (uint64_t)INT64_MAX ||
        !xx_rar_add_i64(extra_offset, (int64_t)extra_size, &end)) {
        return false;
    }
    while (position < end) {
        uint64_t record_size;
        uint64_t record_type;
        int64_t record_data;
        int64_t record_end;
        if (!xx_rar_read_vint(device, &position, end, &record_size, NULL) ||
            record_size == 0 || record_size > (uint64_t)INT64_MAX) {
            return false;
        }
        record_data = position;
        if (!xx_rar_add_i64(record_data, (int64_t)record_size, &record_end) ||
            record_end > end ||
            !xx_rar_read_vint(device, &position, record_end, &record_type, NULL)) {
            return false;
        }
        if (record_type == XX_RAR5_EXTRA_FILE_HASH) {
            uint64_t hash_type;
            if (info->has_blake2 ||
                !xx_rar_read_vint(device,&position,record_end,&hash_type,NULL) ||
                hash_type != 0 || record_end-position != 32 ||
                !xx_rar_read_exact_at(device,position,info->blake2,32)) return false;
            info->has_blake2 = true;
        } else if (record_type == XX_RAR5_EXTRA_FILE_ENCRYPTION) {
            uint64_t encryption_version;
            uint64_t encryption_flags;
            if (info->is_encrypted) return false;
            info->is_encrypted = true;
            if (!xx_rar_read_vint(device, &position, record_end,
                                  &encryption_version, NULL) ||
                !xx_rar_read_vint(device, &position, record_end,
                                  &encryption_flags, NULL)) {
                return false;
            }
            /* Version zero contains KDF count, salt and IV, plus an optional
             * password check value.  Future versions remain enumerable but
             * are never passed to extraction. */
            if (encryption_version == 0) {
                int64_t required = 1 + 16 + 16;
                if ((encryption_flags & 0x01U) != 0) {
                    required += 12;
                }
                if (record_end - position != required) {
                    return false;
                }
                info->crypto_version = 5;
                info->crypto_flags = (uint8_t)encryption_flags;
                if (encryption_flags > 3U ||
                    !xx_rar_read_exact_at(device, position, &info->crypto_log, 1) ||
                    !xx_rar_read_exact_at(device, position + 1, info->crypto_salt, 16) ||
                    !xx_rar_read_exact_at(device, position + 17, info->crypto_iv, 16) ||
                    ((encryption_flags & 1U) && !xx_rar_read_exact_at(device,
                        position + 33, info->crypto_check, 12))) return false;
            }
        }
        position = record_end;
    }
    return position == end;
}

static bool xx_rar_parse_file_info4(xx_io_device *device, const xx_rar_block *block,
                                    xx_rar_file_info *info) {
    uint16_t flags;
    uint16_t name_size;
    int64_t name_offset;
    int64_t name_end;
    size_t fixed_size;

    if (!device || !block || !info ||
        (block->type != XX_RAR4_HEADER_FILE &&
         block->type != XX_RAR4_HEADER_SERVICE) ||
        (block->flags & XX_RAR4_FLAG_LONG_BLOCK) == 0) {
        return false;
    }
    flags = (uint16_t)block->flags;
    fixed_size = (flags & XX_RAR4_FILE_FLAG_LARGE) != 0 ? 40U : 32U;
    if (block->header_size < (int64_t)fixed_size) {
        return false;
    }
    name_size = xx_io_get_u16(device, block->offset + 26, false);
    name_offset = block->offset + (int64_t)fixed_size;
    if (name_size == 0 || !xx_rar_add_i64(name_offset, name_size, &name_end) ||
        name_end > block->data_offset) {
        return false;
    }

    xx_mem_zero(info, sizeof(*info));
    info->block = *block;
    info->unpacked_size = xx_io_get_u32(device, block->offset + 11, false);
    if ((flags & XX_RAR4_FILE_FLAG_LARGE) != 0) {
        info->unpacked_size |= ((uint64_t)xx_io_get_u32(device, block->offset + 36, false)) << 32;
    }
    info->attributes = xx_io_get_u32(device, block->offset + 28, false);
    info->host_os = (uint8_t)xx_io_get_u8(device, block->offset + 15);
    info->timestamp = xx_io_get_u32(device, block->offset + 20, false);
    info->data_crc = xx_io_get_u32(device, block->offset + 16, false);
    info->has_crc = true;
    info->file_flags = flags;
    info->method = (uint8_t)xx_io_get_u8(device, block->offset + 25);
    info->unpack_version = (uint8_t)xx_io_get_u8(device, block->offset + 24);
    info->name_offset = name_offset;
    info->name_size = name_size;
    info->is_folder =
        ((flags & XX_RAR4_FILE_FLAG_DIRECTORY) == XX_RAR4_FILE_FLAG_DIRECTORY) ||
        (info->host_os <= 2U && (info->attributes & 0x10U) != 0) ||
        (info->host_os == 3U &&
         (info->attributes & 0170000U) == 0040000U);
    info->is_encrypted = (flags & XX_RAR4_FILE_FLAG_PASSWORD) != 0;
    if (info->is_encrypted && info->unpack_version >= 29U) {
        info->crypto_version = 3;
        if (flags & XX_RAR4_FILE_FLAG_SALT) {
            if (block->data_offset - name_end < 8 ||
                !xx_rar_read_exact_at(device, name_end, info->crypto_salt, 8)) return false;
            info->crypto_flags = 1;
        }
    }
    info->is_split = (flags & (XX_RAR4_FILE_FLAG_SPLIT_BEFORE |
                               XX_RAR4_FILE_FLAG_SPLIT_AFTER)) != 0;
    info->rar4_unicode_name = (flags & XX_RAR4_FILE_FLAG_UNICODE) != 0;
    return xx_rar_normalize_codec(info);
}

static bool xx_rar_parse_file_info5(xx_io_device *device, const xx_rar_block *block,
                                    xx_rar_file_info *info, bool headers_encrypted) {
    int64_t position;
    int64_t fixed_end;
    uint64_t file_flags;
    uint64_t unpacked_size;
    uint64_t attributes;
    uint64_t compression_info;
    uint64_t host_os;
    uint64_t name_size;
    uint32_t timestamp = 0;
    uint32_t data_crc = 0;

    if (!device || !block || !info ||
        (block->type != XX_RAR5_HEADER_FILE &&
         block->type != XX_RAR5_HEADER_SERVICE) ||
        block->extra_size > (uint64_t)block->header_size) {
        return false;
    }
    fixed_end = block->data_offset - (int64_t)block->extra_size;
    position = block->offset + block->flags_rel + block->flags_width;
    if ((block->flags & XX_RAR5_BLOCK_FLAG_EXTRA) != 0) {
        position += block->extra_size_width;
    }
    if ((block->flags & XX_RAR5_BLOCK_FLAG_DATA) != 0) {
        position += block->data_size_width;
    }
    if (position > fixed_end ||
        !xx_rar_read_vint(device, &position, fixed_end, &file_flags, NULL) ||
        !xx_rar_read_vint(device, &position, fixed_end, &unpacked_size, NULL) ||
        !xx_rar_read_vint(device, &position, fixed_end, &attributes, NULL)) {
        return false;
    }
    if ((file_flags & XX_RAR5_FILE_FLAG_MTIME) != 0) {
        if (fixed_end - position < 4) {
            return false;
        }
        timestamp = xx_io_get_u32(device, position, false);
        position += 4;
    }
    if ((file_flags & XX_RAR5_FILE_FLAG_CRC32) != 0) {
        if (fixed_end - position < 4) {
            return false;
        }
        data_crc = xx_io_get_u32(device, position, false);
        position += 4;
    }
    if (!xx_rar_read_vint(device, &position, fixed_end, &compression_info, NULL) ||
        !xx_rar_read_vint(device, &position, fixed_end, &host_os, NULL) ||
        !xx_rar_read_vint(device, &position, fixed_end, &name_size, NULL) ||
        name_size == 0 || name_size > (uint64_t)(fixed_end - position) ||
        name_size > SIZE_MAX) {
        return false;
    }
    xx_mem_zero(info, sizeof(*info));
    if (block->extra_size != 0 &&
        !xx_rar5_parse_file_extras(device, fixed_end, block->extra_size, info)) return false;
    info->block = *block;
    info->unpacked_size = unpacked_size;
    info->attributes = attributes;
    info->timestamp = timestamp;
    info->compression_info = compression_info;
    info->host_os = (uint32_t)host_os;
    info->data_crc = data_crc;
    info->has_crc = (file_flags & XX_RAR5_FILE_FLAG_CRC32) != 0;
    info->file_flags = (uint32_t)file_flags;
    info->method = (uint32_t)((compression_info >> 7) & 0x07U);
    info->name_offset = position;
    info->name_size = (size_t)name_size;
    info->is_folder = (file_flags & XX_RAR5_FILE_FLAG_DIRECTORY) != 0;
    info->is_split = (block->flags & (XX_RAR5_BLOCK_FLAG_SPLIT_BEFORE |
                                      XX_RAR5_BLOCK_FLAG_SPLIT_AFTER)) != 0;
    info->unknown_size = (file_flags & XX_RAR5_FILE_FLAG_UNKNOWN_SIZE) != 0;
    info->name_is_utf8 = true;
    /* Native -hp archives omit HASHMAC because checksum metadata is itself
     * encrypted.  Allow this only for views produced by header decryption,
     * never based on an archive-supplied flag or the presence of a password.
     * With visible headers, require keyed final checksums; intermediate split
     * fragments may still declare raw checksums of their packed ciphertext. */
    if (info->is_encrypted && info->crypto_version == 5 &&
        !headers_encrypted &&
        (block->flags & XX_RAR5_BLOCK_FLAG_SPLIT_AFTER) == 0 &&
        (info->crypto_flags & 0x02U) == 0) {
        return false;
    }
    return xx_rar_normalize_codec(info);
}

static bool xx_rar_parse_file_info(xx_io_device *device, const xx_rar_block *block,
                                   xx_rar_file_info *info, bool headers_encrypted) {
    if (!block) {
        return false;
    }
    return block->version == XX_RAR_VERSION_4
               ? xx_rar_parse_file_info4(device, block, info)
               : xx_rar_parse_file_info5(device, block, info, headers_encrypted);
}

static int64_t xx_rar5_specific_fields_offset(const xx_rar_block *block) {
    int64_t position;
    if (!block || block->version != XX_RAR_VERSION_5) {
        return -1;
    }
    position = block->offset + block->flags_rel + block->flags_width;
    if ((block->flags & XX_RAR5_BLOCK_FLAG_EXTRA) != 0) {
        position += block->extra_size_width;
    }
    if ((block->flags & XX_RAR5_BLOCK_FLAG_DATA) != 0) {
        position += block->data_size_width;
    }
    return position;
}

static bool xx_rar_validate_block5_semantics(xx_io_device *device,
                                             const xx_rar_block *block) {
    int64_t position;
    int64_t fixed_end;
    uint64_t value;
    if (!device || !block || block->version != XX_RAR_VERSION_5 ||
        block->extra_size > (uint64_t)block->header_size) {
        return false;
    }
    position = xx_rar5_specific_fields_offset(block);
    fixed_end = block->data_offset - (int64_t)block->extra_size;
    if (position < 0 || position > fixed_end) {
        return false;
    }
    if (block->type == XX_RAR5_HEADER_MAIN) {
        uint64_t archive_flags;
        if (!xx_rar_read_vint(device, &position, fixed_end, &archive_flags, NULL)) {
            return false;
        }
        if ((archive_flags & 0x02U) != 0 &&
            !xx_rar_read_vint(device, &position, fixed_end, &value, NULL)) {
            return false;
        }
        return position == fixed_end;
    }
    if (block->type == XX_RAR5_HEADER_END) {
        return xx_rar_read_vint(device, &position, fixed_end, &value, NULL) &&
               position == fixed_end;
    }
    if (block->type == XX_RAR5_HEADER_CRYPT) {
        uint64_t version;
        uint64_t flags;
        int64_t required;
        if (block->extra_size != 0 || block->data_size != 0 ||
            !xx_rar_read_vint(device, &position, fixed_end, &version, NULL) ||
            !xx_rar_read_vint(device, &position, fixed_end, &flags, NULL)) {
            return false;
        }
        required = 1 + 16 + ((flags & 0x01U) != 0 ? 12 : 0);
        if (version == 0 && fixed_end - position != required) {
            return false;
        }
        return version != 0 || fixed_end - position >= 1;
    }
    return true;
}

#include "xx_rar_crypto.inc"
#include "xx_rar_split.inc"

static bool xx_rar_has_decrypted_headers(const xx_rar *rar) {
    return rar->split_view &&
        ((const xx_rar_split_view *)rar->split_view)->headers_encrypted;
}

static bool xx_rar_set_record_name(xx_io_device *device, const xx_rar_file_info *info,
                                   xx_archive_record *record) {
    char *name;
    size_t usable_size;
    wchar_t *wide_name;
    bool result;

    if (!device || !info || !record || info->name_size == 0) {
        return false;
    }
    name = (char *)xx_mem_alloc(info->name_size + 1U);
    if (!name) {
        return false;
    }
    if (!xx_rar_read_exact_at(device, info->name_offset, name, info->name_size)) {
        xx_rar_secure_clear(name,info->name_size);
        xx_mem_free(name);
        return false;
    }
    usable_size = info->name_size;
    if (info->rar4_unicode_name) {
        size_t i;
        for (i = 0; i < usable_size; ++i) {
            if (name[i] == '\0') {
                usable_size = i;
                break;
            }
        }
    }
    if (usable_size == 0) {
        xx_rar_secure_clear(name,info->name_size);
        xx_mem_free(name);
        return false;
    }
    name[usable_size] = '\0';
    wide_name = info->name_is_utf8 ? xx_str_utf8_to_unicode(name)
                                   : xx_str_ansi_to_unicode(name);
    if (wide_name) {
        result = xx_archive_record_set_original_name_w(record, wide_name);
        xx_rar_secure_clear(wide_name,xx_str_wlen(wide_name)*sizeof(wchar_t));
        xx_str_wfree(wide_name);
    } else {
        result = xx_archive_record_set_original_name(record, name);
    }
    xx_rar_secure_clear(name,info->name_size);xx_mem_free(name);
    return result;
}

static bool xx_rar_populate_archive_record(xx_io_device *device,
                                           const xx_rar_file_info *info,
                                           xx_archive_record *record) {
    if (!device || !info || !record) {
        return false;
    }
    xx_archive_record_init(record);
    record->header_offset = info->block.offset;
    record->header_size = info->block.header_size;
    record->data_offset = info->block.data_offset;
    record->compressed_size = info->block.data_size;
    if (!xx_rar_set_record_name(device, info, record) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, info->unpacked_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)info->block.data_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, info->method) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, info->attributes) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, info->timestamp) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, info->block.flags) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_INTERNAL_ATTRS, info->file_flags) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, info->is_folder) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, info->is_encrypted)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (info->has_crc &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, info->data_crc)) {
        xx_archive_record_cleanup(record);
        return false;
    }
    if (info->block.version == XX_RAR_VERSION_4) {
        if (!xx_archive_record_set_meta_u64(record, XX_META_ID_VERSION_NEEDED,
                                            info->unpack_version)) {
            xx_archive_record_cleanup(record);
            return false;
        }
    }
    return true;
}

void xx_rar_init(xx_rar *rar, xx_io_device *dev, int64_t base_address) {
    if (!rar) {
        return;
    }
    xx_mem_zero(rar, sizeof(*rar));
    xx_format_init(&rar->format, dev, base_address);
    rar->format.endian = XX_ENDIAN_LITTLE;
    rar->format.file_type = XX_FILE_TYPE_RAR;
    rar->format.format_type = XX_TYPE_ARCHIVE;
    rar->format.is_archive = true;
    xx_format_set_mime_type(&rar->format, "application/vnd.rar");
    xx_format_set_extension(&rar->format, "rar");

    rar->format.check_is_valid = xx_rar_check_is_valid;
    rar->format.handle_base_info = xx_rar_handle_base_info;
    rar->format.handle_split_format = xx_rar_handle_split_format;
    rar->format.get_format_size = xx_rar_get_format_size;
    rar->format.get_number_of_archive_records = xx_rar_get_number_of_archive_records;
    rar->format.create_archive_records_reading = xx_rar_create_archive_records_reading;
    rar->format.get_current_archive_record = xx_rar_get_current_archive_record;
    rar->format.unpack_current_archive_record = xx_rar_unpack_current_archive_record;
    rar->format.archive_record_move_to_next = xx_rar_archive_record_move_to_next;
    rar->format.free_archive_records_reading = xx_rar_free_archive_records_reading;
    rar->format.data_struct_id_to_string = xx_rar_data_struct_id_to_string;
    rar->format.data_struct_string_to_id = xx_rar_data_struct_string_to_id;
    rar->format.create_data_structs_reading = xx_rar_create_data_structs_reading;
    rar->format.get_current_data_struct = xx_rar_get_current_data_struct;
    rar->format.data_struct_move_to_next = xx_rar_data_struct_move_to_next;
    rar->format.free_data_structs_reading = xx_rar_free_data_structs_reading;
    rar->format.create_data_struct_records_reading = xx_rar_create_data_struct_records_reading;
    rar->format.get_current_data_struct_record = xx_rar_get_current_data_struct_record;
    rar->format.data_struct_record_move_to_next = xx_rar_data_struct_record_move_to_next;
    rar->format.free_data_struct_records_reading = xx_rar_free_data_struct_records_reading;
    rar->format.destroy = xx_rar_vtable_destroy;

    rar->version = XX_RAR_VERSION_UNKNOWN;
    rar->signature_offset = -1;
    rar->first_header_offset = -1;
}

xx_rar *xx_rar_create(xx_io_device *dev, int64_t base_address) {
    xx_rar *rar = (xx_rar *)xx_mem_alloc(sizeof(*rar));
    if (rar) {
        xx_rar_init(rar, dev, base_address);
    }
    return rar;
}

void xx_rar_destroy(xx_rar *rar) {
    if (rar) xx_rar_split_cleanup(rar);
    if (rar && rar->format.close) {
        rar->format.close(&rar->format);
    }
    if (rar) {
        xx_format_cleanup_extra_parameters(&rar->format);
    }
}

static void xx_rar_vtable_destroy(Abstractformat *self) {
    if (self) {
        xx_rar_destroy((xx_rar *)self);
    }
}

void xx_rar_free(xx_rar *rar) {
    if (rar) {
        xx_rar_destroy(rar);
        xx_mem_free(rar);
    }
}

bool xx_rar_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_rar_version_t version;
    int64_t signature_offset;
    if (!xx_format_handle_split_format(self, pd)) return false;
    return xx_rar_find_signature(self, &version, &signature_offset);
}

bool xx_rar_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_rar *rar;
    xx_rar_version_t version;
    int64_t signature_offset;
    int64_t offset;
    int64_t format_end = -1;
    int64_t device_size;
    uint64_t count = 0;
    uint32_t last_unpack_version = 0;
    bool first_block = true;
    bool found_end = false;
    bool headers_encrypted = false;

    if (!xx_format_handle_split_format(self, pd)) return false;
    if (!self || !self->device || !xx_rar_find_signature(self, &version, &signature_offset)) {
        if (self) self->is_valid = false;
        return false;
    }
    rar = (xx_rar *)self;
    device_size = xx_io_total_size(self->device);
    offset = signature_offset + (version == XX_RAR_VERSION_4
                                     ? (int64_t)XX_RAR4_SIGNATURE_SIZE
                                     : (int64_t)XX_RAR5_SIGNATURE_SIZE);
    rar->version = version;
    rar->signature_offset = signature_offset;
    rar->first_header_offset = offset;
    rar->number_of_records = 0;
    self->is_crypted = xx_rar_has_decrypted_headers(rar);

    while (offset < device_size) {
        xx_rar_block block;
        if (pd && xx_pd_is_stopped(pd)) {
            self->is_valid = false;
            return false;
        }
        if (!xx_rar_parse_block(self->device, device_size, version, offset, &block)) {
            self->is_valid = false;
            return false;
        }
        if (version == XX_RAR_VERSION_5 &&
            !xx_rar_validate_block5_semantics(self->device, &block)) {
            self->is_valid = false;
            return false;
        }
        if (first_block) {
            bool valid_first = (version == XX_RAR_VERSION_4)
                                   ? block.type == XX_RAR4_HEADER_MAIN
                                   : (block.type == XX_RAR5_HEADER_MAIN ||
                                      block.type == XX_RAR5_HEADER_CRYPT);
            if (!valid_first) {
                self->is_valid = false;
                return false;
            }
            first_block = false;
        }

        if (version == XX_RAR_VERSION_4) {
            if (block.type == XX_RAR4_HEADER_MAIN) {
                if (block.header_size < 13) {
                    self->is_valid = false;
                    return false;
                }
                if ((block.flags & XX_RAR4_MAIN_FLAG_PASSWORD) != 0) {
                    headers_encrypted = true;
                    self->is_crypted = true;
                    format_end = device_size;
                    break;
                }
            } else if (block.type == XX_RAR4_HEADER_FILE ||
                       block.type == XX_RAR4_HEADER_SERVICE) {
                xx_rar_file_info info;
                if (!xx_rar_parse_file_info(self->device, &block, &info,
                                             xx_rar_has_decrypted_headers(rar))) {
                    self->is_valid = false;
                    return false;
                }
                if (block.type == XX_RAR4_HEADER_FILE) {
                    count++;
                    last_unpack_version = info.unpack_version;
                }
                self->is_crypted = self->is_crypted || info.is_encrypted;
            } else if (block.type == XX_RAR4_HEADER_END) {
                found_end = true;
                format_end = block.next_offset;
                break;
            }
        } else {
            if (block.type == XX_RAR5_HEADER_CRYPT) {
                headers_encrypted = true;
                self->is_crypted = true;
                format_end = device_size;
                break;
            }
            if (block.type == XX_RAR5_HEADER_FILE ||
                block.type == XX_RAR5_HEADER_SERVICE) {
                xx_rar_file_info info;
                if (!xx_rar_parse_file_info(self->device, &block, &info,
                                             xx_rar_has_decrypted_headers(rar))) {
                    self->is_valid = false;
                    return false;
                }
                if (block.type == XX_RAR5_HEADER_FILE) {
                    count++;
                }
                self->is_crypted = self->is_crypted || info.is_encrypted;
            } else if (block.type == XX_RAR5_HEADER_END) {
                found_end = true;
                format_end = block.next_offset;
                break;
            }
        }
        if (block.next_offset <= offset) {
            self->is_valid = false;
            return false;
        }
        offset = block.next_offset;
    }

    /* RAR 1.5/2.0 commonly terminates exactly at EOF without ENDARC. */
    if (!found_end && !headers_encrypted && version == XX_RAR_VERSION_4 &&
        count != 0 && offset == device_size && last_unpack_version <= 20U) {
        found_end = true;
        format_end = device_size;
    }
    if (first_block || (!found_end && !headers_encrypted) ||
        format_end < self->base_address) {
        self->is_valid = false;
        return false;
    }
    rar->number_of_records = count;
    self->number_of_archive_records = count;
    self->format_size = format_end - self->base_address;
    if (format_end < device_size) {
        self->overlay_offset = format_end;
        self->overlay_size = device_size - format_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->file_type = XX_FILE_TYPE_RAR;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->endian = XX_ENDIAN_LITTLE;
    xx_format_set_version(self, version == XX_RAR_VERSION_4 ? "4" : "5");
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_rar_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(self, pd)) {
        return -1;
    }
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_rar_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd) {
    if (!xx_format_handle_split_format(self, pd)) {
        return 0;
    }
    if (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) {
        return 0;
    }
    return ((xx_rar *)self)->number_of_records;
}

xx_rar_version_t xx_rar_get_version(const xx_rar *rar) {
    return rar ? rar->version : XX_RAR_VERSION_UNKNOWN;
}

int64_t xx_rar_get_signature_offset(const xx_rar *rar) {
    return rar ? rar->signature_offset : -1;
}

uint64_t xx_rar_get_number_of_records(const xx_rar *rar) {
    return rar ? rar->number_of_records : 0;
}

typedef struct xx_rar_stream_state_s {
    int64_t scan_offset;
    int64_t scan_end;
    uint64_t next_index;
    xx_rar_file_info current_info;
    bool current_info_valid;
    bool current_decoded;
    bool solid_chain_gap;
    xx_rarx_decoder *decoder;
    uint8_t *decoded_data;
    size_t decoded_size;
} xx_rar_stream_state;

static void xx_rar_clear_record_name(xx_archive_record *record) {
    size_t i;
    if(!record)return;
    for(i=0;i<record->list_meta.count;++i) {
        xx_meta *meta=(xx_meta *)xx_list_at(&record->list_meta,i);
        if(meta && meta->meta_id==XX_META_ID_ORIGINAL_NAME && meta->var.is_allocated) {
            if(meta->var.type==XX_VAR_TYPE_STRING)
                xx_rar_secure_clear(meta->var.val.str.ptr,meta->var.val.str.len);
            else if(meta->var.type==XX_VAR_TYPE_WSTRING)
                xx_rar_secure_clear(meta->var.val.wstr.ptr,meta->var.val.wstr.len*sizeof(wchar_t));
        }
    }
}

static void xx_rar_stream_state_free(void *pointer) {
    xx_rar_stream_state *state = (xx_rar_stream_state *)pointer;
    if (!state) return;
    xx_rarx_decoder_free(state->decoder);
    xx_rar_secure_clear(state->decoded_data,state->decoded_size);
    xx_mem_free(state->decoded_data);
    xx_rar_secure_clear(state,sizeof(*state));
    xx_mem_free(state);
}

static bool xx_rar_stream_load_next(Abstractformat *self,
                                    xx_archive_record_state *state,
                                    xx_rar_stream_state *rar_state,
                                    xx_pd_struct *pd) {
    xx_rar *rar = (xx_rar *)self;
    int64_t device_size = xx_io_total_size(self->device);
    int64_t cursor = rar_state->scan_offset;
    while (cursor < rar_state->scan_end) {
        xx_rar_block block;
        bool is_file;
        if (pd && xx_pd_is_stopped(pd)) {
            return false;
        }
        if (!xx_rar_parse_block(self->device, device_size, rar->version,
                                cursor, &block)) {
            return false;
        }
        is_file = rar->version == XX_RAR_VERSION_4
                      ? block.type == XX_RAR4_HEADER_FILE
                      : block.type == XX_RAR5_HEADER_FILE;
        if (is_file) {
            xx_rar_file_info info;
            if (!xx_rar_parse_file_info(self->device, &block, &info,
                                         xx_rar_has_decrypted_headers(rar)) ||
                !xx_rar_populate_archive_record(self->device, &info,
                                                &state->current_record)) {
                xx_rar_clear_record_name(&state->current_record);
                xx_archive_record_cleanup(&state->current_record);
                xx_archive_record_init(&state->current_record);
                return false;
            }
            if (xx_pd_is_stopped(pd)) {
                xx_rar_clear_record_name(&state->current_record);
                xx_archive_record_cleanup(&state->current_record);
                xx_archive_record_init(&state->current_record);
                return false;
            }
            rar_state->scan_offset = block.next_offset;
            state->current_index = (int64_t)rar_state->next_index++;
            state->has_record = true;
            rar_state->current_info = info;
            rar_state->current_info_valid = true;
            rar_state->current_decoded = false;
            xx_rar_secure_clear(rar_state->decoded_data,rar_state->decoded_size);
            xx_mem_free(rar_state->decoded_data);
            rar_state->decoded_data = NULL;
            rar_state->decoded_size = 0;
            return true;
        }
        if ((rar->version == XX_RAR_VERSION_4 && block.type == XX_RAR4_HEADER_END) ||
            (rar->version == XX_RAR_VERSION_5 &&
             (block.type == XX_RAR5_HEADER_END || block.type == XX_RAR5_HEADER_CRYPT))) {
            rar_state->scan_offset = block.next_offset;
            return false;
        }
        cursor = block.next_offset;
    }
    rar_state->scan_offset = cursor;
    return false;
}

xx_archive_record_state *xx_rar_create_archive_records_reading(Abstractformat *self,
                                                               const xx_list_s *options,
                                                               xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_rar_stream_state *rar_state;
    xx_rar *rar;
    int pd_level = -1;

    if (!xx_format_handle_split_format(self, pd) || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    rar = (xx_rar *)self;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (options) {
        for (size_t i = 0; i < options->count; ++i) {
            const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
            xx_meta copy;
            if (!source) continue;
            xx_meta_init(&copy, source->meta_id);
            if (!xx_var_copy(&copy.var, &source->var) ||
                !xx_list_append(&state->options, &copy)) {
                xx_meta_cleanup(&copy);
                xx_archive_record_state_free(state);
                return NULL;
            }
        }
    }
    rar_state = (xx_rar_stream_state *)xx_mem_calloc(1, sizeof(*rar_state));
    if (!rar_state) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    rar_state->scan_offset = rar->first_header_offset;
    rar_state->scan_end = self->base_address + self->format_size;
    rar_state->decoder = xx_rarx_decoder_create(0);
    if (!rar_state->decoder) {
        xx_mem_free(rar_state);
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->internal_state = rar_state;
    state->free_internal = xx_rar_stream_state_free;
    state->total_records = (int64_t)rar->number_of_records;
    if (pd) {
        pd_level = xx_pd_enter_level(pd, rar->number_of_records,
                                     "Reading RAR records");
    }
    if (rar->number_of_records != 0) {
        if (!xx_rar_stream_load_next(self, state, rar_state, pd)) {
            xx_pd_leave_level(pd, pd_level);
            xx_archive_record_state_free(state);
            return NULL;
        }
        if (pd_level >= 0) {
            xx_pd_set_current(pd, pd_level, 1U);
        }
    }
    xx_pd_leave_level(pd, pd_level);
    return state;
}

const xx_archive_record *xx_rar_get_current_archive_record(Abstractformat *self,
                                                           xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rar_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_rar_stream_state *rar_state;
    int pd_level;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    rar_state = (xx_rar_stream_state *)state->internal_state;
    pd_level = xx_pd_enter_level(pd, (uint64_t)state->total_records,
                                 "Reading RAR records");
    if (pd_level >= 0) {
        xx_pd_set_current(pd, pd_level, rar_state->next_index);
    }
    if (rar_state->current_info_valid && !rar_state->current_decoded &&
        !rar_state->current_info.is_folder &&
        ((rar_state->current_info.block.version == XX_RAR_VERSION_4 &&
          rar_state->current_info.method != XX_RAR4_METHOD_STORE) ||
         (rar_state->current_info.block.version == XX_RAR_VERSION_5 &&
          rar_state->current_info.method != XX_RAR5_METHOD_STORE))) {
        rar_state->solid_chain_gap = true;
    }
    rar_state->current_info_valid = false;
    rar_state->current_decoded = false;
    xx_rar_secure_clear(rar_state->decoded_data,rar_state->decoded_size);
    xx_mem_free(rar_state->decoded_data);
    rar_state->decoded_data = NULL;
    rar_state->decoded_size = 0;
    if (state->has_record) {
        xx_rar_clear_record_name(&state->current_record);
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
    }
    if (rar_state->next_index >= (uint64_t)state->total_records) {
        xx_pd_leave_level(pd, pd_level);
        return false;
    }
    result = xx_rar_stream_load_next(self, state, rar_state, pd);
    if (result && pd_level >= 0) {
        xx_pd_set_current(pd, pd_level, (uint64_t)state->current_index + 1U);
    }
    xx_pd_leave_level(pd, pd_level);
    return result;
}


static bool xx_rar_relative_name_is_safe(const wchar_t *name) {
    const wchar_t *segment;
    const wchar_t *position;
    if (!name || !name[0] || name[0] == L'/' || name[0] == L'\\' ||
        (name[0] && name[1] == L':')) {
        return false;
    }
    segment = name;
    position = name;
    for (;;) {
        bool at_end = *position == L'\0';
        bool separator = *position == L'/' || *position == L'\\';
        if (*position == L':') {
            return false;
        }
        if (at_end || separator) {
            size_t length = (size_t)(position - segment);
            if ((length == 1 && segment[0] == L'.') ||
                (length == 2 && segment[0] == L'.' && segment[1] == L'.')) {
                return false;
            }
            /* Win32 aliases leading spaces and trailing spaces/dots, so a
             * lexically safe name can still land on a different on-disk path.
             * Checked everywhere, so an archive rejected on one platform is
             * rejected on all; the reserved-name test is false off Windows. */
            if (length != 0U &&
                (segment[0] == L' ' || segment[length - 1U] == L'.' ||
                 segment[length - 1U] == L' ')) {
                return false;
            }
            if (length != 0U &&
                xx_io_platform_wsegment_is_reserved(segment, length)) {
                return false;
            }
            if (at_end) {
                return true;
            }
            segment = position + 1;
        }
        position++;
    }
}

static wchar_t *xx_rar_make_destination(const wchar_t *base, const wchar_t *name) {
    wchar_t *normalized;
    wchar_t *result;
    size_t base_length;
    bool separator_needed;
    if (!base || !name || !xx_rar_relative_name_is_safe(name)) {
        return NULL;
    }
    const wchar_t separator = xx_io_platform_wseparator();
    const wchar_t separator_text[2] = {separator, L'\0'};

    normalized = xx_str_wdup(name);
    if (!normalized) {
        return NULL;
    }
    for (size_t i = 0; normalized[i]; ++i) {
        if (normalized[i] == L'/' || normalized[i] == L'\\') {
            normalized[i] = separator;
        }
    }
    base_length = xx_str_wlen(base);
    separator_needed = base_length != 0 && base[base_length - 1] != L'/' &&
                       base[base_length - 1] != L'\\';
    result = separator_needed ? xx_str_wconcat3(base, separator_text, normalized)
                              : xx_str_wconcat(base, normalized);
    xx_str_wfree(normalized);
    return result;
}

static bool xx_rar_verify_member(const xx_rar_file_info *info,
                                  const uint8_t *data,size_t size,
                                  const uint8_t hash_key[32],xx_pd_struct *pd) {
    xx_blake2sp_context blake;
    uint8_t digest[32] = {0};
    uint32_t crc = 0;
    size_t done = 0, i;
    unsigned difference = 0;
    bool ok = false;
    if (info->has_blake2 && !xx_blake2sp_init(&blake)) return false;
    while (done<size) {
        size_t n=size-done>65536U?65536U:size-done;
        if (xx_pd_is_stopped(pd)) goto finished;
        if (info->has_crc) crc=xx_crc32_calc(crc,data+done,n);
        if (info->has_blake2 && !xx_blake2sp_update(&blake,data+done,n)) goto finished;
        done+=n;
    }
    if (info->has_crc) {
        if (info->crypto_version==5 && (info->crypto_flags&2U))
            crc=xx_rar5_aes_mac_crc32(hash_key,crc);
        if (crc!=info->data_crc) goto finished;
    }
    if (info->has_blake2) {
        if (!xx_blake2sp_final(&blake,digest)) goto finished;
        if (info->crypto_version==5 && (info->crypto_flags&2U) &&
            !xx_rar5_aes_mac_hash(hash_key,digest,digest)) goto finished;
        for(i=0;i<32U;++i) difference |= digest[i]^info->blake2[i];
        if(difference) goto finished;
    }
    ok=!xx_pd_is_stopped(pd);
finished:
    xx_rar_secure_clear(&blake,sizeof(blake));xx_rar_secure_clear(digest,sizeof(digest));
    return ok;
}

static bool xx_rar_get_u64_limit(const Abstractformat *self,
                                 const xx_list_s *options, uint32_t meta_id,
                                 bool *present, uint64_t *limit) {
    const xx_var *value;
    int64_t signed_value;
    if (!present || !limit) return false;
    *present = false;
    *limit = 0;
    value = xx_format_resolve_extra_parameter(self, options, meta_id);
    if (!value) return true;
    *present = true;
    switch ((xx_var_type_t)value->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64:
            *limit = xx_var_get_u64(value);
            return true;
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64:
            signed_value = xx_var_get_i64(value);
            if (signed_value < 0) return false;
            *limit = (uint64_t)signed_value;
            return true;
        default:
            return false;
    }
}

static bool xx_rar_add_memory(uint64_t *total, uint64_t amount) {
    if (!total || *total > UINT64_MAX - amount) return false;
    *total += amount;
    return true;
}

static bool xx_rar_resource_limits_allow(
    const Abstractformat *self, const xx_list_s *options,
    const xx_rar_file_info *info, bool is_store, xx_pd_struct *pd) {
    bool member_limit_present;
    bool memory_limit_present;
    uint64_t member_limit;
    uint64_t memory_limit;
    uint64_t required = 0;
    uint64_t output_allocation;
    uint64_t packed_allocation;
    uint64_t window_allocation;

    if (!self || !info ||
        !xx_rar_get_u64_limit(self, options,
                              XX_META_ID_OPT_MAX_MEMBER_SIZE,
                              &member_limit_present, &member_limit) ||
        !xx_rar_get_u64_limit(self, options,
                              XX_META_ID_OPT_MEMORY_LIMIT,
                              &memory_limit_present, &memory_limit)) {
        xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG,
                        "RAR extraction limits must be nonnegative integers");
        return false;
    }
    if (member_limit_present && info->unpacked_size > member_limit) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "RAR member exceeds the configured size limit");
        return false;
    }
    if (!memory_limit_present) return true;

    /* Account the buffers allocated by the format before any allocation is
     * attempted.  Zero-length buffers still use a one-byte allocation. */
    output_allocation = info->unpacked_size ? info->unpacked_size : 1U;
    packed_allocation = (info->is_encrypted || !is_store)
                            ? (info->block.data_size != 0
                                   ? (uint64_t)info->block.data_size
                                   : 1U)
                            : 0U;
    window_allocation = !is_store ? info->window_size : 0U;
    /* The RAR 2.x decoder owns a fixed 1 MiB history even when the header's
     * nominal dictionary is smaller. */
    if (!is_store && info->rarx_method == XX_RARX_METHOD_20 &&
        window_allocation < 0x100000U) {
        window_allocation = 0x100000U;
    }
    if (!xx_rar_add_memory(&required, output_allocation) ||
        !xx_rar_add_memory(&required, packed_allocation) ||
        !xx_rar_add_memory(&required, window_allocation) ||
        required > memory_limit) {
        xx_pd_set_error(pd, XXFC_ERR_OUT_OF_BOUNDS,
                        "RAR extraction exceeds the configured memory limit");
        return false;
    }
    return true;
}

static bool xx_rar_decode_current_record(Abstractformat *self,
                                         xx_rar_stream_state *rar_state,
                                         const xx_list_s *options,
                                         xx_pd_struct *pd) {
    const xx_rar_file_info *info;
    uint8_t *output = NULL;
    uint8_t *packed = NULL;
    size_t output_size;
    size_t packed_size;
    bool is_store;
    bool used_rarx = false;
    bool ok = false;
    uint8_t hash_key[32] = {0};
    uint8_t key[32]={0},iv[16]={0};
    size_t key_size=0;

    if (!self || !self->device || !rar_state ||
        !rar_state->current_info_valid) return false;
    info = &rar_state->current_info;
    if (info->is_split || info->unknown_size ||
        info->unpacked_size > (uint64_t)SIZE_MAX || info->block.data_size < 0 ||
        (uint64_t)info->block.data_size > (uint64_t)SIZE_MAX) return false;
    if (info->is_folder && (info->unpacked_size || info->block.data_size)) return false;
    if (info->is_encrypted && !info->has_crc && !info->has_blake2) return false;

    output_size = (size_t)info->unpacked_size;
    packed_size = (size_t)info->block.data_size;
    is_store = info->is_folder || (info->block.version == XX_RAR_VERSION_4
                   ? info->method == XX_RAR4_METHOD_STORE
                   : info->method == XX_RAR5_METHOD_STORE);
    if (!xx_rar_resource_limits_allow(self, options, info, is_store, pd))
        return false;
    if (rar_state->current_decoded) return true;
    if (info->is_encrypted) {
        if((packed_size&15U) || !xx_rar_member_keys(self,options,info,key,&key_size,iv,hash_key,pd)) goto cleanup;
        packed = (uint8_t *)xx_mem_alloc(packed_size ? packed_size : 1U);
        if (!packed ||
            !xx_rar_read_member_at(self->device, info->block.data_offset, packed, packed_size,pd) ||
            !xx_rar_decrypt_blocks(packed,packed_size,key,key_size,iv,pd)) goto cleanup;
    }
    output=(uint8_t *)xx_mem_alloc(output_size?output_size:1U);
    if(!output) goto cleanup;
    if (is_store) {
        if (info->is_encrypted) {
            if (output_size > packed_size || packed_size - output_size > 15U) goto cleanup;
            size_t copied=0;
            while(copied<output_size) {
                size_t n=output_size-copied>65536U?65536U:output_size-copied;
                if(xx_pd_is_stopped(pd)) goto cleanup;
                xx_rt_memcpy(output+copied,packed+copied,n);copied+=n;
            }
        } else if (packed_size != output_size ||
                   !xx_rar_read_member_at(self->device, info->block.data_offset,
                                         output, output_size,pd)) goto cleanup;
    } else {
        xx_rarx_result result;
        if (info->rarx_method == 0 || info->window_size == 0 ||
            (info->solid && rar_state->solid_chain_gap)) goto cleanup;
        if (!packed) packed = (uint8_t *)xx_mem_alloc(packed_size != 0 ? packed_size : 1U);
        used_rarx = true;
        if (!packed ||
            (!info->is_encrypted && !xx_rar_read_member_at(self->device, info->block.data_offset,
                                  packed, packed_size,pd)) ||
            !xx_rarx_decoder_unpack_memory(rar_state->decoder, packed,
                                           packed_size, output, output_size,
                                           info->rarx_method,
                                           info->window_size, info->solid,
                                           &result, pd) ||
            result.output_written != (int64_t)output_size) goto cleanup;
    }
    if(!xx_rar_verify_member(info,output,output_size,hash_key,pd)) goto cleanup;

    rar_state->decoded_data = output;
    rar_state->decoded_size = output_size;
    rar_state->current_decoded = true;
    if (!info->solid) rar_state->solid_chain_gap = false;
    output = NULL;
    ok = true;

cleanup:
    if (!ok && used_rarx) {
        xx_rarx_decoder_reset(rar_state->decoder);
        rar_state->solid_chain_gap = true;
    }
    xx_rar_secure_clear(packed,packed_size);xx_mem_free(packed);
    xx_rar_secure_clear(hash_key,sizeof(hash_key));
    xx_rar_secure_clear(key,sizeof(key));xx_rar_secure_clear(iv,sizeof(iv));
    xx_rar_secure_clear(output,output_size);
    xx_mem_free(output);
    return ok;
}

static xx_io_device *xx_rar_open_stage_file(const char *destination,
                                             char **stage_path) {
    unsigned int attempt;
    if (!destination || !stage_path) return NULL;
    *stage_path = NULL;
    for (attempt = 0U; attempt < 10000U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *device;
        int length = xx_rt_snprintf(suffix, sizeof(suffix),
                              ".xxfclib.tmp.%u", attempt);
        if (length <= 0 || (size_t)length >= sizeof(suffix)) return NULL;
        candidate = xx_str_concat(destination, suffix);
        if (!candidate) return NULL;
        device = xx_io_file_open(candidate, "wbx");
        if (device) {
            *stage_path = candidate;
            return device;
        }
        xx_str_free(candidate);
    }
    return NULL;
}

static bool xx_rar_write_decoded_file(const char *path, const uint8_t *data,
                                      size_t size, bool overwrite,
                                      xx_pd_struct *pd) {
    xx_io_device *device = NULL;
    char *stage_path = NULL;
    size_t done = 0;
    bool ok = false;
    if (!path || (!data && size != 0)) return false;
    if (!overwrite && xx_io_file_exists_a(path)) return false;
    device = xx_rar_open_stage_file(path, &stage_path);
    if (!device) return false;
    while (done < size) {
        ssize_t amount;
        size_t chunk = size - done;
        if (chunk > 65536U) chunk = 65536U;
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        amount = xx_io_write(device, data + done, chunk);
        if (amount <= 0 || (size_t)amount > chunk) goto cleanup;
        done += (size_t)amount;
    }
    ok = !xx_pd_is_stopped(pd);
cleanup:
    if (xx_io_close(device) != 0) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
    if (stage_path) {
        if (xx_io_file_exists_a(stage_path)) {
            (void)xx_io_file_remove_a(stage_path);
        }
        xx_str_free(stage_path);
    }
    return ok;
}

bool xx_rar_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    const xx_archive_record *record;
    xx_rar_stream_state *rar_state;
    const xx_rar_file_info *info;
    const xx_var *path_option;
    const xx_var *overwrite_option;
    wchar_t *base_path = NULL;
    wchar_t *item_name = NULL;
    wchar_t *destination = NULL;
    bool success = false;

    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) return false;
    rar_state = (xx_rar_stream_state *)state->internal_state;
    if (!rar_state->current_info_valid ||
        !xx_rar_decode_current_record(self, rar_state, &state->options, pd)) return false;
    info = &rar_state->current_info;
    record = &state->current_record;

    overwrite_option = xx_format_resolve_extra_parameter(
        self, &state->options, XX_META_ID_OPT_OVERWRITE);

    path_option = xx_format_resolve_extra_parameter(
        self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_WSTRING ||
        path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        const wchar_t *value = xx_var_get_wstr(path_option);
        base_path = value ? xx_str_wdup(value) : NULL;
    } else if (path_option->type == XX_VAR_TYPE_STRING ||
               path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        const char *value = xx_var_get_str(path_option);
        base_path = value ? xx_str_utf8_to_unicode(value) : NULL;
    }
    if (!base_path) return false;
    {
        const wchar_t *wide_name = xx_archive_record_get_original_name_w(record);
        const char *name = xx_archive_record_get_original_name(record);
        if (wide_name) item_name = xx_str_wdup(wide_name);
        else if (name) item_name = xx_str_utf8_to_unicode(name);
    }
    destination = xx_rar_make_destination(base_path, item_name);
    if (!destination) goto cleanup;
    if (info->is_folder) {
        success = xx_store_create_dirs_w(destination, true);
    } else {
        char *destination_utf8;
        if (!xx_store_create_dirs_w(destination, false)) goto cleanup;
        destination_utf8 = xx_str_unicode_to_utf8(destination);
        if (!destination_utf8) goto cleanup;
        success = xx_rar_write_decoded_file(destination_utf8,
                                            rar_state->decoded_data,
                                            rar_state->decoded_size,
                                            overwrite_option &&
                                                xx_var_get_bool(overwrite_option),
                                            pd);
        xx_rar_secure_clear(destination_utf8,xx_rt_strlen(destination_utf8)+1U);
        xx_str_free(destination_utf8);
        if (success && info->block.version == XX_RAR_VERSION_4) {
            uint32_t packed_time = (uint32_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_TIMESTAMP, 0);
            uint32_t attributes = (uint32_t)xx_archive_record_get_meta_u64(
                record, XX_META_ID_ATTRIBUTES, 0);
            (void)xx_store_apply_dos_time_and_attrs_w(destination,
                                                     (uint16_t)(packed_time >> 16),
                                                     (uint16_t)packed_time,
                                                     attributes);
        }
    }

cleanup:
    if (destination) {
        xx_rar_secure_clear(destination,
            (xx_str_wlen(destination)+1U)*sizeof(*destination));
    }
    if (item_name) {
        xx_rar_secure_clear(item_name,
            (xx_str_wlen(item_name)+1U)*sizeof(*item_name));
    }
    if (base_path) {
        xx_rar_secure_clear(base_path,
            (xx_str_wlen(base_path)+1U)*sizeof(*base_path));
    }
    xx_str_wfree(destination);
    xx_str_wfree(item_name);
    xx_str_wfree(base_path);
    return success;
}

void xx_rar_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    if(state)xx_rar_clear_record_name(&state->current_record);
    xx_archive_record_state_free(state);
}

/* ========================================================================= */
/* --- Data structure streaming                                           --- */
/* ========================================================================= */

typedef struct xx_rar_ds_name_s {
    xx_rar_data_struct_id_t id;
    const char *name;
} xx_rar_ds_name;

static const xx_rar_ds_name g_xx_rar_ds_names[] = {
    {XX_RAR_DS_UNKNOWN, "UNKNOWN"},
    {XX_RAR_DS_SIGNATURE, "SIGNATURE"},
    {XX_RAR_DS_MAIN_HEADER, "MAIN_HEADER"},
    {XX_RAR_DS_FILE_HEADER, "FILE_HEADER"},
    {XX_RAR_DS_SERVICE_HEADER, "SERVICE_HEADER"},
    {XX_RAR_DS_ENCRYPTION_HEADER, "ENCRYPTION_HEADER"},
    {XX_RAR_DS_END_HEADER, "END_HEADER"},
    {XX_RAR_DS_OTHER_HEADER, "OTHER_HEADER"},
    {XX_RAR_DS_DATA, "DATA"}
};

const char *xx_rar_data_struct_id_to_string(Abstractformat *self, uint32_t id) {
    (void)self;
    for (size_t i = 0; i < sizeof(g_xx_rar_ds_names) / sizeof(g_xx_rar_ds_names[0]); ++i) {
        if ((uint32_t)g_xx_rar_ds_names[i].id == id) {
            return g_xx_rar_ds_names[i].name;
        }
    }
    return "UNKNOWN";
}

uint32_t xx_rar_data_struct_string_to_id(Abstractformat *self, const char *name) {
    (void)self;
    if (!name) {
        return XX_RAR_DS_UNKNOWN;
    }
    for (size_t i = 0; i < sizeof(g_xx_rar_ds_names) / sizeof(g_xx_rar_ds_names[0]); ++i) {
        if (xx_str_cmp(name, g_xx_rar_ds_names[i].name) == 0) {
            return (uint32_t)g_xx_rar_ds_names[i].id;
        }
    }
    return XX_RAR_DS_UNKNOWN;
}

typedef struct xx_rar_ds_state_s {
    xx_data_struct *items;
    size_t count;
    size_t capacity;
} xx_rar_ds_state;

static void xx_rar_ds_state_free(void *pointer) {
    xx_rar_ds_state *state = (xx_rar_ds_state *)pointer;
    if (state) {
        xx_mem_free(state->items);
        xx_mem_free(state);
    }
}

static bool xx_rar_ds_append(xx_rar_ds_state *state, uint32_t id, int64_t offset,
                             int64_t address, int64_t entry_size,
                             int64_t total_size, xx_data_struct_type_t type) {
    xx_data_struct *resized;
    xx_data_struct *item;
    size_t new_capacity;
    if (!state || offset < 0 || entry_size < 0 || total_size < 0) {
        return false;
    }
    if (state->count == state->capacity) {
        new_capacity = state->capacity == 0 ? 16U : state->capacity * 2U;
        if (new_capacity < state->capacity ||
            new_capacity > SIZE_MAX / sizeof(*state->items)) {
            return false;
        }
        resized = (xx_data_struct *)xx_mem_realloc(
            state->items, new_capacity * sizeof(*state->items));
        if (!resized) {
            return false;
        }
        state->items = resized;
        state->capacity = new_capacity;
    }
    item = &state->items[state->count++];
    item->id = id;
    item->offset = offset;
    item->address = address;
    item->entry_size = entry_size;
    item->total_size = total_size;
    item->count = 1;
    item->type = type;
    return true;
}

static xx_rar_data_struct_id_t xx_rar_block_ds_id(const xx_rar_block *block) {
    if (!block) {
        return XX_RAR_DS_UNKNOWN;
    }
    if (block->version == XX_RAR_VERSION_4) {
        switch (block->type) {
            case XX_RAR4_HEADER_MAIN: return XX_RAR_DS_MAIN_HEADER;
            case XX_RAR4_HEADER_FILE: return XX_RAR_DS_FILE_HEADER;
            case XX_RAR4_HEADER_SERVICE: return XX_RAR_DS_SERVICE_HEADER;
            case XX_RAR4_HEADER_END: return XX_RAR_DS_END_HEADER;
            default: return XX_RAR_DS_OTHER_HEADER;
        }
    }
    switch (block->type) {
        case XX_RAR5_HEADER_MAIN: return XX_RAR_DS_MAIN_HEADER;
        case XX_RAR5_HEADER_FILE: return XX_RAR_DS_FILE_HEADER;
        case XX_RAR5_HEADER_SERVICE: return XX_RAR_DS_SERVICE_HEADER;
        case XX_RAR5_HEADER_CRYPT: return XX_RAR_DS_ENCRYPTION_HEADER;
        case XX_RAR5_HEADER_END: return XX_RAR_DS_END_HEADER;
        default: return XX_RAR_DS_OTHER_HEADER;
    }
}

xx_data_struct_state *xx_rar_create_data_structs_reading(Abstractformat *self,
                                                         xx_pd_struct *pd) {
    xx_data_struct_state *state;
    xx_rar_ds_state *rar_state;
    xx_rar *rar;
    int64_t offset;
    int64_t scan_end;
    int64_t device_size;

    if (!xx_format_handle_split_format(self, pd) || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid) {
        return NULL;
    }
    rar = (xx_rar *)self;
    device_size = xx_io_total_size(self->device);
    scan_end = self->base_address + self->format_size;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    rar_state = (xx_rar_ds_state *)xx_mem_calloc(1, sizeof(*rar_state));
    if (!state || !rar_state) {
        xx_mem_free(state);
        xx_mem_free(rar_state);
        return NULL;
    }
    xx_data_struct_state_init(state, self);

    if (rar->signature_offset > self->base_address &&
        !xx_rar_ds_append(rar_state, XX_DATA_STRUCT_ID_RAW_DATA,
                          self->base_address,
                          self->is_mapped ? self->base_address : -1,
                          rar->signature_offset - self->base_address,
                          rar->signature_offset - self->base_address,
                          XX_DATA_STRUCT_TYPE_RAW_DATA)) {
        goto error;
    }
    {
        int64_t signature_size = rar->version == XX_RAR_VERSION_4
                                     ? XX_RAR4_SIGNATURE_SIZE
                                     : XX_RAR5_SIGNATURE_SIZE;
        if (!xx_rar_ds_append(rar_state, XX_RAR_DS_SIGNATURE,
                              rar->signature_offset,
                              self->is_mapped ? rar->signature_offset : -1,
                              signature_size, signature_size,
                              XX_DATA_STRUCT_TYPE_STRUCT)) {
            goto error;
        }
    }

    offset = rar->first_header_offset;
    while (offset < scan_end) {
        xx_rar_block block;
        xx_rar_data_struct_id_t id;
        xx_data_struct_type_t type;
        bool encrypted_tail = false;
        if (!xx_rar_parse_block(self->device, device_size, rar->version,
                                offset, &block)) {
            goto error;
        }
        id = xx_rar_block_ds_id(&block);
        type = id == XX_RAR_DS_END_HEADER ? XX_DATA_STRUCT_TYPE_FOOTER
                                         : XX_DATA_STRUCT_TYPE_STRUCT;
        if (!xx_rar_ds_append(rar_state, id, block.offset,
                              self->is_mapped ? block.offset : -1,
                              block.header_size, block.header_size, type)) {
            goto error;
        }
        if (block.data_size > 0 &&
            !xx_rar_ds_append(rar_state, XX_RAR_DS_DATA, block.data_offset,
                              self->is_mapped ? block.data_offset : -1,
                              block.data_size, block.data_size,
                              XX_DATA_STRUCT_TYPE_RAW_DATA)) {
            goto error;
        }
        encrypted_tail = (rar->version == XX_RAR_VERSION_4 &&
                          block.type == XX_RAR4_HEADER_MAIN &&
                          (block.flags & XX_RAR4_MAIN_FLAG_PASSWORD) != 0) ||
                         (rar->version == XX_RAR_VERSION_5 &&
                          block.type == XX_RAR5_HEADER_CRYPT);
        offset = block.next_offset;
        if (encrypted_tail) {
            if (offset < scan_end &&
                !xx_rar_ds_append(rar_state, XX_DATA_STRUCT_ID_RAW_DATA,
                                  offset, self->is_mapped ? offset : -1,
                                  scan_end - offset, scan_end - offset,
                                  XX_DATA_STRUCT_TYPE_RAW_DATA)) {
                goto error;
            }
            break;
        }
        if (id == XX_RAR_DS_END_HEADER) {
            break;
        }
    }

    state->internal_state = rar_state;
    state->free_internal = xx_rar_ds_state_free;
    state->total_structs = (int64_t)rar_state->count;
    if (rar_state->count != 0) {
        state->current_struct = rar_state->items[0];
        state->current_index = 0;
        state->has_struct = true;
    }
    return state;

error:
    xx_rar_ds_state_free(rar_state);
    xx_data_struct_state_free(state);
    return NULL;
}

const xx_data_struct *xx_rar_get_current_data_struct(Abstractformat *self,
                                                     xx_data_struct_state *state) {
    return self && state && state->format == self && state->has_struct
               ? &state->current_struct
               : NULL;
}

bool xx_rar_data_struct_move_to_next(Abstractformat *self,
                                     xx_data_struct_state *state,
                                     xx_pd_struct *pd) {
    xx_rar_ds_state *rar_state;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_struct || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    rar_state = (xx_rar_ds_state *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= rar_state->count) {
        state->has_struct = false;
        return false;
    }
    state->current_struct = rar_state->items[next];
    state->current_index = next;
    state->has_struct = true;
    return true;
}

void xx_rar_free_data_structs_reading(Abstractformat *self,
                                      xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

/* ========================================================================= */
/* --- Data structure field streaming                                     --- */
/* ========================================================================= */

static const xx_data_struct_field_desc g_xx_rar4_signature_fields[] = {
    {L"magic", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"magic_tail", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version", L"uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static const xx_data_struct_field_desc g_xx_rar5_signature_fields[] = {
    {L"magic", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"magic_tail", L"uint16", 4, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"version", L"uint8", 6, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"reserved", L"uint8", 7, 1, XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED}
};

static const xx_data_struct_field_desc g_xx_rar4_base_fields[] = {
    {L"header_crc", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"header_type", L"uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"header_flags", L"uint16", 3, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"header_size", L"uint16", 5, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE}
};

static const xx_data_struct_field_desc g_xx_rar4_file_fields[] = {
    {L"header_crc", L"uint16", 0, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"header_type", L"uint8", 2, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"header_flags", L"uint16", 3, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"header_size", L"uint16", 5, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"packed_size", L"uint32", 7, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"unpacked_size", L"uint32", 11, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"host_os", L"uint8", 15, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"file_crc", L"uint32", 16, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"file_time", L"uint32", 20, 4, XX_DATA_STRUCT_RECORD_PROPERTY_TIMESTAMP},
    {L"unpack_version", L"uint8", 24, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"method", L"uint8", 25, 1, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"name_size", L"uint16", 26, 2, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"attributes", L"uint32", 28, 4, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS}
};

typedef struct xx_rar_value_desc_s {
    const wchar_t *name;
    const wchar_t *type;
    int64_t relative_offset;
    int64_t size;
    xx_data_struct_record_property_t property;
    uint64_t value;
} xx_rar_value_desc;

typedef struct xx_rar_record_state_s {
    const xx_data_struct_field_desc *fixed_fields;
    size_t fixed_count;
    xx_rar_value_desc values[6];
    size_t value_count;
    bool dynamic_values;
} xx_rar_record_state;

static void xx_rar_record_state_free(void *pointer) {
    xx_mem_free(pointer);
}

static bool xx_rar_record_populate_dynamic(xx_data_struct_record *record,
                                           const xx_rar_value_desc *field) {
    char ascii_display[64];
    wchar_t display[64];
    int display_length;
    size_t display_index;
    if (!record || !field ||
        !xx_data_struct_record_set_name(record, field->name) ||
        !xx_data_struct_record_set_type(record, field->type)) {
        return false;
    }
    record->offset = field->relative_offset;
    record->size = field->size;
    record->property = field->property;
    xx_var_set_u64(&record->value, field->value);
    if ((field->property & (XX_DATA_STRUCT_RECORD_PROPERTY_ID |
                            XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS)) != 0) {
        display_length = xx_rt_snprintf(ascii_display, sizeof(ascii_display),
                                        "0x%llX",
                                        (unsigned long long)field->value);
    } else {
        display_length = xx_rt_snprintf(ascii_display, sizeof(ascii_display),
                                        "%llu",
                                        (unsigned long long)field->value);
    }
    if (display_length < 0 ||
        (size_t)display_length >= sizeof(ascii_display)) {
        return false;
    }
    for (display_index = 0U; display_index <= (size_t)display_length;
         ++display_index) {
        display[display_index] =
            (wchar_t)(unsigned char)ascii_display[display_index];
    }
    return xx_data_struct_record_set_display_value(record, display);
}

static bool xx_rar_record_populate_current(Abstractformat *self,
                                           xx_data_struct_record_state *state,
                                           xx_rar_record_state *rar_state,
                                           size_t index) {
    if (rar_state->dynamic_values) {
        return index < rar_state->value_count &&
               xx_rar_record_populate_dynamic(&state->current_record,
                                              &rar_state->values[index]);
    }
    return index < rar_state->fixed_count &&
           xx_data_struct_record_populate(&state->current_record, self->device,
                                          state->parent_struct.offset,
                                          &rar_state->fixed_fields[index], false);
}

static void xx_rar_value_add(xx_rar_record_state *state, const wchar_t *name,
                             const wchar_t *type, int64_t offset, int64_t size,
                             xx_data_struct_record_property_t property,
                             uint64_t value) {
    xx_rar_value_desc *field;
    if (!state || state->value_count >= sizeof(state->values) / sizeof(state->values[0])) {
        return;
    }
    field = &state->values[state->value_count++];
    field->name = name;
    field->type = type;
    field->relative_offset = offset;
    field->size = size;
    field->property = property;
    field->value = value;
}

xx_data_struct_record_state *xx_rar_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *data_struct, xx_pd_struct *pd) {
    xx_data_struct_record_state *state;
    xx_rar_record_state *rar_state;
    xx_rar *rar;
    if (!xx_format_handle_split_format(self, pd) || !self->device || !data_struct ||
        data_struct->type == XX_DATA_STRUCT_TYPE_RAW_DATA) {
        return NULL;
    }
    rar = (xx_rar *)self;
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    rar_state = (xx_rar_record_state *)xx_mem_calloc(1, sizeof(*rar_state));
    if (!state || !rar_state) {
        xx_mem_free(state);
        xx_mem_free(rar_state);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, data_struct);

    if (data_struct->id == XX_RAR_DS_SIGNATURE) {
        if (rar->version == XX_RAR_VERSION_4) {
            rar_state->fixed_fields = g_xx_rar4_signature_fields;
            rar_state->fixed_count = sizeof(g_xx_rar4_signature_fields) /
                                     sizeof(g_xx_rar4_signature_fields[0]);
        } else {
            rar_state->fixed_fields = g_xx_rar5_signature_fields;
            rar_state->fixed_count = sizeof(g_xx_rar5_signature_fields) /
                                     sizeof(g_xx_rar5_signature_fields[0]);
        }
    } else if (rar->version == XX_RAR_VERSION_4) {
        if (data_struct->id == XX_RAR_DS_FILE_HEADER ||
            data_struct->id == XX_RAR_DS_SERVICE_HEADER) {
            rar_state->fixed_fields = g_xx_rar4_file_fields;
            rar_state->fixed_count = sizeof(g_xx_rar4_file_fields) /
                                     sizeof(g_xx_rar4_file_fields[0]);
        } else {
            rar_state->fixed_fields = g_xx_rar4_base_fields;
            rar_state->fixed_count = sizeof(g_xx_rar4_base_fields) /
                                     sizeof(g_xx_rar4_base_fields[0]);
        }
    } else {
        xx_rar_block block;
        uint64_t header_data_size;
        int64_t device_size = xx_io_total_size(self->device);
        if (!xx_rar_parse_block(self->device, device_size, rar->version,
                                data_struct->offset, &block)) {
            xx_rar_record_state_free(rar_state);
            xx_data_struct_record_state_free(state);
            return NULL;
        }
        header_data_size = (uint64_t)(block.header_size - 4 - block.header_size_width);
        rar_state->dynamic_values = true;
        xx_rar_value_add(rar_state, L"header_crc", L"uint32", 0, 4,
                         XX_DATA_STRUCT_RECORD_PROPERTY_ID,
                         xx_io_get_u32(self->device, block.offset, false));
        xx_rar_value_add(rar_state, L"header_size", L"vint", block.header_size_rel,
                         block.header_size_width, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE,
                         header_data_size);
        xx_rar_value_add(rar_state, L"header_type", L"vint", block.type_rel,
                         block.type_width, XX_DATA_STRUCT_RECORD_PROPERTY_ID,
                         block.type);
        xx_rar_value_add(rar_state, L"header_flags", L"vint", block.flags_rel,
                         block.flags_width, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS,
                         block.flags);
        if (block.extra_size_rel >= 0) {
            xx_rar_value_add(rar_state, L"extra_area_size", L"vint",
                             block.extra_size_rel, block.extra_size_width,
                             XX_DATA_STRUCT_RECORD_PROPERTY_SIZE, block.extra_size);
        }
        if (block.data_size_rel >= 0) {
            xx_rar_value_add(rar_state, L"data_size", L"vint",
                             block.data_size_rel, block.data_size_width,
                             XX_DATA_STRUCT_RECORD_PROPERTY_SIZE,
                             (uint64_t)block.data_size);
        }
    }

    if ((!rar_state->dynamic_values && rar_state->fixed_count == 0) ||
        (rar_state->dynamic_values && rar_state->value_count == 0)) {
        xx_rar_record_state_free(rar_state);
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    state->internal_state = rar_state;
    state->free_internal = xx_rar_record_state_free;
    state->total_records = (int64_t)(rar_state->dynamic_values
                                         ? rar_state->value_count
                                         : rar_state->fixed_count);
    if (!xx_rar_record_populate_current(self, state, rar_state, 0)) {
        xx_data_struct_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_data_struct_record *xx_rar_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rar_data_struct_record_move_to_next(Abstractformat *self,
                                            xx_data_struct_record_state *state,
                                            xx_pd_struct *pd) {
    xx_rar_record_state *rar_state;
    int64_t next;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    rar_state = (xx_rar_record_state *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || next >= state->total_records) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_rar_record_populate_current(self, state, rar_state, (size_t)next)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    state->has_record = true;
    return true;
}

void xx_rar_free_data_struct_records_reading(Abstractformat *self,
                                             xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}
