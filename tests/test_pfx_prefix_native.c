/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT
 * Native regression: PFX producer framing and incomplete SFX member chains.
 * Fixtures are built in memory; no executable bytes are executed. */
#include "xxfclib/formats/archive/larc_pfx/xx_larc_pfx.h"
#include "xxfclib/formats/archive/sfx_lha/xx_sfx_lha.h"
#include "xxfclib/formats/archive/lha/xx_lha.h"
#include "../src/formats/archive/larc_pfx/xx_larc_pfx_stub.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); return 1; } } while (0)
static void le32(uint8_t *p, uint32_t v) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void be32(uint8_t *p, uint32_t v) { unsigned i; for (i = 0; i < 4; ++i) p[3 - i] = (uint8_t)(v >> (8 * i)); }
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t c = 0; size_t i; unsigned k;
    for (i = 0; i < n; ++i) { c ^= p[i]; for (k = 0; k < 8; ++k) c = (uint16_t)((c >> 1) ^ ((c & 1) ? 0xa001 : 0)); }
    return c;
}
static void checksum(uint8_t *p)
{
    unsigned i, sum = 0;
    for (i = 2; i < (unsigned)p[0] + 2; ++i) sum += p[i];
    p[1] = (uint8_t)sum;
}
static size_t member(uint8_t *p, const char *name, const char *method, const uint8_t *packed, size_t pn, size_t plain, uint16_t crc)
{
    size_t n = strlen(name), hs = n + 24;
    memset(p, 0, hs);
    p[0] = (uint8_t)(hs - 2);
    memcpy(p + 2, method, 5);
    le32(p + 7, (uint32_t)pn); le32(p + 11, (uint32_t)plain);
    p[19] = 0x20; p[21] = (uint8_t)n;
    memcpy(p + 22, name, n);
    p[22 + n] = (uint8_t)crc; p[23 + n] = (uint8_t)(crc >> 8);
    checksum(p);
    memcpy(p + hs, packed, pn);
    return hs + pn;
}
static size_t pfx(uint8_t *p, bool compact, bool two)
{
    static const uint8_t literal[] = {0x1f, 'h', 'e', 'l', 'l', 'o'};
    const uint8_t *body = compact ? pfx_113p_compact_body : pfx_113p_standard_body;
    size_t bn = compact ? sizeof(pfx_113p_compact_body) : sizeof(pfx_113p_standard_body);
    size_t start = 74 + bn, n;
    memset(p, 0, 2048);
    p[0] = 0x60; p[1] = 0x1a;
    p[28] = 0x60; p[29] = 0x2c;
    be32(p + 2, (uint32_t)(start - 28));
    memcpy(p + 30, "LArc's PFX 1.13P", 16);
    memcpy(p + 74, body, bn);
    n = member(p + start, "C:\\GFA\\HELLO.PRG", "-lz5-", literal, sizeof(literal), 5, 0x1234);
    if (two) n += member(p + start + n, "second.prg", "-lz5-", literal, sizeof(literal), 5, 0x5678);
    be32(p + 6, (uint32_t)n);
    return start + n + 4;
}
static int check_pfx(void)
{
    uint8_t bytes[2048], bad[2048]; size_t n, start;
    xx_io_device *d; xx_larc_pfx r; xx_archive_record_state *s; xx_pd_struct pd;
    unsigned compact;
    for (compact = 0; compact < 2; ++compact) {
        n = pfx(bytes, compact != 0, true);
        d = xx_io_mem_open_ro(bytes, n); CHECK(d);
        xx_larc_pfx_init(&r, d, 0);
        CHECK(xx_larc_pfx_check_is_valid(&r.format, NULL));
        s = xx_format_create_archive_records_reading(&r.format, NULL, NULL); CHECK(s && s->total_records == 2);
        pd = xx_pd_init();
        CHECK(xx_format_unpack_current_archive_record(&r.format, s, &pd));
        CHECK(xx_format_archive_record_move_to_next(&r.format, s, &pd));
        CHECK(xx_format_unpack_current_archive_record(&r.format, s, &pd));
        CHECK(!xx_format_archive_record_move_to_next(&r.format, s, &pd) && !pd.last_error);
        xx_format_free_archive_records_reading(&r.format, s);
        xx_larc_pfx_destroy(&r); xx_io_close(d);
        /* The same CRC-mismatched LZ5 member remains invalid ordinary LHA. */
        start = compact ? 414 : 420;
        d = xx_io_mem_open_ro(bytes + start, n - start - 4); CHECK(d);
        { xx_lha l; xx_lha_init(&l, d, 0); l.sanitize_sfx_drive = true;
          s = xx_lha_create_archive_records_reading(&l.format, NULL, NULL); CHECK(s);
          pd = xx_pd_init(); CHECK(!xx_lha_unpack_current_archive_record(&l.format, s, &pd) && pd.last_error);
          xx_lha_free_archive_records_reading(&l.format, s); xx_lha_destroy(&l); }
        xx_io_close(d);
        /* A banner with modified instructions cannot gain the PFX policy. */
        memcpy(bad, bytes, n); bad[100] ^= 1;
        d = xx_io_mem_open_ro(bad, n); CHECK(d); xx_larc_pfx_init(&r, d, 0);
        CHECK(!xx_larc_pfx_check_is_valid(&r.format, NULL)); xx_larc_pfx_destroy(&r); xx_io_close(d);
        /* Erasing only the banner retains the exact producer signature. */
        memcpy(bad, bytes, n); memset(bad + 30, 0, 44);
        d = xx_io_mem_open_ro(bad, n); CHECK(d); xx_larc_pfx_init(&r, d, 0);
        CHECK(xx_larc_pfx_check_is_valid(&r.format, NULL)); xx_larc_pfx_destroy(&r); xx_io_close(d);
        /* A corrupt header sum must never be treated as producer metadata. */
        memcpy(bad, bytes, n); bad[start + 1] ^= 1;
        d = xx_io_mem_open_ro(bad, n); CHECK(d); xx_larc_pfx_init(&r, d, 0);
        CHECK(!xx_larc_pfx_check_is_valid(&r.format, NULL)); xx_larc_pfx_destroy(&r); xx_io_close(d);
        /* A bounded compressed stream with extra nonzero bytes is rejected
         * by full LZ5 accounting even under the producer CRC policy. */
        n = pfx(bad, compact != 0, false);
        bad[n - 4] = 0x7f;
        le32(bad + start + 7, 7); checksum(bad + start); be32(bad + 6, (uint32_t)(n - start - 3));
        d = xx_io_mem_open_ro(bad, n + 1); CHECK(d); xx_larc_pfx_init(&r, d, 0);
        s = xx_format_create_archive_records_reading(&r.format, NULL, NULL); CHECK(s);
        CHECK(!xx_format_unpack_current_archive_record(&r.format, s, NULL));
        xx_format_free_archive_records_reading(&r.format, s); xx_larc_pfx_destroy(&r); xx_io_close(d);
    }
    return 0;
}
static int check_prefix(void)
{
    uint8_t bytes[2048]; const uint8_t plain[] = {'h', 'e', 'l', 'l', 'o'};
    size_t first, second, n; xx_io_device *d; xx_sfx_lha r; xx_archive_record_state *s; xx_pd_struct pd;
    memset(bytes, 0, sizeof(bytes)); memcpy(bytes, "MZ", 2); bytes[2] = 64; bytes[4] = 1; bytes[8] = 4;
    first = member(bytes + 64, "first.txt", "-lh0-", plain, 5, 5, crc16(plain, 5));
    second = member(bytes + 64 + first, "next.wad", "-lh0-", plain, 5, 100, 0);
    le32(bytes + 64 + first + 7, 100); checksum(bytes + 64 + first);
    n = 64 + first + second;
    d = xx_io_mem_open_ro(bytes, n); CHECK(d); xx_sfx_lha_init(&r, d, 0);
    CHECK(xx_sfx_lha_handle_base_info(&r.format, NULL) && r.incomplete_tail && r.format.number_of_archive_records == 1);
    s = xx_format_create_archive_records_reading(&r.format, NULL, NULL); CHECK(s);
    CHECK(xx_format_get_current_archive_record(&r.format, s)->header_offset == 64);
    CHECK(xx_format_get_current_archive_record(&r.format, s)->data_offset == 64 + 33);
    pd = xx_pd_init(); CHECK(xx_format_unpack_current_archive_record(&r.format, s, &pd));
    CHECK(!xx_format_archive_record_move_to_next(&r.format, s, &pd) && pd.last_error && strstr(pd.error_string, "next member"));
    xx_format_free_archive_records_reading(&r.format, s); xx_sfx_lha_destroy(&r); xx_io_close(d);
    /* A carrier inside another device has physical public offsets, while
     * its codec still reads the bounded memory view. Re-reading a record
     * must not translate it twice; next must translate its fresh record. */
    {
        uint8_t nested[4096];
        size_t second_full, last, end;
        const xx_archive_record *record;
        memset(nested, 0x55, sizeof(nested));
        memcpy(nested + 17, bytes, 64 + first);
        second_full = member(nested + 17 + 64 + first, "second.txt", "-lh0-", plain, 5, 5, crc16(plain, 5));
        end = 17 + 64 + first + second_full;
        last = member(nested + end, "next.wad", "-lh0-", plain, 5, 100, 0);
        le32(nested + end + 7, 100); checksum(nested + end);
        d = xx_io_mem_open_ro(nested, end + last); CHECK(d); xx_sfx_lha_init(&r, d, 17);
        CHECK(xx_sfx_lha_handle_base_info(&r.format, NULL) && r.incomplete_tail && r.format.number_of_archive_records == 2);
        s = xx_format_create_archive_records_reading(&r.format, NULL, NULL); CHECK(s);
        record = xx_format_get_current_archive_record(&r.format, s);
        CHECK(record && record->header_offset == 81 && record->data_offset == 114);
        CHECK(xx_format_get_current_archive_record(&r.format, s)->header_offset == 81);
        pd = xx_pd_init(); CHECK(xx_format_unpack_current_archive_record(&r.format, s, &pd));
        CHECK(xx_format_archive_record_move_to_next(&r.format, s, &pd));
        record = xx_format_get_current_archive_record(&r.format, s);
        CHECK(record && record->header_offset == (int64_t)(81 + first) && record->data_offset == (int64_t)(81 + first + 34));
        CHECK(xx_format_unpack_current_archive_record(&r.format, s, &pd));
        CHECK(xx_format_get_current_archive_record(&r.format, s)->header_offset == (int64_t)(81 + first));
        CHECK(!xx_format_archive_record_move_to_next(&r.format, s, &pd) && pd.last_error);
        xx_format_free_archive_records_reading(&r.format, s); xx_sfx_lha_destroy(&r); xx_io_close(d);
    }
    /* A payload CRC failure must discard the purported prefix. */
    bytes[64 + first - 1] ^= 1;
    d = xx_io_mem_open_ro(bytes, n); CHECK(d); xx_sfx_lha_init(&r, d, 0);
    CHECK(!xx_sfx_lha_check_is_valid(&r.format, NULL)); xx_sfx_lha_destroy(&r); xx_io_close(d);
    bytes[64 + first - 1] ^= 1;
    /* A malformed later header is not a proven incomplete volume. */
    bytes[64 + first + 1] ^= 1;
    d = xx_io_mem_open_ro(bytes, n); CHECK(d); xx_sfx_lha_init(&r, d, 0);
    CHECK(!xx_sfx_lha_check_is_valid(&r.format, NULL)); xx_sfx_lha_destroy(&r); xx_io_close(d);
    return 0;
}
int main(void)
{
    CHECK(!check_pfx()); CHECK(!check_prefix());
    puts("PFX producer and strict incomplete SFX prefix regressions passed");
    return 0;
}
