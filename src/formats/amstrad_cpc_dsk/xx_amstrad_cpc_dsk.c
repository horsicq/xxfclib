/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://github.com/ColinPitrat/caprice32/blob/master/src/slotshandler.cpp
 * Standard/extended CPC DSK, at most 84 tracks/two sides and 29 sectors per track; exports original sector bytes and track descriptors. Weak/multiple-copy sectors
 * retained as original bytes. LibDsk 1.5.22's drvcpcem.c defines the optional EDSK Offset-Info trailer: 15 signature bytes followed by one LE16 track length and one LE16
 * timing offset per sector in every formatted track. Preserve the complete trailer.
 */
#include "xxfclib/formats/amstrad_cpc_dsk/xx_amstrad_cpc_dsk.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

/* The shared retro helper stops at 4096 members, while one legal CPC image
 * can contain 84 * 2 tracks with 29 sectors each. Include the disk header,
 * every track descriptor, and one possible trailer in the local bound. */
#define CPC_MAX_MEMBERS (1U + 84U * 2U * (1U + 29U) + 1U)
static bool cpc_emit(Abstractformat *f, pm_stream *s, const retro_disk_blob *b, const char *name, uint32_t at, uint32_t size)
{
    return s->count < CPC_MAX_MEMBERS && retro_disk_poll(b) && retro_disk_range(b, at, size) && pm_add(f, s, name, at, size);
}

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t tracks, sides, at = 256, i, nonempty = 0, offset_entries = 0;
    bool ext;
    char name[48];
    if (!retro_disk_range(b, 0, 256)) return false;
    ext = !xx_rt_memcmp(b->p, "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
    if ((!ext && xx_rt_memcmp(b->p, "MV - CPCEMU Disk-File\r\nDisk-Info\r\n", 34)) || !(tracks = b->p[48]) || tracks > 84 || !(sides = b->p[49]) || sides > 2 ||
        (!ext && xx_data_get_u16(b->p + 50, 2, 0, false) < 256) || !cpc_emit(f, s, b, "disk-descriptor.bin", 0, 256))
        return false;
    for (i = 0; i < tracks * sides; ++i) {
        uint32_t z = ext ? (uint32_t)b->p[52 + i] * 256U : xx_data_get_u16(b->p + 50, 2, 0, false), j, pos, ns;
        const uint8_t *p;
        uint8_t ids[256] = {0};
        if (!z) continue;
        if (z < 256 || !retro_disk_range(b, at, z) || xx_rt_memcmp(b->p + at, "Track-Info\r\n", 12)) {
            return false;
        }
        p = b->p + at;
        if (p[16] != i / sides || p[17] != i % sides || p[20] > 7 || (ns = p[21]) > 29) return false;
        if (ext) offset_entries += 1U + ns;
        xx_rt_snprintf(name, sizeof(name), "track-%u-descriptor.bin", i);
        if (!cpc_emit(f, s, b, name, at, 256)) return false;
        pos = 256;
        for (j = 0; j < ns; ++j) {
            const uint8_t *e = p + 24 + j * 8;
            uint32_t len;
            if (e[3] > 7) return false;
            /* EDSK records physical sectors in track order; copy-protected disks may
             * repeat an ID with different data. Keep the historical name for the
             * first occurrence and disambiguate each later physical occurrence. */
            ++ids[e[2]];
            len = ext ? xx_data_get_u16(e + 6, 2, 0, false) : 128U << p[20];
            if (!len || len > z - pos) return false;
            if (ids[e[2]] == 1U) xx_rt_snprintf(name, sizeof(name), "track-%u-sector-%u.bin", i, e[2]);
            else xx_rt_snprintf(name, sizeof(name), "track-%u-sector-%u-instance-%u.bin", i, e[2], ids[e[2]]);
            if (!cpc_emit(f, s, b, name, at + pos, len)) {
                return false;
            }
            pos += len;
            ++nonempty;
        }
        at += z;
    }
    if (!nonempty) return false;
    if (at < b->n) {
        uint32_t remaining = b->n - at;
        static const char offset_magic[] = "Offset-Info\r\n";
        if (ext && remaining >= 13U && !xx_rt_memcmp(b->p + at, offset_magic, 13U)) {
            uint32_t expected = 15U + 2U * offset_entries;
            if (remaining != expected || b->p[at + 13U] || b->p[at + 14U] || !cpc_emit(f, s, b, "offset-info.bin", at, remaining)) return false;
        } else {
            /* Unknown trailing bytes remain extractable without assigning them
             * Offset-Info semantics. A truncated magic prefix is corrupt. */
            if (ext && remaining < 13U && !xx_rt_memcmp(b->p + at, offset_magic, remaining)) return false;
            if (!cpc_emit(f, s, b, "trailer.bin", at, remaining)) return false;
        }
    }
    s->size = b->n;
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_disk_blob b;
    bool ok;
    if (!retro_disk_load(f, &b, pd)) return false;
    ok = parse_blob(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}

void xx_amstrad_cpc_dsk_init(xx_amstrad_cpc_dsk *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AMSTRAD_CPC_DSK, "dsk");
    }
}
xx_amstrad_cpc_dsk *xx_amstrad_cpc_dsk_create(xx_io_device *d, int64_t b)
{
    xx_amstrad_cpc_dsk *r = (xx_amstrad_cpc_dsk *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amstrad_cpc_dsk_init(r, d, b);
    return r;
}
void xx_amstrad_cpc_dsk_destroy(xx_amstrad_cpc_dsk *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amstrad_cpc_dsk_free(xx_amstrad_cpc_dsk *r)
{
    if (r) {
        xx_amstrad_cpc_dsk_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amstrad_cpc_dsk_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amstrad_cpc_dsk_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
