/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT. Complete-member recovery and strict failures. */
#include "xxfclib/formats/archive/bigaf/xx_bigaf.h"
#include "xxfclib/formats/archive/chz/xx_chz.h"
#include "xxfclib/formats/archive/sfx_chz/xx_sfx_chz.h"
#include "xxfclib/formats/archive/spis/xx_spis.h"
#include "xxfclib/formats/archive/sfx_swag/xx_sfx_swag.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "tail recovery line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, (uint16_t)v); le16(p + 2, (uint16_t)(v >> 16)); }
static void decimal(uint8_t *p, size_t n, unsigned v)
{
    char text[32];
    size_t length;
    snprintf(text, sizeof(text), "%u", v);
    length = strlen(text);
    CHECK(length <= n);
    memset(p, ' ', n);
    memcpy(p, text, length);
}
static void check_member(Abstractformat *f, bool comment)
{
    xx_archive_record_state *state;
    const xx_archive_record *record;
    CHECK(xx_format_handle_base_info(f, NULL));
    CHECK(f->number_of_archive_records == 1);
    state = xx_format_create_archive_records_reading(f, NULL, NULL);
    CHECK(state);
    record = xx_format_get_current_archive_record(f, state);
    CHECK(record);
    if (comment) CHECK(xx_archive_record_get_meta_str(record, XX_META_ID_COMMENT));
    CHECK(xx_format_unpack_current_archive_record(f, state, NULL));
    CHECK(!xx_format_archive_record_move_to_next(f, state, NULL));
    xx_format_free_archive_records_reading(f, state);
}
static void dos(uint8_t *p)
{
    memset(p, 0, 64);
    memcpy(p, "MZ", 2);
    le16(p + 2, 64); le16(p + 4, 1); le16(p + 8, 4); le16(p + 24, 28);
}
static void bigaf_tests(void)
{
    uint8_t data[300];
    xx_io_device *device;
    xx_bigaf archive;
    memset(data, ' ', sizeof(data)); memcpy(data, "<bigaf>\n", 8);
    decimal(data + 68, 20, 128); decimal(data + 88, 20, 400);
    decimal(data + 128, 20, 3); decimal(data + 148, 20, 400); decimal(data + 236, 4, 1);
    data[240] = 'a'; data[241] = 0; memcpy(data + 242, "`\nabc", 5);
    device = xx_io_mem_open_ro(data, sizeof(data)); CHECK(device);
    xx_bigaf_init(&archive, device, 0); check_member(&archive.format, true);
    CHECK(!archive.is_complete); xx_bigaf_destroy(&archive); xx_io_close(device);
    data[128] = 'x';
    device = xx_io_mem_open_ro(data, sizeof(data)); xx_bigaf_init(&archive, device, 0);
    CHECK(!xx_format_handle_base_info(&archive.format, NULL));
    xx_bigaf_destroy(&archive); xx_io_close(device);
}
static size_t chz_fixture(uint8_t *data)
{
    memset(data, 0, 64);
    memcpy(data, "SChF", 4); le32(data + 4, 27); le32(data + 8, 2); le16(data + 22, 1);
    memcpy(data + 24, "aok", 3);
    memcpy(data + 27, "SChF", 4); le32(data + 31, 100); le32(data + 35, 75); le16(data + 49, 1);
    data[51] = 'b';
    return 52;
}
static void chz_tests(void)
{
    uint8_t data[128]; size_t length;
    xx_io_device *device; xx_chz raw; xx_sfx_chz outer;
    length = chz_fixture(data);
    device = xx_io_mem_open_ro(data, length); xx_chz_init(&raw, device, 0);
    check_member(&raw.format, true); xx_chz_destroy(&raw); xx_io_close(device);
    length = chz_fixture(data + 64); dos(data);
    device = xx_io_mem_open_ro(data, 64 + length); xx_sfx_chz_init(&outer, device, 0);
    check_member(&outer.format, true); xx_sfx_chz_destroy(&outer); xx_io_close(device);
    data[64 + 22] = 0;
    device = xx_io_mem_open_ro(data, 64 + length); xx_sfx_chz_init(&outer, device, 0);
    CHECK(!xx_format_handle_base_info(&outer.format, NULL));
    xx_sfx_chz_destroy(&outer); xx_io_close(device);
}
static void spis_tests(void)
{
    uint8_t data[81]; xx_io_device *device; xx_spis archive;
    xx_archive_record_state *state;
    memset(data, 0, sizeof(data)); memcpy(data, "SPIS\x1a" "RLE", 8); le32(data + 8, 3); data[12] = 1;
    le16(data + 21, 1); le32(data + 29, 3); le32(data + 33, 3); data[37] = 1; le32(data + 38, 195);
    memcpy(data + 46, "aA\x94\x03", 4);
    /* Complete first segment, followed by a declared ten-byte short member. */
    memcpy(data + 50, "SPIS\x1a" "NON", 8); le32(data + 58, 10); data[62] = 1;
    le16(data + 71, 1); /* This header itself is cut off. */
    device = xx_io_mem_open_ro(data, sizeof(data)); xx_spis_init(&archive, device, 0);
    check_member(&archive.format, true); xx_spis_destroy(&archive); xx_io_close(device);
    data[38] ^= 1;
    device = xx_io_mem_open_ro(data, sizeof(data)); xx_spis_init(&archive, device, 0);
    CHECK(xx_format_handle_base_info(&archive.format, NULL));
    state = xx_format_create_archive_records_reading(&archive.format, NULL, NULL); CHECK(state);
    CHECK(!xx_format_unpack_current_archive_record(&archive.format, state, NULL));
    xx_format_free_archive_records_reading(&archive.format, state); xx_spis_destroy(&archive); xx_io_close(device);
    data[12] = 7;
    device = xx_io_mem_open_ro(data, sizeof(data)); xx_spis_init(&archive, device, 0);
    CHECK(!xx_format_handle_base_info(&archive.format, NULL)); xx_spis_destroy(&archive); xx_io_close(device);
}
static void swag_tests(void)
{
    uint8_t data[1024], *h = data + 64, footer[129];
    const uint8_t payload[] = {'a', 'b', 'c'};
    unsigned sum = 0, i; size_t end; xx_io_device *device; xx_sfx_swag archive;
    memset(data, 0, sizeof(data)); dos(data);
    h[0] = 192; memcpy(h + 2, "-sw1-", 5); le32(h + 7, 3);
    le32(h + 21, ~xx_crc32_calc(0, payload, 3)); h[186] = 5; memcpy(h + 187, "a.txt", 5);
    le16(h + 192, xx_crc16_arc_calc(0, payload, 3));
    for (i = 2; i < 194; ++i) sum += h[i];
    h[1] = (uint8_t)sum; memcpy(h + 194, payload, 3); end = 64 + 197;
    memset(footer, 0, sizeof(footer)); footer[0] = 1; footer[1] = 'c'; footer[61] = 1; footer[62] = 't'; le16(footer + 127, 1);
    memcpy(data + end, footer, 129); memcpy(data + end + 129, footer, 129); end += 258;
    device = xx_io_mem_open_ro(data, end); xx_sfx_swag_init(&archive, device, 0);
    check_member(&archive.format, true); xx_sfx_swag_destroy(&archive); xx_io_close(device);
    h[194] ^= 1;
    device = xx_io_mem_open_ro(data, end); xx_sfx_swag_init(&archive, device, 0);
    CHECK(xx_format_handle_base_info(&archive.format, NULL));
    {
        xx_archive_record_state *state = xx_format_create_archive_records_reading(&archive.format, NULL, NULL);
        if (state) {
            CHECK(!xx_format_unpack_current_archive_record(&archive.format, state, NULL));
            xx_format_free_archive_records_reading(&archive.format, state);
        }
    }
    xx_sfx_swag_destroy(&archive); xx_io_close(device);
    h[194] ^= 1;
    data[end - 258 + 1] ^= 1;
    device = xx_io_mem_open_ro(data, end); xx_sfx_swag_init(&archive, device, 0);
    CHECK(!xx_format_handle_base_info(&archive.format, NULL)); xx_sfx_swag_destroy(&archive); xx_io_close(device);
    data[end - 258 + 1] ^= 1; h[1] ^= 1;
    device = xx_io_mem_open_ro(data, end); xx_sfx_swag_init(&archive, device, 0);
    CHECK(!xx_format_handle_base_info(&archive.format, NULL)); xx_sfx_swag_destroy(&archive); xx_io_close(device);
}
int main(void)
{
    bigaf_tests(); chz_tests(); spis_tests(); swag_tests();
    puts("complete-member tail recovery tests passed");
    return 0;
}
