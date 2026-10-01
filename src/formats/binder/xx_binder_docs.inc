/* Standalone CFB documents embedded in an Office Binder.  Kept in this
 * include so the reader and its checked FAT/mini-FAT helpers share one
 * translation unit without changing the library's source list. */

#define BINDER_DOC_MAX_SIZE (32U * 1024U * 1024U)
#define BINDER_DOC_MAX_COUNT 1024U
#define BINDER_DOC_SECTOR 512U
#define BINDER_DOC_MINI 64U
#define BINDER_DOC_FATSECT UINT32_C(0xFFFFFFFD)

static void binder_doc_put16(uint8_t *at, uint16_t value) {
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8U);
}

static void binder_doc_put32(uint8_t *at, uint32_t value) {
    binder_doc_put16(at, (uint16_t)value);
    binder_doc_put16(at + 2U, (uint16_t)(value >> 16U));
}

static void binder_doc_put64(uint8_t *at, uint64_t value) {
    binder_doc_put32(at, (uint32_t)value);
    binder_doc_put32(at + 4U, (uint32_t)(value >> 32U));
}

static const binder_member *binder_doc_source_member(
    const binder_stream *stream, uint32_t directory_index) {
    size_t index;
    for (index = 0U; index < stream->count; ++index) {
        const binder_member *member = &stream->items[index];
        if (!member->virtual_document &&
            member->directory_index == directory_index)
            return member;
    }
    return NULL;
}

static bool binder_doc_mark(const binder_stream *stream, uint32_t index,
                            uint8_t *selected, unsigned depth) {
    const uint8_t *entry;
    uint8_t type;
    if (index == BINDER_ENDOFCHAIN || index == BINDER_FREESECT) return true;
    if (depth >= BINDER_MAX_DEPTH || (size_t)index >= stream->entry_count ||
        selected[index])
        return false;
    entry = stream->directory + (size_t)index * BINDER_DIR_ENTRY_SIZE;
    type = entry[0x42U];
    if (type != BINDER_TYPE_STORAGE && type != BINDER_TYPE_STREAM)
        return false;
    selected[index] = 1U;
    if (!binder_doc_mark(stream, binder_le32(entry + 0x44U), selected,
                         depth + 1U) ||
        !binder_doc_mark(stream, binder_le32(entry + 0x48U), selected,
                         depth + 1U))
        return false;
    return type != BINDER_TYPE_STORAGE ||
           binder_doc_mark(stream, binder_le32(entry + 0x4CU), selected,
                           depth + 1U);
}

static void binder_doc_set_fat(uint8_t *output, uint32_t fat_start,
                               uint32_t sector, uint32_t value) {
    size_t offset = (size_t)BINDER_DOC_SECTOR +
                    (size_t)fat_start * BINDER_DOC_SECTOR +
                    (size_t)sector * 4U;
    binder_doc_put32(output + offset, value);
}

static bool binder_doc_build(const binder_stream *stream, uint32_t storage,
                             uint8_t **result, size_t *result_size) {
    const uint8_t *source_root;
    uint8_t *selected = NULL, *output = NULL, *directory;
    uint64_t large_sectors = 0U, mini_sectors = 0U;
    uint64_t base_sectors, total_sectors, total_bytes;
    uint32_t dir_sectors, mini_stream_sectors, minifat_sectors, fat_sectors;
    uint32_t regular_at, mini_at, mini_stream_at, minifat_at, fat_at;
    size_t index, dir_bytes;
    static const uint8_t cfb_magic[8] = {
        0xD0U, 0xCFU, 0x11U, 0xE0U, 0xA1U, 0xB1U, 0x1AU, 0xE1U};

    if (!result || !result_size || !stream || !stream->directory ||
        storage == 0U || (size_t)storage >= stream->entry_count ||
        stream->entry_count > BINDER_MAX_DIR_ENTRIES)
        return false;
    *result = NULL;
    *result_size = 0U;
    source_root = stream->directory +
                  (size_t)storage * BINDER_DIR_ENTRY_SIZE;
    if (source_root[0x42U] != BINDER_TYPE_STORAGE) return false;
    selected = (uint8_t *)xx_mem_calloc(stream->entry_count, 1U);
    if (!selected) return false;
    selected[0] = 1U;
    if (!binder_doc_mark(stream, binder_le32(source_root + 0x4CU),
                         selected, 0U))
        goto fail;

    for (index = 1U; index < stream->entry_count; ++index) {
        const binder_member *member;
        const uint8_t *entry;
        uint64_t sectors;
        if (!selected[index]) continue;
        entry = stream->directory + index * BINDER_DIR_ENTRY_SIZE;
        if (entry[0x42U] != BINDER_TYPE_STREAM) continue;
        member = binder_doc_source_member(stream, (uint32_t)index);
        if (!member || member->size > BINDER_DOC_MAX_SIZE) goto fail;
        if (member->size == 0U) continue;
        if (member->size < 4096U) {
            sectors = (member->size + BINDER_DOC_MINI - 1U) /
                      BINDER_DOC_MINI;
            mini_sectors += sectors;
        } else {
            sectors = (member->size + BINDER_DOC_SECTOR - 1U) /
                      BINDER_DOC_SECTOR;
            large_sectors += sectors;
        }
        if (mini_sectors > BINDER_DOC_MAX_SIZE / BINDER_DOC_MINI ||
            large_sectors > BINDER_DOC_MAX_SIZE / BINDER_DOC_SECTOR)
            goto fail;
    }

    dir_bytes = stream->entry_count * BINDER_DIR_ENTRY_SIZE;
    dir_sectors = (uint32_t)((dir_bytes + BINDER_DOC_SECTOR - 1U) /
                             BINDER_DOC_SECTOR);
    mini_stream_sectors = (uint32_t)((mini_sectors * BINDER_DOC_MINI +
                                      BINDER_DOC_SECTOR - 1U) /
                                     BINDER_DOC_SECTOR);
    minifat_sectors = (uint32_t)((mini_sectors * 4U +
                                  BINDER_DOC_SECTOR - 1U) /
                                 BINDER_DOC_SECTOR);
    base_sectors = (uint64_t)dir_sectors + large_sectors +
                   mini_stream_sectors + minifat_sectors;
    fat_sectors = 1U;
    while ((uint64_t)fat_sectors * (BINDER_DOC_SECTOR / 4U) <
           base_sectors + fat_sectors) {
        if (++fat_sectors > 109U) goto fail;
    }
    total_sectors = base_sectors + fat_sectors;
    total_bytes = (total_sectors + 1U) * BINDER_DOC_SECTOR;
    if (fat_sectors > 109U || total_bytes > BINDER_DOC_MAX_SIZE ||
        total_sectors > UINT32_MAX)
        goto fail;
    output = (uint8_t *)xx_mem_calloc((size_t)total_bytes, 1U);
    if (!output) goto fail;
    directory = output + BINDER_DOC_SECTOR;
    xx_mem_copy(directory, stream->directory, dir_bytes);
    for (index = 1U; index < stream->entry_count; ++index) {
        if (!selected[index])
            xx_mem_zero(directory + index * BINDER_DIR_ENTRY_SIZE,
                        BINDER_DIR_ENTRY_SIZE);
    }
    xx_mem_copy(directory, source_root, BINDER_DIR_ENTRY_SIZE);
    xx_mem_zero(directory, BINDER_DIR_NAME_SIZE);
    {
        static const char root_name[] = "Root Entry";
        size_t at;
        for (at = 0U; at < sizeof(root_name) - 1U; ++at)
            directory[at * 2U] = (uint8_t)root_name[at];
        binder_doc_put16(directory + 0x40U,
                         (uint16_t)(sizeof(root_name) * 2U));
    }
    directory[0x42U] = BINDER_TYPE_ROOT;
    directory[0x43U] = 1U;
    binder_doc_put32(directory + 0x44U, BINDER_FREESECT);
    binder_doc_put32(directory + 0x48U, BINDER_FREESECT);

    regular_at = dir_sectors;
    mini_stream_at = regular_at + (uint32_t)large_sectors;
    minifat_at = mini_stream_at + mini_stream_sectors;
    fat_at = minifat_at + minifat_sectors;
    mini_at = 0U;
    xx_rt_memset(output + BINDER_DOC_SECTOR + (size_t)fat_at *
                 BINDER_DOC_SECTOR, 0xFF,
                 (size_t)fat_sectors * BINDER_DOC_SECTOR);
    if (minifat_sectors != 0U)
        xx_rt_memset(output + BINDER_DOC_SECTOR + (size_t)minifat_at *
                     BINDER_DOC_SECTOR, 0xFF,
                     (size_t)minifat_sectors * BINDER_DOC_SECTOR);

    for (index = 0U; index < dir_sectors; ++index)
        binder_doc_set_fat(output, fat_at, (uint32_t)index,
                           index + 1U < dir_sectors
                               ? (uint32_t)index + 1U : BINDER_ENDOFCHAIN);
    for (index = 1U; index < stream->entry_count; ++index) {
        const binder_member *member;
        uint8_t *entry;
        uint32_t count, first;
        size_t unit;
        if (!selected[index]) continue;
        entry = directory + index * BINDER_DIR_ENTRY_SIZE;
        if (entry[0x42U] != BINDER_TYPE_STREAM) continue;
        member = binder_doc_source_member(stream, (uint32_t)index);
        if (!member) goto fail;
        if (member->size == 0U) {
            binder_doc_put32(entry + 0x74U, BINDER_ENDOFCHAIN);
            binder_doc_put64(entry + 0x78U, 0U);
            continue;
        }
        unit = member->size < 4096U ? BINDER_DOC_MINI : BINDER_DOC_SECTOR;
        count = (uint32_t)((member->size + unit - 1U) / unit);
        if (unit == BINDER_DOC_MINI) {
            uint8_t *destination = output + BINDER_DOC_SECTOR +
                (size_t)mini_stream_at * BINDER_DOC_SECTOR +
                (size_t)mini_at * BINDER_DOC_MINI;
            first = mini_at;
            if (!binder_read_mini_chain(stream, member->start_sector,
                                        member->size, destination))
                goto fail;
            for (uint32_t part = 0U; part < count; ++part)
                binder_doc_put32(output + BINDER_DOC_SECTOR +
                                  (size_t)minifat_at * BINDER_DOC_SECTOR +
                                  (size_t)(mini_at + part) * 4U,
                                  part + 1U < count
                                      ? mini_at + part + 1U
                                      : BINDER_ENDOFCHAIN);
            mini_at += count;
        } else {
            uint8_t *destination = output + BINDER_DOC_SECTOR +
                (size_t)regular_at * BINDER_DOC_SECTOR;
            first = regular_at;
            if (!binder_read_fat_chain(stream, member->start_sector,
                                       member->size, destination))
                goto fail;
            for (uint32_t part = 0U; part < count; ++part)
                binder_doc_set_fat(output, fat_at, regular_at + part,
                                   part + 1U < count
                                       ? regular_at + part + 1U
                                       : BINDER_ENDOFCHAIN);
            regular_at += count;
        }
        binder_doc_put32(entry + 0x74U, first);
        binder_doc_put64(entry + 0x78U, member->size);
    }
    if (regular_at != mini_stream_at || mini_at != mini_sectors) goto fail;
    for (index = 0U; index < mini_stream_sectors; ++index)
        binder_doc_set_fat(output, fat_at, mini_stream_at + (uint32_t)index,
                           index + 1U < mini_stream_sectors
                               ? mini_stream_at + (uint32_t)index + 1U
                               : BINDER_ENDOFCHAIN);
    for (index = 0U; index < minifat_sectors; ++index)
        binder_doc_set_fat(output, fat_at, minifat_at + (uint32_t)index,
                           index + 1U < minifat_sectors
                               ? minifat_at + (uint32_t)index + 1U
                               : BINDER_ENDOFCHAIN);
    for (index = 0U; index < fat_sectors; ++index)
        binder_doc_set_fat(output, fat_at, fat_at + (uint32_t)index,
                           BINDER_DOC_FATSECT);
    binder_doc_put32(directory + 0x74U,
                     mini_stream_sectors ? mini_stream_at : BINDER_ENDOFCHAIN);
    binder_doc_put64(directory + 0x78U, mini_sectors * BINDER_DOC_MINI);

    xx_mem_copy(output, cfb_magic, sizeof(cfb_magic));
    binder_doc_put16(output + 0x18U, 0x003EU);
    binder_doc_put16(output + 0x1AU, 3U);
    binder_doc_put16(output + 0x1CU, 0xFFFEU);
    binder_doc_put16(output + 0x1EU, 9U);
    binder_doc_put16(output + 0x20U, 6U);
    binder_doc_put32(output + 0x2CU, fat_sectors);
    binder_doc_put32(output + 0x30U, 0U);
    binder_doc_put32(output + 0x38U, 4096U);
    binder_doc_put32(output + 0x3CU,
                     minifat_sectors ? minifat_at : BINDER_ENDOFCHAIN);
    binder_doc_put32(output + 0x40U, minifat_sectors);
    binder_doc_put32(output + 0x44U, BINDER_ENDOFCHAIN);
    binder_doc_put32(output + 0x48U, 0U);
    xx_rt_memset(output + 0x4CU, 0xFF, 109U * 4U);
    for (index = 0U; index < fat_sectors; ++index)
        binder_doc_put32(output + 0x4CU + index * 4U,
                         fat_at + (uint32_t)index);

    xx_mem_free(selected);
    *result = output;
    *result_size = (size_t)total_bytes;
    return true;
fail:
    if (selected) xx_mem_free(selected);
    if (output) xx_mem_free(output);
    return false;
}

static uint32_t binder_doc_storage_index(const binder_stream *stream,
                                          uint32_t storage_id) {
    char wanted[24];
    size_t index;
    int chars = snprintf(wanted, sizeof(wanted), "%u", storage_id);
    if (chars <= 0 || (size_t)chars >= sizeof(wanted)) return BINDER_FREESECT;
    for (index = 1U; index < stream->entry_count; ++index) {
        const uint8_t *entry = stream->directory +
                               index * BINDER_DIR_ENTRY_SIZE;
        uint16_t name_length;
        char name[BINDER_DIR_NAME_SIZE * 2U + 4U];
        size_t bytes;
        if (entry[0x42U] != BINDER_TYPE_STORAGE) continue;
        name_length = binder_le16(entry + 0x40U);
        if (name_length < 2U || name_length > BINDER_DIR_NAME_SIZE ||
            (name_length & 1U) != 0U)
            continue;
        bytes = binder_name_to_utf8(entry, name_length, name,
                                    sizeof(name) - 1U);
        if (bytes != (size_t)chars) continue;
        name[bytes] = 0;
        if (xx_rt_memcmp(name, wanted, bytes) == 0) return (uint32_t)index;
    }
    return BINDER_FREESECT;
}

static const char *binder_doc_extension(const binder_stream *stream,
                                         uint32_t storage_id,
                                         uint32_t storage_index) {
    const uint8_t *entry = stream->directory +
                           (size_t)storage_index * BINDER_DIR_ENTRY_SIZE;
    char prefix[32];
    size_t index, prefix_size;
    bool word = false, excel = false, powerpoint = false;
    bool binary_document = false;
    int length = snprintf(prefix, sizeof(prefix), "%u/", storage_id);
    if (length <= 0 || (size_t)length >= sizeof(prefix)) return "";
    prefix_size = (size_t)length;
    for (index = 0U; index < stream->count; ++index) {
        const binder_member *member = &stream->items[index];
        const char *leaf;
        if (member->virtual_document || !member->name ||
            xx_rt_strlen(member->name) < prefix_size ||
            xx_rt_memcmp(member->name, prefix, prefix_size) != 0)
            continue;
        leaf = member->name + prefix_size;
        if (xx_rt_strcmp(leaf, "WordDocument") == 0) word = true;
        else if (xx_rt_strcmp(leaf, "Workbook") == 0 ||
                 xx_rt_strcmp(leaf, "Book") == 0)
            excel = true;
        else if (xx_rt_strcmp(leaf, "PowerPoint Document") == 0)
            powerpoint = true;
        else if (xx_rt_strcmp(leaf, "Document Data") == 0)
            binary_document = true;
    }
    if (word) return ".doc";
    if (excel) return ".xls";
    if (powerpoint) {
        static const uint8_t old_powerpoint_clsid[16] = {
            0x10U, 0x8DU, 0x81U, 0x64U, 0x9BU, 0x4FU, 0xCFU, 0x11U,
            0x86U, 0xEAU, 0x00U, 0xAAU, 0x00U, 0xB9U, 0x29U, 0xE8U};
        return xx_rt_memcmp(entry + 0x50U, old_powerpoint_clsid,
                            sizeof(old_powerpoint_clsid)) == 0
                   ? ".bin" : ".ppt";
    }
    if (binary_document) return ".bin";
    return "";
}

static bool binder_add_virtual_documents(binder_stream *stream) {
    const binder_member *binder = NULL;
    uint8_t *blob = NULL;
    size_t index, count, cursor, blob_size;
    if (!stream || !stream->directory) return false;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name &&
            xx_rt_strcmp(stream->items[index].name, "Binder") == 0) {
            binder = &stream->items[index];
            break;
        }
    if (!binder || binder->size < 100U || binder->size > BINDER_DOC_MAX_SIZE)
        return true;
    blob_size = (size_t)binder->size;
    blob = (uint8_t *)xx_mem_alloc(blob_size);
    if (!blob) return false;
    if (binder->mini
            ? !binder_read_mini_chain(stream, binder->start_sector,
                                      binder->size, blob)
            : !binder_read_fat_chain(stream, binder->start_sector,
                                     binder->size, blob))
        goto done;
    count = binder_le32(blob + 36U);
    cursor = binder_le32(blob);
    if (count == 0U || count > BINDER_DOC_MAX_COUNT ||
        cursor < 100U || cursor > blob_size)
        goto done;
    for (index = 0U; index < count; ++index) {
        uint32_t entry_size, title_size, storage_id, storage_index;
        const char *extension;
        char title[1024], path[1100];
        size_t title_bytes, extension_bytes;
        binder_member member;
        uint8_t *document = NULL;
        size_t document_size = 0U;
        if (blob_size - cursor < 56U) break;
        entry_size = binder_le32(blob + cursor);
        storage_id = binder_le32(blob + cursor + 24U);
        title_size = binder_le32(blob + cursor + 52U);
        if (entry_size < 56U || entry_size > blob_size - cursor ||
            title_size < 2U || title_size > 512U ||
            (title_size & 1U) != 0U || title_size > entry_size - 56U)
            break;
        title_bytes = binder_name_to_utf8(blob + cursor + 56U,
                                          title_size, title,
                                          sizeof(title) - 1U);
        cursor += entry_size;
        if (title_bytes == 0U) continue;
        title[title_bytes] = 0;
        storage_index = binder_doc_storage_index(stream, storage_id);
        if (storage_index == BINDER_FREESECT) continue;
        extension = binder_doc_extension(stream, storage_id, storage_index);
        extension_bytes = xx_rt_strlen(extension);
        if (title_bytes + extension_bytes >= sizeof(path)) continue;
        xx_mem_copy(path, title, title_bytes);
        xx_mem_copy(path + title_bytes, extension, extension_bytes + 1U);
        if (!binder_safe_output_name(path) ||
            !binder_doc_build(stream, storage_index, &document,
                              &document_size))
            continue;
        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_create(path);
        if (!member.name) {
            xx_mem_free(document);
            continue;
        }
        member.header_offset = -1;
        member.data_offset = -1;
        member.size = document_size;
        member.virtual_document = true;
        member.virtual_bytes = document;
        if (!binder_add_member(stream, &member)) {
            xx_str_free(member.name);
            xx_mem_free(document);
            break;
        }
    }
done:
    xx_mem_free(blob);
    return true;
}
