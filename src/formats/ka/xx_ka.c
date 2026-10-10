/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KA Archive has a 22-byte lead and 21-byte table rows: 13-byte NUL-padded
 * leaf name, little-endian absolute data offset, little-endian data size.
 * Table order need not be payload order.  The selected CMD.ARC corpus has
 * one truncated entry (EXIT.CMD), which is omitted while the five bounded
 * entries remain extractable. Their bytes agree exactly with the reference reader output.
 */
#include "xxfclib/formats/ka/xx_ka.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define KA_HEADER_SIZE 22U
#define KA_ENTRY_SIZE 21U
#define KA_NAME_SIZE 13U

#ifdef KA
#define KA_FILE_TYPE XX_FILE_TYPE_KA
#else
#define KA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool ka_name(const uint8_t *field, char *name)
{
    size_t length = 0U, i;
    while (length < KA_NAME_SIZE && field[length] != 0U) ++length;
    if (length == 0U || length == KA_NAME_SIZE) return false;
    for (i = 0U; i < length; ++i) {
        uint8_t c = field[i];
        if (c <= 0x20U || c >= 0x7fU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
        name[i] = (char)c;
    }
    name[length] = 0;
    return !(name[0] == '.' && (length == 1U || (length == 2U && name[1] == '.')));
}

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    uint8_t header[KA_HEADER_SIZE], row[KA_ENTRY_SIZE];
    int64_t span = pm_available(format), table_end, largest;
    uint16_t count;
    size_t i;
    if (span < (int64_t)KA_HEADER_SIZE || !pm_read(format, 0, header, sizeof(header)) || xx_rt_memcmp(header, "KA Archive\0", 11U) != 0) return false;
    for (i = 11U; i < 20U; ++i)
        if (header[i] != 0U) return false;
    count = xx_data_get_u16(header + 20U, 2, 0, false);
    table_end = (int64_t)KA_HEADER_SIZE + (int64_t)count * KA_ENTRY_SIZE;
    if (count == 0U || table_end > span) return false;
    largest = table_end;
    for (i = 0U; i < count; ++i) {
        char name[KA_NAME_SIZE + 1U];
        int64_t offset, size;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(format, (int64_t)KA_HEADER_SIZE + (int64_t)i * KA_ENTRY_SIZE, row, sizeof(row)) || !ka_name(row, name)) return false;
        offset = (int64_t)xx_data_get_u32(row + KA_NAME_SIZE, 4, 0, false);
        size = (int64_t)xx_data_get_u32(row + KA_NAME_SIZE + 4U, 4, 0, false);
        if (offset < table_end || offset > span || size > span - offset) {
            /* Damaged entry: its declared bytes do not exist.  Never emit
             * the truncated tail as a valid member. */
            continue;
        }
        if (!pm_add(format, stream, name, offset, size)) return false;
        xx_rt_snprintf(stream->items[stream->count - 1U].name, sizeof(stream->items[stream->count - 1U].name), "%s", name);
        if (offset + size > largest) largest = offset + size;
    }
    if (stream->count == 0U) return false;
    stream->size = largest;
    return true;
}

void xx_ka_init(xx_ka *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, KA_FILE_TYPE, "arc");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format, "application/x-ka-archive");
}
xx_ka *xx_ka_create(xx_io_device *device, int64_t base_address)
{
    xx_ka *archive = (xx_ka *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ka_init(archive, device, base_address);
    return archive;
}
void xx_ka_destroy(xx_ka *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_ka_free(xx_ka *archive)
{
    if (!archive) return;
    xx_ka_destroy(archive);
    xx_mem_free(archive);
}
bool xx_ka_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_valid(format, pd);
}
bool xx_ka_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    return pm_handle(format, pd);
}
