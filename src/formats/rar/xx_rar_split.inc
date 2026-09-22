/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Private implementation included by xx_rar.c after its raw header parsers.
 * A normalized, read-only archive consists of owned small headers and borrowed
 * packed-data ranges. Source bytes are never modified or copied wholesale.
 */

#define XX_RAR_SPLIT_MAX_RANGES 1048576U

typedef struct xx_rar_split_range_s {
    xx_io_volume span;
    uint8_t *owned;
} xx_rar_split_range;

typedef struct xx_rar_split_view_s {
    xx_io_device *original;
    xx_io_device *view;
    int64_t original_base;
    xx_rar_split_range *ranges;
    size_t count;
    size_t capacity;
    struct xx_rar_split_view_s *parent;
    struct xx_rar_split_view_s **children;
    size_t child_count;
    bool headers_encrypted;
} xx_rar_split_view;

static void xx_rar_split_free(xx_rar_split_view *view) {
    size_t i;
    if (!view) return;
    if (view->view) xx_io_close(view->view);
    for (i = 0; i < view->count; ++i) {
        if (view->ranges[i].owned) {
            xx_io_close(view->ranges[i].span.device);
            xx_rar_secure_clear(view->ranges[i].owned,(size_t)view->ranges[i].span.size);
            xx_mem_free(view->ranges[i].owned);
        }
    }
    xx_mem_free(view->ranges);
    for (i = 0; i < view->child_count; ++i) xx_rar_split_free(view->children[i]);
    xx_mem_free(view->children);
    xx_rar_split_free(view->parent);
    xx_mem_free(view);
}

static bool xx_rar_split_append(xx_rar_split_view *view, xx_io_device *device,
                                int64_t offset, int64_t size) {
    xx_rar_split_range *grown;
    size_t capacity;
    if (view->count >= XX_RAR_SPLIT_MAX_RANGES) return false;
    if (view->count == view->capacity) {
        capacity = view->capacity ? view->capacity * 2U : 32U;
        grown = (xx_rar_split_range *)xx_mem_realloc(
            view->ranges, capacity * sizeof(*grown));
        if (!grown) return false;
        view->ranges = grown;
        view->capacity = capacity;
    }
    xx_mem_zero(&view->ranges[view->count], sizeof(*view->ranges));
    view->ranges[view->count].span.device = device;
    view->ranges[view->count].span.offset = offset;
    view->ranges[view->count].span.size = size;
    ++view->count;
    return true;
}

/* Takes the header buffer only on success. */
static bool xx_rar_split_header(xx_rar_split_view *view, size_t index,
                                uint8_t *bytes, size_t size) {
    xx_io_device *memory = xx_io_mem_open_ro(bytes, size);
    if (!memory) return false;
    if (index == view->count && !xx_rar_split_append(view, NULL, 0, 0)) {
        xx_io_close(memory);
        return false;
    }
    view->ranges[index].span.device = memory;
    view->ranges[index].span.offset = 0;
    view->ranges[index].span.size = (int64_t)size;
    view->ranges[index].owned = bytes;
    return true;
}

static void xx_rar_split_u16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}

static void xx_rar_split_u32(uint8_t *p, uint32_t n) {
    unsigned i;
    for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8U * i));
}

static size_t xx_rar_split_vint(uint8_t *p, uint64_t n) {
    size_t count = 0;
    do {
        p[count] = (uint8_t)(n & 127U);
        n >>= 7;
        if (n) p[count] |= 128U;
        ++count;
    } while (n);
    return count;
}

static bool xx_rar_split_block5(xx_rar_split_view *view, size_t index,
                               const uint8_t *body, size_t size) {
    uint8_t *header;
    size_t width;
    if (size > XX_RAR5_MAX_HEADER_SIZE) return false;
    header = (uint8_t *)xx_mem_alloc(size + 14U);
    if (!header) return false;
    width = xx_rar_split_vint(header + 4, size);
    xx_rt_memcpy(header + 4 + width, body, size);
    xx_rar_split_u32(header, xx_crc32_calc(0, header + 4, width + size));
    if (!xx_rar_split_header(view, index, header, 4U + width + size)) {
        xx_rar_secure_clear(header, size + 14U);
        xx_mem_free(header);
        return false;
    }
    return true;
}

static bool xx_rar_split_marker(xx_rar_split_view *view,
                                xx_rar_version_t version, bool end) {
    uint8_t *bytes;
    size_t size;
    if (version == XX_RAR_VERSION_5) {
        uint8_t body[3] = {1, 0, 0};
        if (end) body[0] = 5;
        return xx_rar_split_block5(view, view->count, body, sizeof(body));
    }
    size = end ? 7U : 13U;
    bytes = (uint8_t *)xx_mem_calloc(size, 1U);
    if (!bytes) return false;
    bytes[2] = end ? XX_RAR4_HEADER_END : XX_RAR4_HEADER_MAIN;
    xx_rar_split_u16(bytes + 5, (uint16_t)size);
    xx_rar_split_u16(bytes, (uint16_t)xx_crc32_calc(0, bytes + 2, size - 2U));
    if (!xx_rar_split_header(view, view->count, bytes, size)) {
        xx_mem_free(bytes);
        return false;
    }
    return true;
}

static bool xx_rar_split_file_header(xx_rar_split_view *view, size_t index,
                                     const xx_rar_file_info *first,
                                     const xx_rar_file_info *last,
                                     const uint8_t *name, int64_t packed) {
    uint8_t *body;
    size_t n = 0;
    bool ok;
    uint8_t extra[96];
    size_t extra_size = 0;
    if (first->name_size > XX_RAR5_MAX_HEADER_SIZE - 192U) return false;
    body = (uint8_t *)xx_mem_calloc(first->name_size + 192U, 1U);
    if (!body) return false;
    if (first->block.version == XX_RAR_VERSION_5) {
        if (first->is_encrypted) {
            if (first->crypto_version != 5) {
                xx_rar_secure_clear(body,first->name_size+192U);
                xx_mem_free(body); return false;
            }
            extra[0] = (uint8_t)(36U + ((first->crypto_flags & 1U) ? 12U : 0U));
            extra[1] = 1; extra[2] = 0; extra[3] = last->crypto_flags;
            extra[4] = first->crypto_log;
            xx_rt_memcpy(extra + 5, first->crypto_salt, 16);
            xx_rt_memcpy(extra + 21, first->crypto_iv, 16);
            if (first->crypto_flags & 1U) xx_rt_memcpy(extra + 37, first->crypto_check, 12);
            extra_size = (size_t)extra[0] + 1U;
        }
        if (last->has_blake2) {
            extra[extra_size++]=34;extra[extra_size++]=2;extra[extra_size++]=0;
            xx_rt_memcpy(extra+extra_size,last->blake2,32);extra_size+=32;
        }
        n += xx_rar_split_vint(body + n, XX_RAR5_HEADER_FILE);
        n += xx_rar_split_vint(body + n, XX_RAR5_BLOCK_FLAG_DATA |
                             (extra_size ? XX_RAR5_BLOCK_FLAG_EXTRA : 0));
        if (extra_size) n += xx_rar_split_vint(body + n, extra_size);
        n += xx_rar_split_vint(body + n, (uint64_t)packed);
        n += xx_rar_split_vint(body + n, first->file_flags);
        n += xx_rar_split_vint(body + n, last->unpacked_size);
        n += xx_rar_split_vint(body + n, first->attributes);
        if (first->file_flags & XX_RAR5_FILE_FLAG_MTIME) {
            xx_rar_split_u32(body + n, (uint32_t)first->timestamp); n += 4;
        }
        if (first->has_crc) {
            xx_rar_split_u32(body + n, last->data_crc); n += 4;
        }
        n += xx_rar_split_vint(body + n, first->compression_info);
        n += xx_rar_split_vint(body + n, first->host_os);
        n += xx_rar_split_vint(body + n, first->name_size);
        xx_rt_memcpy(body + n, name, first->name_size); n += first->name_size;
        if (extra_size) { xx_rt_memcpy(body + n, extra, extra_size); n += extra_size; }
        ok = xx_rar_split_block5(view, index, body, n);
        xx_rar_secure_clear(body,first->name_size+192U);
        xx_rar_secure_clear(extra,sizeof(extra));
        xx_mem_free(body);
        return ok;
    } else {
        bool large = (uint64_t)packed > UINT32_MAX || last->unpacked_size > UINT32_MAX;
        uint16_t flags = (uint16_t)(first->file_flags &
            (XX_RAR4_FILE_FLAG_SOLID | XX_RAR4_FILE_FLAG_DIRECTORY |
             XX_RAR4_FILE_FLAG_UNICODE | XX_RAR4_FILE_FLAG_PASSWORD | XX_RAR4_FILE_FLAG_SALT));
        flags |= XX_RAR4_FLAG_LONG_BLOCK;
        if (large) flags |= XX_RAR4_FILE_FLAG_LARGE;
        n = large ? 40U : 32U;
        if (first->name_size > UINT16_MAX - n - ((flags & XX_RAR4_FILE_FLAG_SALT) ? 8U : 0U))
            { xx_rar_secure_clear(body,first->name_size+192U);
              xx_mem_free(body); return false; }
        body[2] = XX_RAR4_HEADER_FILE;
        xx_rar_split_u16(body + 3, flags);
        xx_rar_split_u16(body + 5, (uint16_t)(n + first->name_size +
                            ((flags & XX_RAR4_FILE_FLAG_SALT) ? 8U : 0U)));
        xx_rar_split_u32(body + 7, (uint32_t)packed);
        xx_rar_split_u32(body + 11, (uint32_t)last->unpacked_size);
        body[15] = (uint8_t)first->host_os;
        xx_rar_split_u32(body + 16, last->data_crc);
        xx_rar_split_u32(body + 20, (uint32_t)first->timestamp);
        body[24] = (uint8_t)first->unpack_version;
        body[25] = (uint8_t)first->method;
        xx_rar_split_u16(body + 26, (uint16_t)first->name_size);
        xx_rar_split_u32(body + 28, (uint32_t)first->attributes);
        if (large) {
            xx_rar_split_u32(body + 32, (uint32_t)((uint64_t)packed >> 32));
            xx_rar_split_u32(body + 36, (uint32_t)(last->unpacked_size >> 32));
        }
        xx_rt_memcpy(body + n, name, first->name_size); n += first->name_size;
        if (flags & XX_RAR4_FILE_FLAG_SALT) { xx_rt_memcpy(body + n, first->crypto_salt, 8); n += 8; }
        xx_rar_split_u16(body, (uint16_t)xx_crc32_calc(0, body + 2, n - 2));
        if (!xx_rar_split_header(view, index, body, n)) {
            xx_rar_secure_clear(body,first->name_size+192U);
            xx_mem_free(body); return false;
        }
        return true;
    }
}

static bool xx_rar_split_main(xx_io_device *device, const xx_rar_block *block,
                              uint64_t *flags, uint64_t *number) {
    int64_t p;
    *number = 0;
    if (block->version == XX_RAR_VERSION_4) {
        if (block->type != XX_RAR4_HEADER_MAIN || block->header_size < 13 ||
            (block->flags & XX_RAR4_MAIN_FLAG_PASSWORD)) return false;
        *flags = block->flags;
        return true;
    }
    p = xx_rar5_specific_fields_offset(block);
    if (block->type != XX_RAR5_HEADER_MAIN ||
        !xx_rar_validate_block5_semantics(device, block) ||
        !xx_rar_read_vint(device, &p, block->data_offset - (int64_t)block->extra_size,
                          flags, NULL)) return false;
    return !(*flags & 2U) || xx_rar_read_vint(device, &p, block->data_offset,
                                            number, NULL);
}

static bool xx_rar_split_same_file(const xx_rar_file_info *a,
                                  const xx_rar_file_info *b) {
    uint32_t mask = a->block.version == XX_RAR_VERSION_4 ? ~3U : UINT32_MAX;
    return a->name_size == b->name_size && a->unpacked_size == b->unpacked_size &&
           a->attributes == b->attributes && a->timestamp == b->timestamp &&
           a->compression_info == b->compression_info && a->method == b->method &&
           a->unpack_version == b->unpack_version && a->host_os == b->host_os &&
           a->has_blake2 == b->has_blake2 &&
           a->crypto_version == b->crypto_version &&
           (a->crypto_flags & ~2U) == (b->crypto_flags & ~2U) &&
           a->crypto_log == b->crypto_log && !xx_rt_memcmp(a->crypto_salt, b->crypto_salt, 16) &&
           !xx_rt_memcmp(a->crypto_iv, b->crypto_iv, 16) && !xx_rt_memcmp(a->crypto_check, b->crypto_check, 12) &&
           (a->file_flags & mask) == (b->file_flags & mask);
}

static bool xx_rar_split_extras_supported(xx_io_device *device,
                                         const xx_rar_block *block) {
    int64_t p = block->data_offset - (int64_t)block->extra_size;
    while (p < block->data_offset) {
        uint64_t size, type;
        int64_t end;
        if (!xx_rar_read_vint(device, &p, block->data_offset, &size, NULL) ||
            !size || size > (uint64_t)(block->data_offset - p)) return false;
        end = p + (int64_t)size;
        if (!xx_rar_read_vint(device, &p, end, &type, NULL)) return false;
        /* Encryption metadata is retained; redirected/reference entries cannot
         * become ordinary files when omitting ancillary extra metadata. */
        if (type == 5U) return false;
        p = end;
    }
    return true;
}

static bool xx_rar_split_zero_tail(xx_io_device *device, int64_t start,
                                  int64_t end, xx_pd_struct *pd) {
    uint8_t buffer[4096];
    while (start < end) {
        size_t n = end - start > (int64_t)sizeof(buffer) ? sizeof(buffer) : (size_t)(end-start);
        size_t i;
        if (xx_pd_is_stopped(pd) || !xx_rar_read_exact_at(device, start, buffer, n)) return false;
        for (i = 0; i < n; ++i) if (buffer[i]) return false;
        start += (int64_t)n;
    }
    return true;
}

static bool xx_rar_prepare_split_source(Abstractformat *self, xx_pd_struct *pd,
                                         bool headers_encrypted) {
    xx_rar *rar = (xx_rar *)self;
    xx_rar_version_t version;
    xx_rar_block block;
    xx_rar_split_view *view = NULL;
    xx_rar_file_info first = {0};
    xx_io_volume *spans = NULL;
    xx_io_device *source;
    int64_t signature, total, packed = 0;
    uint64_t main_flags, volume_number, original_flags;
    size_t count, volume_index, header_index = 0, previous_volume = 0, i;
    size_t signature_size;
    uint8_t *name = NULL;
    bool pending = false, ok = false;
    if (!self || !self->device || xx_pd_is_stopped(pd)) return false;
    if (rar->split_view) return true;
    source = self->device;
    /* A plain device may intentionally inspect just one physical volume.
     * Keep that legacy enumeration path (split entries cannot be unpacked).
     * An explicit one-element volume table still represents a complete set
     * and must pass all continuation/end checks below. */
    count = xx_io_multivolume_count(source);
    if (!count) return true;
    total = xx_io_total_size(source);
    if (!xx_rar_find_signature(self, &version, &signature)) return false;
    signature_size = version == XX_RAR_VERSION_5 ? 8U : 7U;
    if (!xx_rar_parse_block(source, total, version, signature + (int64_t)signature_size, &block)) return false;
    /* Ordinary archives (including raw byte-split archives) need no mapping.
     * Header encryption without a supplied password remains detectable through
     * the legacy metadata-only path; supplied passwords are handled upstream. */
    if (version == XX_RAR_VERSION_5 && block.type == XX_RAR5_HEADER_CRYPT) return true;
    if (version == XX_RAR_VERSION_4 && block.type == XX_RAR4_HEADER_MAIN &&
        !(block.flags & 1U)) return true;
    if (!xx_rar_split_main(source, &block, &main_flags, &volume_number)) return false;
    if (!(main_flags & 1U)) return true;
    original_flags = main_flags;
    if (count > XX_RAR_SPLIT_MAX_RANGES) return false;
    view = (xx_rar_split_view *)xx_mem_calloc(1, sizeof(*view));
    if (!view) return false;
    view->original = source;
    view->original_base = self->base_address;
    if (!xx_rar_split_append(view, source, signature, (int64_t)signature_size) ||
        !xx_rar_split_marker(view, version, false)) goto done;
    for (volume_index = 0; volume_index < count; ++volume_index) {
        xx_io_volume volume;
        int64_t start = 0, end = total, offset;
        uint8_t magic[8];
        bool saw_end = false;
        if (xx_pd_is_stopped(pd)) goto done;
        if (xx_io_multivolume_count(source)) {
            if (!xx_io_multivolume_get_volume(source, volume_index, &volume, &start) ||
                !xx_rar_add_i64(start, volume.size, &end)) goto done;
        }
        offset = volume_index == 0 ? signature : start;
        if (!xx_rar_range_valid(end, offset, (int64_t)signature_size) ||
            !xx_rar_read_exact_at(source, offset, magic, signature_size) ||
            xx_rt_memcmp(magic, version == XX_RAR_VERSION_5 ? g_xx_rar5_signature : g_xx_rar4_signature,
                   signature_size)) goto done;
        offset += (int64_t)signature_size;
        if (!xx_rar_parse_block(source, end, version, offset, &block) ||
            !xx_rar_split_main(source, &block, &main_flags, &volume_number) ||
            !(main_flags & 1U)) goto done;
        if (version == XX_RAR_VERSION_5) {
            if (volume_number != volume_index ||
                ((main_flags & 2U) != 0) != (volume_index != 0) ||
                (main_flags & ~3ULL) != (original_flags & ~3ULL)) goto done;
        } else {
            if (((main_flags & 0x100U) != 0) != (volume_index == 0) ||
                (main_flags & ~0x100ULL) != (original_flags & ~0x100ULL)) goto done;
        }
        offset = block.next_offset;
        while (offset < end) {
            bool is_file;
            if (xx_pd_is_stopped(pd) ||
                !xx_rar_parse_block(source, end, version, offset, &block)) goto done;
            if (version == XX_RAR_VERSION_5 && !xx_rar_validate_block5_semantics(source, &block)) goto done;
            is_file = block.type == (version == XX_RAR_VERSION_5 ? XX_RAR5_HEADER_FILE : XX_RAR4_HEADER_FILE);
            if (is_file) {
                xx_rar_file_info info;
                bool before, after;
                uint8_t *part_name;
                if (!xx_rar_parse_file_info(source, &block, &info, headers_encrypted) ||
                    info.unknown_size || (!info.has_crc && !info.has_blake2 && !info.is_folder) ||
                    (info.is_folder && (info.unpacked_size || block.data_size))) goto done;
                if (version == XX_RAR_VERSION_5 &&
                    !xx_rar_split_extras_supported(source, &block)) goto done;
                before = (block.flags & (version == XX_RAR_VERSION_5 ? 8U : 1U)) != 0;
                after = (block.flags & (version == XX_RAR_VERSION_5 ? 16U : 2U)) != 0;
                if (before != pending || (before && previous_volume + 1 != volume_index) ||
                    (info.is_folder && (before || after))) goto done;
                part_name = (uint8_t *)xx_mem_alloc(info.name_size);
                if (!part_name) goto done;
                if (!xx_rar_read_exact_at(source, info.name_offset, part_name, info.name_size)) {
                    xx_rar_secure_clear(part_name,info.name_size);
                    xx_mem_free(part_name); goto done;
                }
                if (before) {
                    bool same = xx_rar_split_same_file(&first, &info) &&
                                !xx_rt_memcmp(name, part_name, info.name_size);
                    xx_rar_secure_clear(part_name,info.name_size);
                    xx_mem_free(part_name);
                    if (!same) goto done;
                } else {
                    first = info;
                    name = part_name;
                    packed = 0;
                    header_index = view->count;
                    if (!xx_rar_split_append(view, NULL, 0, 0)) goto done;
                }
                if (!xx_rar_add_i64(packed, block.data_size, &packed) ||
                    !xx_rar_split_append(view, source, block.data_offset, block.data_size)) goto done;
                /* Intermediate checksums cover this part's packed ciphertext.
                 * Honor HASHMAC when declared (normally clear on split parts).
                 * Final unpacked checksums are retained for the decoder. */
                if(after && !xx_rar_verify_fragment(self,source,&info,pd)) goto done;
                pending = after;
                previous_volume = volume_index;
                if (!after) {
                    if (!xx_rar_split_file_header(view, header_index, &first, &info, name, packed)) goto done;
                    xx_rar_secure_clear(name,first.name_size);
                    xx_mem_free(name); name = NULL;
                }
            } else if (block.type == (version == XX_RAR_VERSION_5 ? XX_RAR5_HEADER_END : XX_RAR4_HEADER_END)) {
                uint64_t end_flags;
                int64_t p;
                if (version == XX_RAR_VERSION_5) {
                    p = xx_rar5_specific_fields_offset(&block);
                    if (!xx_rar_read_vint(source, &p, block.data_offset, &end_flags, NULL)) goto done;
                } else {
                    end_flags = block.flags;
                    p = block.offset + 7 + ((end_flags & 2U) ? 4 : 0);
                    /* Numbered ENDARC blocks are required for safe ordering. */
                    if (!(end_flags & 8U) || p + 2 > block.data_offset ||
                        xx_io_get_u16(source, p, false) != volume_index) goto done;
                }
                if (((end_flags & 1U) != 0) != (volume_index + 1 < count) ||
                    (pending && volume_index + 1 == count) ||
                    !xx_rar_split_zero_tail(source, block.next_offset, end, pd)) goto done;
                saw_end = true;
                break;
            } else if (block.type != (version == XX_RAR_VERSION_5 ? XX_RAR5_HEADER_SERVICE : XX_RAR4_HEADER_SERVICE)) {
                goto done;
            }
            offset = block.next_offset;
        }
        if (!saw_end) goto done;
    }
    if (pending || !xx_rar_split_marker(view, version, true) || xx_pd_is_stopped(pd)) goto done;
    spans = (xx_io_volume *)xx_mem_alloc(view->count * sizeof(*spans));
    if (!spans) goto done;
    for (i = 0; i < view->count; ++i) spans[i] = view->ranges[i].span;
    view->view = xx_io_multivolume_open(spans, view->count, false);
    if (!view->view || xx_pd_is_stopped(pd)) goto done;
    view->view->write = NULL;
    self->device = view->view;
    self->base_address = 0;
    rar->split_view = view;
    view = NULL;
    ok = true;
done:
    if(name)xx_rar_secure_clear(name,first.name_size);
    xx_mem_free(name);
    xx_mem_free(spans);
    xx_rar_split_free(view);
    if (!ok) xx_pd_set_error(pd, XXFC_ERR_INVALID_ARG, "Invalid or unsupported RAR volume chain");
    return ok;
}

static void xx_rar_split_cleanup(xx_rar *rar) {
    xx_rar_split_view *view = (xx_rar_split_view *)rar->split_view;
    if (!view) return;
    rar->format.device = view->original;
    rar->format.base_address = view->original_base;
    rar->split_view = NULL;
    rar->format.split_format_handled = false;
    xx_rar_split_free(view);
}

#include "xx_rar_headers.inc"

bool xx_rar_handle_split_format(Abstractformat *self, xx_pd_struct *pd) {
    xx_rar *rar = (xx_rar *)self;
    xx_rar staged;
    xx_rar_split_view *decrypted = NULL, *prepared;
    if (!self || !self->device || xx_pd_is_stopped(pd)) return false;
    if (rar->split_view) {
        if (self->split_format_handled) return true;
        /* Password changes invalidate preparation; never reuse headers decoded
         * under an earlier password after such a change. */
        xx_rar_split_cleanup(rar);
    }
    if (!xx_rar_decrypt_headers(self, pd, &decrypted)) return false;
    if (!decrypted) return xx_rar_prepare_split_source(self, pd, false);
    staged = *rar;
    staged.format.device = decrypted->view;
    staged.format.base_address = 0;
    staged.split_view = NULL;
    if (xx_io_multivolume_count(self->device) &&
        !xx_rar_prepare_split_source(&staged.format, pd, true)) {
        xx_rar_split_free(decrypted); return false;
    }
    prepared = (xx_rar_split_view *)staged.split_view;
    if (prepared) {
        prepared->parent = decrypted;
        prepared->original = self->device;
        prepared->original_base = self->base_address;
        prepared->headers_encrypted = true;
    } else prepared = decrypted;
    self->device = prepared->view; self->base_address = 0;
    rar->split_view = prepared;
    return true;
}
