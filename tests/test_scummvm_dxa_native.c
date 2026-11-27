/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* SPDX-License-Identifier: MIT
 * Constructed DEXA fixtures; the private serializer is checked in memory so
 * palette colors, delta pixels and row padding are independently asserted. */
#include "../src/formats/video/scummvm_dxa/xx_scummvm_dxa.c"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "DXA line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void be16(uint8_t *p, unsigned n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
static void be32(uint8_t *p, uint32_t n) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> ((3U - i) * 8U)); }
static size_t frame(uint8_t *b, size_t at, unsigned method, const uint8_t *pixels)
{
    size_t n = 0;
    memcpy(b + at, "FRAM", 4); b[at + 4] = (uint8_t)method;
    CHECK(xx_zlib_stream_encode_memory(pixels, 6, b + at + 9, 100, &n, 6));
    be32(b + at + 5, (uint32_t)n); return at + 9 + n;
}
static size_t fixture(uint8_t *b, size_t *first)
{
    static const uint8_t key[6] = {1, 2, 3, 4, 5, 6}, delta[6] = {1, 1, 1, 1, 1, 1};
    size_t p = 19; unsigned i;
    memset(b, 0, 4096); memcpy(b, "DEXA", 4); be16(b + 5, 4); be32(b + 7, 66); be16(b + 11, 3); be16(b + 13, 2); memcpy(b + 15, "NULL", 4);
    memcpy(b + p, "CMAP", 4); p += 4;
    for (i = 0; i < 256; ++i) { b[p++] = (uint8_t)i; b[p++] = (uint8_t)(i + 1); b[p++] = (uint8_t)(i + 2); }
    *first = p; p = frame(b, p, 2, key);
    memcpy(b + p, "NULL", 4); p = frame(b, p + 4, 3, delta);
    memcpy(b + p, "NULLNULL", 8); p += 8;
    memcpy(b + p, "CMAP", 4); p += 4;
    memset(b + p, 0x42, 768); p += 768; memcpy(b + p, "NULL", 4); return p + 4;
}
static bool valid(const uint8_t *b, size_t n)
{
    xx_io_device *d = xx_io_mem_open_ro(b, n); xx_scummvm_dxa r; bool result;
    CHECK(d); xx_scummvm_dxa_init(&r, d, 0); result = xx_scummvm_dxa_check_is_valid(&r.format, NULL);
    xx_scummvm_dxa_destroy(&r); xx_io_close(d); return result;
}
int main(void)
{
    uint8_t b[4096], bmp[1100], previous[8]; size_t n, first, cut; unsigned i;
    sdxa_stream *s; xx_io_device *d, *out; xx_scummvm_dxa r; xx_pd_struct pd = xx_pd_init();
    n = fixture(b, &first); CHECK(valid(b, n));
    for (cut = 0; cut < n; ++cut) CHECK(!valid(b, cut));
    b[first + 4] = 12; CHECK(!valid(b, n)); b[first + 4] = 13; CHECK(!valid(b, n)); b[first + 4] = 2;
    b[4] = 1; CHECK(!valid(b, n)); b[4] = 0;
    be32(b + 7, 0x80000000U); CHECK(!valid(b, n)); be32(b + 7, 66);
    d = xx_io_mem_open_ro(b, n); CHECK(d); xx_scummvm_dxa_init(&r, d, 0);
    CHECK(xx_scummvm_dxa_handle_base_info(&r.format, NULL) && r.frame_count == 4 && r.width == 3 && r.height == 2);
    s = (sdxa_stream *)xx_mem_alloc(sizeof(*s)); CHECK(s); xx_mem_zero(s, sizeof(*s)); CHECK(sdxa_parse(&r.format, s, true, NULL));
    for (i = 0; i < 4; ++i) {
        unsigned j; memset(bmp, 0xcc, sizeof(bmp)); out = xx_io_mem_open(bmp, sizeof(bmp)); CHECK(out);
        CHECK(sdxa_bmp(&r.format, s, i, out, NULL)); CHECK(!xx_io_close(out));
        CHECK(!memcmp(bmp, "BM", 2) && xx_data_get_u32(bmp + 2, 4, 0, false) == 1086);
        CHECK(xx_data_get_u32(bmp + 22, 4, 0, false) == 0xfffffffeU && bmp[28] == 8);
        CHECK(bmp[1081] == 0 && bmp[1085] == 0);
        if (i == 0) for (j = 0; j < 6; ++j) CHECK(bmp[1078 + j + j / 3] == j + 1);
        else for (j = 0; j < 6; ++j) CHECK(bmp[1078 + j + j / 3] == ((j + 1) ^ 1U));
        if (i < 3) CHECK(bmp[58] == 3 && bmp[59] == 2 && bmp[60] == 1);
        else CHECK(bmp[58] == 0x42 && bmp[59] == 0x42 && bmp[60] == 0x42);
        if (i == 1) memcpy(previous, bmp + 1078, 8);
        if (i == 2) CHECK(!memcmp(previous, bmp + 1078, 8));
    }
    /* Both height flags retain the complete display size; the interlaced
     * unencoded lines contain index zero even when color zero is nonblack. */
    s->height = 4; s->flags = 0x40;
    out = xx_io_mem_open(bmp, sizeof(bmp)); CHECK(out);
    CHECK(sdxa_bmp(&r.format, s, 3, out, NULL)); CHECK(!xx_io_close(out));
    CHECK(!memcmp(bmp + 1078, bmp + 1082, 4) && !memcmp(bmp + 1086, bmp + 1090, 4));
    s->flags = 0x80; out = xx_io_mem_open(bmp, sizeof(bmp)); CHECK(out);
    CHECK(sdxa_bmp(&r.format, s, 3, out, NULL)); CHECK(!xx_io_close(out));
    CHECK(!memcmp(bmp + 1082, "\0\0\0\0", 4) && !memcmp(bmp + 1090, "\0\0\0\0", 4));
    xx_pd_stop(&pd); CHECK(!sdxa_bmp(&r.format, s, 3, NULL, &pd)); sdxa_free(s);
    xx_scummvm_dxa_destroy(&r); CHECK(!xx_io_close(d));
    /* The container can be structurally valid while a payload checksum fails. */
    b[first + 9 + xx_data_get_u32(b + first + 5, 4, 0, true) - 1] ^= 1;
    CHECK(valid(b, n)); d = xx_io_mem_open_ro(b, n); CHECK(d); xx_scummvm_dxa_init(&r, d, 0);
    {
        xx_archive_record_state *state = xx_scummvm_dxa_create_archive_records_reading(&r.format, NULL, NULL); CHECK(state);
        CHECK(!xx_scummvm_dxa_unpack_current_archive_record(&r.format, state, NULL));
        xx_scummvm_dxa_free_archive_records_reading(&r.format, state);
    }
    xx_scummvm_dxa_destroy(&r); CHECK(!xx_io_close(d));
    puts("ScummVM DXA native BMP, XOR, palette, bounds and checksum regressions passed"); return 0;
}
