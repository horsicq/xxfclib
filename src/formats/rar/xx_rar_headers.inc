/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Encrypted header views own only decrypted headers. Member ciphertext stays
 * borrowed from the caller, including across native or raw volume boundaries.
 */

static bool xx_rar_split_finish(xx_rar_split_view *view) {
    xx_io_volume *spans;
    size_t i;
    if (!view->count) return false;
    spans = (xx_io_volume *)xx_mem_alloc(view->count * sizeof(*spans));
    if (!spans) return false;
    for (i = 0; i < view->count; ++i) spans[i] = view->ranges[i].span;
    view->view = xx_io_multivolume_open(spans, view->count, false);
    xx_mem_free(spans);
    if (!view->view) return false;
    view->view->write = NULL;
    return true;
}

static bool xx_rar_header_key5(xx_io_device *source, const xx_rar_block *crypt,
                               const uint8_t *password, size_t password_size,
                               uint8_t key[32], xx_pd_struct *pd) {
    uint64_t version, flags;
    int64_t p = xx_rar5_specific_fields_offset(crypt), end = crypt->data_offset;
    uint8_t log, salt[16], stored[12], check[8] = {0}, hash[32] = {0};
    bool ok = false;
    if (crypt->type != XX_RAR5_HEADER_CRYPT || crypt->data_size || crypt->extra_size ||
        !xx_rar_read_vint(source, &p, end, &version, NULL) || version ||
        !xx_rar_read_vint(source, &p, end, &flags, NULL) || flags > 1U ||
        end - p != 17 + ((flags & 1U) ? 12 : 0) ||
        !xx_rar_read_exact_at(source, p, &log, 1) ||
        !xx_rar_read_exact_at(source, p + 1, salt, 16) ||
        !xx_rar5_aes_derive(password, password_size, salt, log, key, hash, check, pd)) goto done;
    if ((flags & 1U) && (!xx_rar_read_exact_at(source, p + 17, stored, 12) ||
                         !xx_rar5_aes_check_password(stored, check))) goto done;
    ok = true;
done:
    xx_mem_zero(hash, sizeof(hash)); xx_mem_zero(check, sizeof(check));
    return ok;
}

static bool xx_rar_read_encrypted_header(xx_io_device *source, int64_t offset,
                                         int64_t end, xx_rar_version_t version,
                                         const uint8_t *password, size_t password_size,
                                         const uint8_t key5[32], uint8_t **plain,
                                         xx_rar_block *block, int64_t *data_offset,
                                         xx_pd_struct *pd) {
    uint8_t prefix[16], first[16], key[32] = {0}, iv[16] = {0}, *data = NULL;
    size_t prefix_size = version == XX_RAR_VERSION_4 ? 8U : 16U;
    size_t header_size, rounded=0, key_size, i;
    uint64_t value = 0;
    xx_io_device *memory = NULL;
    bool ok = false;
    *plain = NULL;
    if (xx_pd_is_stopped(pd) || !xx_rar_range_valid(end, offset, (int64_t)prefix_size + 16) ||
        !xx_rar_read_exact_at(source, offset, prefix, prefix_size)) goto done;
    if (version == XX_RAR_VERSION_4) {
        key_size = 16;
        if (!xx_rar_derive3(password, password_size, prefix, key, iv, pd)) goto done;
    } else {
        key_size = 32; xx_rt_memcpy(key, key5, 32); xx_rt_memcpy(iv, prefix, 16);
    }
    if (!xx_rar_read_exact_at(source, offset + (int64_t)prefix_size, first, 16) ||
        !xx_aes_cbc_decrypt(first, 16, key, key_size, iv, first)) goto done;
    if (version == XX_RAR_VERSION_4) {
        header_size = (size_t)first[5] | ((size_t)first[6] << 8);
        if (header_size < 7) goto done;
    } else {
        for (i = 0; i < 10; ++i) {
            uint8_t byte = first[4 + i];
            if (i == 9 && (byte & 0xfeU)) goto done;
            value |= (uint64_t)(byte & 127U) << (7U * i);
            if (!(byte & 128U)) break;
        }
        if (i == 10 || value > XX_RAR5_MAX_HEADER_SIZE) goto done;
        header_size = 5U + i + (size_t)value;
    }
    if (header_size > XX_RAR5_MAX_HEADER_SIZE) goto done;
    rounded = (header_size + 15U) & ~(size_t)15U;
    if (!xx_rar_range_valid(end, offset, (int64_t)(prefix_size + rounded))) goto done;
    data = (uint8_t *)xx_mem_alloc(rounded);
    if (!data || !xx_rar_read_exact_at(source, offset + (int64_t)prefix_size, data, rounded) ||
        !xx_aes_cbc_decrypt(data, rounded, key, key_size, iv, data)) goto done;
    memory = xx_io_mem_open_ro(data, header_size);
    if (!memory || !xx_rar_parse_block(memory, INT64_MAX, version, 0, block) ||
        block->header_size != (int64_t)header_size ||
        (version == XX_RAR_VERSION_5 && !xx_rar_validate_block5_semantics(memory, block))) goto done;
    *data_offset = offset + (int64_t)(prefix_size + rounded);
    if (!xx_rar_range_valid(end, *data_offset, block->data_size)) goto done;
    xx_rar_secure_clear(data+header_size,rounded-header_size);
    *plain = data; data = NULL; ok = true;
done:
    if (memory) xx_io_close(memory);
    xx_rar_secure_clear(data,rounded);xx_mem_free(data);
    xx_rar_secure_clear(first,sizeof(first));xx_rar_secure_clear(key,sizeof(key));xx_rar_secure_clear(iv,sizeof(iv));
    return ok;
}

static bool xx_rar_header_volume(xx_io_device *source, int64_t start, int64_t end,
                                  xx_rar_version_t version, const uint8_t *password,
                                  size_t password_size, xx_rar_split_view *view,
                                  xx_pd_struct *pd) {
    xx_rar_block block = {0};
    uint8_t key[32] = {0}, magic[8], *header = NULL;
    size_t signature_size = version == XX_RAR_VERSION_4 ? 7U : 8U;
    int64_t p = start + (int64_t)signature_size, physical_data;
    bool ok = false;
    if (!xx_rar_read_exact_at(source, start, magic, signature_size) ||
        xx_rt_memcmp(magic, version == XX_RAR_VERSION_4 ? g_xx_rar4_signature : g_xx_rar5_signature, signature_size) ||
        !xx_rar_split_append(view, source, start, (int64_t)signature_size) ||
        !xx_rar_parse_block(source, end, version, p, &block)) goto done;
    if (version == XX_RAR_VERSION_4) {
        uint16_t flags;
        if (block.type != XX_RAR4_HEADER_MAIN || block.header_size < 13 ||
            !(block.flags & XX_RAR4_MAIN_FLAG_PASSWORD) || block.data_size) goto done;
        header = (uint8_t *)xx_mem_alloc((size_t)block.header_size);
        if (!header || !xx_rar_read_exact_at(source, p, header, (size_t)block.header_size)) goto done;
        flags = (uint16_t)(block.flags & ~XX_RAR4_MAIN_FLAG_PASSWORD);
        xx_rar_split_u16(header + 3, flags);
        xx_rar_split_u16(header, (uint16_t)xx_crc32_calc(0, header + 2, (size_t)block.header_size - 2));
        if (!xx_rar_split_header(view, view->count, header, (size_t)block.header_size)) goto done;
        header = NULL;
    } else if (!xx_rar_header_key5(source, &block, password, password_size, key, pd)) goto done;
    p = block.next_offset;
    while (p < end) {
        if (!xx_rar_read_encrypted_header(source, p, end, version, password, password_size,
                key, &header, &block, &physical_data, pd)) goto done;
        if (!xx_rar_split_header(view, view->count, header, (size_t)block.header_size)) goto done;
        header = NULL;
        if (block.data_size && !xx_rar_split_append(view, source, physical_data, block.data_size)) goto done;
        p = physical_data + block.data_size;
        if (block.type == (version == XX_RAR_VERSION_4 ? XX_RAR4_HEADER_END : XX_RAR5_HEADER_END)) {
            if (!xx_rar_split_zero_tail(source, p, end, pd)) goto done;
            ok = xx_rar_split_finish(view); break;
        }
    }
done:
    if(header) xx_rar_secure_clear(header,(size_t)block.header_size);
    xx_mem_free(header);xx_rar_secure_clear(key,sizeof(key));
    return ok;
}

static bool xx_rar_decrypt_headers(Abstractformat *self, xx_pd_struct *pd,
                                   xx_rar_split_view **result) {
    xx_io_device *source = self->device;
    xx_rar_version_t version;
    xx_rar_block first;
    xx_rar_split_view *view = NULL;
    uint8_t *password = NULL, *header = NULL, key[32] = {0};
    size_t password_size = 0, count, i;
    int64_t signature, total = xx_io_total_size(source), data_offset;
    uint64_t main_flags = 0, number;
    bool encrypted, native, ok = false;
    *result = NULL;
    if (!xx_rar_find_signature(self, &version, &signature) ||
        !xx_rar_parse_block(source, total, version, signature +
                            (version == XX_RAR_VERSION_4 ? 7 : 8), &first)) return false;
    encrypted = version == XX_RAR_VERSION_4 ?
        first.type == XX_RAR4_HEADER_MAIN && (first.flags & XX_RAR4_MAIN_FLAG_PASSWORD) != 0 :
        first.type == XX_RAR5_HEADER_CRYPT;
    if (!encrypted || !xx_format_find_extra_parameter(self, XX_META_ID_OPT_PASSWORD)) return true;
    if (!xx_rar_password_utf8(self, NULL, &password, &password_size)) goto done;
    if (version == XX_RAR_VERSION_4) native = (first.flags & 1U) != 0;
    else {
        xx_io_device *memory;
        if (!xx_rar_header_key5(source, &first, password, password_size, key, pd) ||
            !xx_rar_read_encrypted_header(source, first.next_offset, total, version,
                password, password_size, key, &header, &first, &data_offset, pd)) goto done;
        memory = xx_io_mem_open_ro(header, (size_t)first.header_size);
        if (!memory) goto done;
        ok = xx_rar_split_main(memory, &first, &main_flags, &number);
        xx_io_close(memory);xx_rar_secure_clear(header,(size_t)first.header_size);xx_mem_free(header);header=NULL;
        if (!ok) goto done;
        native = (main_flags & 1U) != 0; ok = false;
    }
    count = native ? xx_io_multivolume_count(source) : 0;
    if (!count) count = 1;
    if (count > XX_RAR_SPLIT_MAX_RANGES) goto done;
    view = (xx_rar_split_view *)xx_mem_calloc(1, sizeof(*view));
    if (!view) goto done;
    view->original = source; view->original_base = self->base_address;
    view->headers_encrypted = true;
    view->children = (xx_rar_split_view **)xx_mem_calloc(count, sizeof(*view->children));
    if (!view->children) goto done;
    view->child_count = count;
    for (i = 0; i < count; ++i) {
        xx_io_volume volume;
        int64_t start = signature, end = total;
        if (native && xx_io_multivolume_count(source)) {
            if (!xx_io_multivolume_get_volume(source, i, &volume, &start) ||
                !xx_rar_add_i64(start, volume.size, &end)) goto done;
            if (i == 0) start = signature;
        }
        view->children[i] = (xx_rar_split_view *)xx_mem_calloc(1, sizeof(*view));
        if (!view->children[i] ||
            !xx_rar_header_volume(source, start, end, version, password, password_size, view->children[i], pd) ||
            !xx_rar_split_append(view, view->children[i]->view, 0, xx_io_total_size(view->children[i]->view))) goto done;
    }
    if (!xx_rar_split_finish(view) || xx_pd_is_stopped(pd)) goto done;
    *result = view; view = NULL; ok = true;
done:
    if (password) { xx_mem_zero(password, password_size); xx_mem_free(password); }
    if(header) xx_rar_secure_clear(header,(size_t)first.header_size);
    xx_mem_free(header);xx_rar_secure_clear(key,sizeof(key));xx_rar_split_free(view);
    return ok;
}
