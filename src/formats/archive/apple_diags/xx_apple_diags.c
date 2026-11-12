/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Apple Hardware Test compressed localization/script envelope.
 *
 * 0x00: |_|_ magic; 0x04: BE32 decoded size; 0x08: BE32 plaintext Adler-32.
 * 0x0c: Okumura LZSS, 4096-byte window, initial cursor 4078, LSB-first
 * flags (1 literal), 12-bit absolute match position, 4-bit length minus 3.
 * XOR decoded bytes with 0x80 AFTER updating the compressed-byte dictionary.
 * The exact size, full payload consumption and checksum must all agree.
 * Format facts established from the ARC9 Apple_Diags_1 corpus; independently
 * implemented. The header stores no filename, so expose one script.txt.
 */
#include "xxfclib/formats/archive/apple_diags/xx_apple_diags.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/adler32/xx_adler32.h"
#include "xxfclib/data/xx_data.h"

#define APPLE_DIAGS_HEADER 12U
#define APPLE_DIAGS_MEMORY_CAP (256U * 1024U * 1024U)

static bool apple_diags_decode(const uint8_t *packed, size_t packed_size,
                               uint8_t *plain, size_t plain_size, xx_pd_struct *pd)
{
    uint8_t window[4096];
    size_t at = 0, written = 0, cursor = 4078;
    /* The standard Okumura initial dictionary. The checksum authenticates
     * this interpretation even when an input references its initial fill. */
    xx_rt_memset(window, 0x20, sizeof(window));
    while (written < plain_size) {
        uint8_t flags;
        unsigned bit;
        if ((pd && xx_pd_is_stopped(pd)) || at >= packed_size) return false;
        flags = packed[at++];
        for (bit = 0; bit < 8 && written < plain_size; ++bit) {
            size_t position, length, i;
            if (flags & (1U << bit)) {
                if (at >= packed_size) return false;
                position = cursor;
                window[cursor] = packed[at++];
                length = 1;
            } else {
                uint8_t low, high;
                if (packed_size - at < 2) return false;
                low = packed[at++]; high = packed[at++];
                position = low | ((size_t)(high & 0xf0U) << 4);
                length = (high & 15U) + 3U;
                if (length > plain_size - written) return false;
            }
            for (i = 0; i < length; ++i) {
                uint8_t value = window[position];
                position = (position + 1U) & 4095U;
                plain[written++] = value ^ 0x80U;
                window[cursor] = value;
                cursor = (cursor + 1U) & 4095U;
            }
        }
    }
    return at == packed_size && !(pd && xx_pd_is_stopped(pd));
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t header[APPLE_DIAGS_HEADER], *packed = NULL, *plain = NULL;
    uint32_t decoded_size, checksum;
    uint64_t budget = APPLE_DIAGS_MEMORY_CAP, overhead = 8U * sizeof(pm_member) + sizeof(pm_stream) + 11U;
    int64_t available = pm_available(f);
    size_t packed_size;
    const xx_var *limit;
    bool ok = false;
    if ((pd && xx_pd_is_stopped(pd)) || available < APPLE_DIAGS_HEADER ||
        !pm_read(f, 0, header, sizeof(header)) || xx_rt_memcmp(header, "|_|_", 4)) return false;
    decoded_size = xx_data_get_u32(header + 4, 4, 0, true);
    checksum = xx_data_get_u32(header + 8, 4, 0, true);
    limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (limit && xx_var_get_u64(limit) < budget) budget = xx_var_get_u64(limit);
    /* Check integer ranges and the combined allocation budget before casts
     * or allocation. pm_add reserves eight member entries initially. */
    if ((uint64_t)available - APPLE_DIAGS_HEADER > budget || overhead > budget) return false;
    packed_size = (size_t)(available - APPLE_DIAGS_HEADER);
    if (packed_size > budget - overhead || decoded_size > budget - overhead - packed_size) return false;
    limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit && decoded_size > xx_var_get_u64(limit)) return false;
    packed = (uint8_t *)xx_mem_alloc(packed_size ? packed_size : 1U);
    plain = (uint8_t *)xx_mem_alloc(decoded_size ? decoded_size : 1U);
    if (!packed || !plain || !pm_read(f, APPLE_DIAGS_HEADER, packed, packed_size) ||
        !apple_diags_decode(packed, packed_size, plain, decoded_size, pd) ||
        xx_adler32(plain, decoded_size) != checksum || (pd && xx_pd_is_stopped(pd))) goto done;
    if (!pm_add(f, s, "script.txt", APPLE_DIAGS_HEADER, (int64_t)packed_size)) goto done;
    s->items[0].memory = plain;
    plain = NULL;
    s->items[0].size = decoded_size;
    s->items[0].compression_method = 1U /* the LOC envelope has one LZSS method */;
    s->items[0].display_name = xx_str_dup("script.txt");
    s->size = available;
    ok = s->items[0].display_name != NULL;
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}

static const xx_archive_record *apple_diags_current(Abstractformat *f, xx_archive_record_state *state)
{
    const xx_archive_record *record = pm_current(f, state);
    if (record) state->current_record.header_size = APPLE_DIAGS_HEADER;
    return record;
}

Abstractformat *xx_apple_diags_create(xx_io_device *device, int64_t base_address)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) {
        pm_init(format, device, base_address, XX_FILE_TYPE_APPLE_DIAGS, "loc");
        format->get_current_archive_record = apple_diags_current;
    }
    return format;
}

void xx_apple_diags_free(Abstractformat *format)
{
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *apple_diags_open(xx_io_device *device) { return xx_apple_diags_create(device, 0); }
static const xx_file_type_t apple_diags_types[] = {XX_FILE_TYPE_APPLE_DIAGS};
static const uint8_t apple_diags_magic[] = {'|', '_', '|', '_'};
static const xx_format_search_anchor apple_diags_anchors[] = {{apple_diags_magic, 4U, 0U}};
const xx_format_search_desc xx_apple_diags_search_desc = {
    apple_diags_types, 1U, apple_diags_anchors, 1U, apple_diags_open, xx_apple_diags_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(apple_diags, xx_apple_diags_search_desc)
