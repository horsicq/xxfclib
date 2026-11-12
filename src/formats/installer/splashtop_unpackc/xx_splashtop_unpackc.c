/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Splashtop "unpackC" setup wrapper (the Splashtop Personal / Streamer
 * setup.exe; the stub logs to unpackC_*.log, unpacks its items to a
 * temporary folder and runs run.bat). Layout, measured on Splashtop
 * Personal 3.5.801:
 *
 *   overlay (the end of the furthest section's raw data)
 *     record            284 bytes, then the file's bytes
 *       u32 magic       4
 *       u32 item        the item's number
 *       u64 size        the file's bytes
 *       u64 cumulative  the sizes of this file and all before it
 *       char name[260]  NUL-terminated
 *     ... one record per file (setup.msi, run.bat, setup.ini,
 *         PreVerCheck.exe) ...
 *     record            magic and item FFFFFFFF: the end (its size and
 *                       cumulative fields hold leftovers, not FF)
 *     Authenticode signature (not a member)
 *
 * Every record is checked against the running total and the end record is
 * required, so the chain cannot drift into foreign data or pass truncated.
 * Members are the files as stored; the .msi opens with the cfbf reader.
 * Nothing is executed.
 */
#include "xxfclib/formats/installer/splashtop_unpackc/xx_splashtop_unpackc.h"
#include "../../common/xx_executable_carrier.h"

#define SU_RECORD 284U
#define SU_MAGIC 4U
#define SU_NAME 260U
#define SU_MAX_FILES 256U

/* A NUL-terminated printable name of 1..259 bytes. */
static bool su_name_ok(const uint8_t *name)
{
    size_t i;
    for (i = 0; i < SU_NAME && name[i]; ++i) {
        if (name[i] < 0x20U || name[i] == 0x7FU) return false;
    }
    return i > 0 && i < SU_NAME;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t record[SU_RECORD];
    int64_t overlay, cabinet, cabinet_end, available = pm_available(f), at;
    uint64_t total = 0;
    unsigned count = 0;
    bool ended = false;
    if (!carrier_pe(f, &overlay, &cabinet, &cabinet_end, pd) || overlay <= 0 || available - overlay < (int64_t)(2U * SU_RECORD)) return false;
    at = overlay;
    while (available - at >= (int64_t)SU_RECORD) {
        uint32_t magic;
        uint64_t size, cumulative;
        if (carrier_stop(pd) || !pm_read(f, at, record, sizeof(record))) return false;
        magic = xx_data_get_u32(record, 4, 0, false);
        if (magic == 0xFFFFFFFFU && xx_data_get_u32(record + 4, 4, 0, false) == 0xFFFFFFFFU) {
            ended = true; /* the end record */
            break;
        }
        size = xx_data_get_u64(record + 8, 8, 0, false);
        cumulative = xx_data_get_u64(record + 16, 8, 0, false);
        if (magic != SU_MAGIC || ++count > SU_MAX_FILES || size > (uint64_t)(available - at - (int64_t)SU_RECORD) || cumulative != total + size || !su_name_ok(record + 24))
            return false;
        total += size;
        at += (int64_t)SU_RECORD;
        if (size && !executable_carrier_component(f, s, at, (int64_t)size, (const char *)(record + 24))) return false;
        at += (int64_t)size;
    }
    return ended && count > 0 && s->count > 0 && carrier_members(s, pd);
}

void xx_splashtop_unpackc_init(xx_splashtop_unpackc *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SPLASHTOP_UNPACKC, "exe");
    }
}
xx_splashtop_unpackc *xx_splashtop_unpackc_create(xx_io_device *d, int64_t b)
{
    xx_splashtop_unpackc *r = (xx_splashtop_unpackc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_splashtop_unpackc_init(r, d, b);
    return r;
}
void xx_splashtop_unpackc_destroy(xx_splashtop_unpackc *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_splashtop_unpackc_free(xx_splashtop_unpackc *r)
{
    if (r) {
        xx_splashtop_unpackc_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_splashtop_unpackc_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_splashtop_unpackc_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
