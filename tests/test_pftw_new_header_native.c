/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT
 * Constructed PE + PFTW + stored CAB fixtures. No executable is run. */
#include "xxfclib/formats/installer/sfx_packagefortheweb/xx_sfx_packagefortheweb.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static void u16(uint8_t *p, unsigned n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void u32(uint8_t *p, uint32_t n) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8)); }
static size_t fixture(uint8_t *bytes, bool old, bool section, unsigned section_prefix)
{
    uint8_t *p, *cab;
    size_t start = section ? 512 + section_prefix : 1024;
    unsigned i;
    memset(bytes, 0, 2048);
    memcpy(bytes, "MZ", 2); u32(bytes + 60, 128);
    memcpy(bytes + 128, "PE\0\0", 4); u16(bytes + 134, 1); u16(bytes + 148, 224);
    u16(bytes + 152, 0x10b); u32(bytes + 212, 512);
    memcpy(bytes + 376, section ? "_cabinet" : ".text", section ? 8 : 5);
    u32(bytes + 392, 512); u32(bytes + 396, 512);
    p = bytes + start;
    u32(p, 16);
    if (old) {
        memcpy(p + 4, "SCG", 3); u32(p + 8, 79);
        for (i = 0; i < 16; ++i) p[4 + i] ^= (uint8_t)(0x61 + i % 26);
    } else memcpy(p + 4, "\xdc\xed\xbd\xa9", 4);
    cab = p + 20;
    memcpy(cab, "MSCF", 4); u32(cab + 8, 79); u32(cab + 16, 44);
    cab[24] = 3; cab[25] = 1; u16(cab + 26, 1); u16(cab + 28, 1); u16(cab + 32, 12345);
    u32(cab + 36, 66); u16(cab + 40, 1);
    u32(cab + 44, 5); memcpy(cab + 60, "t.txt", 6);
    u16(cab + 70, 5); u16(cab + 72, 5); memcpy(cab + 74, "hello", 5);
    return section ? 1024 : start + 99;
}
static int valid(const uint8_t *bytes, size_t size)
{
    xx_io_device *d = xx_io_mem_open_ro(bytes, size);
    xx_sfx_packagefortheweb r;
    bool result;
    if (!d) return 0;
    xx_sfx_packagefortheweb_init(&r, d, 0);
    result = xx_sfx_packagefortheweb_check_is_valid(&r.format, NULL);
    xx_sfx_packagefortheweb_destroy(&r); xx_io_close(d);
    return result;
}
int main(void)
{
    uint8_t bytes[2048], nested[4096]; size_t n;
    xx_io_device *d; xx_sfx_packagefortheweb r; xx_archive_record_state *s;
    const xx_archive_record *record;
    unsigned section, prefix;
    for (section = 0; section < 2; ++section) {
        for (prefix = 0; prefix < (section ? 2U : 1U); ++prefix) {
            n = fixture(bytes, false, section != 0, prefix ? 12 : 0);
            CHECK(valid(bytes, n));
            memset(nested, 0x55, 17); memcpy(nested + 17, bytes, n);
            d = xx_io_mem_open_ro(nested, n + 17); CHECK(d);
            xx_sfx_packagefortheweb_init(&r, d, 17);
            CHECK(xx_sfx_packagefortheweb_handle_base_info(&r.format, NULL) && r.format.number_of_archive_records == 1);
            s = xx_format_create_archive_records_reading(&r.format, NULL, NULL); CHECK(s);
            record = xx_format_get_current_archive_record(&r.format, s);
            CHECK(record && !strcmp(xx_archive_record_get_original_name(record), "pftw1.pkg"));
            CHECK(record->data_offset == (int64_t)(17 + (section ? 512 + (prefix ? 12 : 0) : 1024) + 20));
            CHECK(record->compressed_size == 79 && xx_archive_record_get_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, 0) == 79);
            CHECK(xx_format_unpack_current_archive_record(&r.format, s, NULL));
            CHECK(!xx_format_archive_record_move_to_next(&r.format, s, NULL));
            xx_format_free_archive_records_reading(&r.format, s); xx_sfx_packagefortheweb_destroy(&r); xx_io_close(d);
        }
    }
    n = fixture(bytes, true, false, 0); CHECK(valid(bytes, n));
    /* The original SCG size contract remains authoritative. */
    bytes[1032] ^= 1; CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    bytes[1028] ^= 1; CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    u32(bytes + 1024, 0x100001); CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    u32(bytes + 1044 + 8, 80); CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    u32(bytes + 1044 + 36, 1000); CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    u32(bytes + 1044 + 16, 28); CHECK(!valid(bytes, n));
    n = fixture(bytes, false, false, 0);
    bytes[1044 + 65] = 'x'; CHECK(!valid(bytes, n));
    puts("PFTW new-header bounded CAB export regressions passed");
    return 0;
}
