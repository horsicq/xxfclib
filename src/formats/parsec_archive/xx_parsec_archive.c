/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/parsec_archive/xx_parsec_archive.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define PARSEC_MAX_RECORDS 1000000U

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    uint8_t first[4], signature[8], *header = NULL;
    uint32_t header_size, count, index;
    int64_t total = pm_available(format), expected;
    bool valid = false;
    if (total < 20 || !pm_read(format, 0, first, sizeof(first))) return false;
    header_size = xx_data_get_u32(first, 4, 0, false);
    if (header_size < 12 || (header_size - 4U) % 8U != 0U || (uint64_t)header_size > (uint64_t)total) return false;
    count = (header_size - 4U) / 8U;
    if (count == 0U || count > PARSEC_MAX_RECORDS) return false;
    header = (uint8_t *)xx_mem_alloc(header_size);
    if (!header || !pm_read(format, 0, header, header_size)) goto done;
    if (xx_data_get_u32(header + (size_t)count * 4U, 4, 0, false) != 0U) goto done;
    expected = header_size;
    for (index = 0U; index < count; ++index) {
        uint32_t offset = xx_data_get_u32(header + (size_t)index * 4U, 4, 0, false);
        uint32_t size = xx_data_get_u32(header + ((size_t)count + 1U + index) * 4U, 4, 0, false);
        uint64_t next = (uint64_t)offset + (uint64_t)size;
        uint64_t declared_next = index + 1U < count ? xx_data_get_u32(header + ((size_t)index + 1U) * 4U, 4, 0, false) : (uint64_t)total;
        bool rib, sm8;
        char label[40];
        if ((pd && xx_pd_is_stopped(pd)) || (uint64_t)offset != (uint64_t)expected || size < 8U || next != declared_next || next > (uint64_t)total ||
            !pm_read(format, offset, signature, sizeof(signature)))
            goto done;
        rib = xx_rt_memcmp(signature, "RIB\0", 4U) == 0;
        sm8 = xx_rt_memcmp(signature, "SM8\0\0\1", 6U) == 0;
        if (!rib && !sm8) goto done;
        if (rib) {
            uint32_t decoded = xx_data_get_u32(signature + 4U, 4, 0, false);
            uint32_t packed = size - 8U;
            if (decoded > 512U * 1024U * 1024U || packed > decoded || ((packed == 0U) != (decoded == 0U))) goto done;
        }
        if (sm8 && size < 10U) goto done;
        (void)xx_rt_snprintf(label, sizeof(label), "record_%06u.%s", (unsigned)(index + 1U), rib ? "rib" : "sm8");
        if (!pm_add(format, stream, label, offset, size)) goto done;
        expected = (int64_t)next;
    }
    if (expected != total) goto done;
    stream->size = total;
    valid = true;
done:
    if (header) xx_mem_free(header);
    return valid;
}

void xx_parsec_archive_init(xx_parsec_archive *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XX_FILE_TYPE_PARSEC_ARCHIVE, "dat");
    xx_format_set_mime_type(&reader->format, "application/x-parsec-resource-archive");
}

xx_parsec_archive *xx_parsec_archive_create(xx_io_device *device, int64_t base_address)
{
    xx_parsec_archive *reader = (xx_parsec_archive *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_parsec_archive_init(reader, device, base_address);
    return reader;
}

void xx_parsec_archive_destroy(xx_parsec_archive *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_parsec_archive_free(xx_parsec_archive *reader)
{
    if (reader) {
        xx_parsec_archive_destroy(reader);
        xx_mem_free(reader);
    }
}
