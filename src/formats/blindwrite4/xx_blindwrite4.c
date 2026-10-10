/* SPDX-License-Identifier: MIT. Original BlindWrite4 descriptor/track parser.
 * Layout facts: Aaru v5.4.2 BlindWrite4/Structs.cs and field-reading offsets.
 * Companions are data only, opened from safe leaf names beside the descriptor.
 */
#include "xxfclib/formats/blindwrite4/xx_blindwrite4.h"
#include "../disk_additions/xx_disk_additions.h"
typedef struct b4_track {
    char file[256];
    uint32_t offset, adjust, end;
    int32_t pregap, start;
    uint8_t mode, point, session;
} b4_track;
typedef struct b4_desc {
    char data[256], sub[256];
    uint32_t count;
    b4_track tracks[256];
} b4_desc;
static bool b4_var(Abstractformat *f, uint64_t *at, char *out, size_t capacity, xx_pd_struct *pd)
{
    uint8_t h[4];
    uint32_t n;
    if (!da_read(f, *at, h, 4, pd)) return false;
    n = xx_data_get_u32(h, 4, 0, false);
    *at += 4U;
    if (n > 65536U || *at > (uint64_t)pm_available(f) || n > (uint64_t)pm_available(f) - *at) return false;
    if (out) {
        size_t i;
        if (n >= capacity || !da_read(f, *at, out, n, pd)) return false;
        out[n] = 0;
        for (i = 0; i < n; ++i)
            if (!out[i]) break;
        out[i] = 0;
    }
    *at += n;
    return da_poll(pd);
}
static bool b4_decode(Abstractformat *f, b4_desc *desc, xx_pd_struct *pd)
{
    uint8_t h[59];
    uint64_t at = 31, n = (uint64_t)pm_available(f);
    uint32_t i, j, count;
    xx_mem_zero(desc, sizeof(*desc));
    if (n > 16U * 1024U * 1024U || !da_read(f, 0, h, 31, pd) || xx_rt_memcmp(h, "BLINDWRITE TOC FILE", 19)) return false;
    for (i = 0; i < 3U; ++i)
        if (!b4_var(f, &at, NULL, 0, pd)) return false;
    if (!da_read(f, at, h, 4, pd)) {
        return false;
    }
    count = xx_data_get_u32(h, 4, 0, false);
    at += 4U;
    if (!count || count > 256U) return false;
    desc->count = count;
    if (!b4_var(f, &at, desc->data, sizeof(desc->data), pd) || !b4_var(f, &at, desc->sub, sizeof(desc->sub), pd) || !da_read(f, at, h, 5, pd)) {
        return false;
    }
    at += 5U;
    if (h[4] > n - at) return false;
    at += h[4];
    for (i = 0; i < count; ++i) {
        b4_track *track = desc->tracks + i;
        if (!b4_var(f, &at, track->file, sizeof(track->file), pd) || !da_read(f, at, h, 59, pd)) return false;
        at += 59;
        track->offset = xx_data_get_u32(h, 4, 0, false);
        track->session = h[13];
        track->mode = h[17];
        track->point = h[19];
        track->adjust = xx_data_get_u32(h + 28, 4, 0, false);
        track->end = xx_data_get_u32(h + 38, 4, 0, false);
        track->pregap = (int32_t)xx_data_get_u32(h + 43, 4, 0, false);
        track->start = (int32_t)xx_data_get_u32(h + 47, 4, 0, false);
        if (track->point < 0xa0U && (!track->point || track->point > 99U || track->mode > 2U || !track->session || track->pregap > track->start || track->start < 0 ||
                                     track->end <= (uint32_t)track->start))
            return false;
        for (j = 0; j < 15U; ++j)
            if (!b4_var(f, &at, NULL, 0, pd)) return false;
    }
    return at == n && da_poll(pd);
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    b4_desc desc;
    xx_disk_additions_info *r = (xx_disk_additions_info *)f;
    uint32_t i, actual = 0;
    char name[64];
    bool missing = false;
    r->incomplete = false;
    if (!b4_decode(f, &desc, pd) || !da_add(f, s, "descriptor.bwt", 0, (uint64_t)pm_available(f))) return false;
    for (i = 0; i < desc.count; ++i) {
        b4_track *t = desc.tracks + i;
        xx_io_device *data;
        int64_t start, raw, sub;
        uint64_t count;
        if (t->point >= 0xa0U) {
            continue;
        }
        ++actual;
        data = r->track_sources[i] ? r->track_sources[i] : r->companion;
        if (!data) {
            missing = true;
            continue;
        }
        start = t->pregap > 0 ? t->pregap : 0;
        count = (uint64_t)t->end - (uint64_t)start;
        raw = (int64_t)t->offset + ((int64_t)150 - t->adjust) * 2352;
        sub = ((int64_t)t->offset / 2352 + 150 - t->adjust) * 96;
        if (t->pregap > 0) {
            raw -= (int64_t)(t->start - t->pregap) * 2352;
            sub -= (int64_t)(t->start - t->pregap) * 96;
        }
        if (raw < 0 || sub < 0 || !count) return false;
        xx_rt_snprintf(name, sizeof(name), "session-%u-track-%02u-raw.bin", t->session, t->point);
        if (!da_one(f, s, name, (uint64_t)raw, 2352, count, 2352, -1, data)) return false;
        if (t->mode) {
            xx_rt_snprintf(name, sizeof(name), "session-%u-track-%02u-user-sectors.img", t->session, t->point);
            if (!da_one(f, s, name, (uint64_t)raw + 16U, t->mode == 1U ? 2048U : 2336U, count, 2352, -1, data)) return false;
        }
        if (desc.sub[0]) {
            if (!r->subchannel) {
                missing = true;
                continue;
            }
            xx_rt_snprintf(name, sizeof(name), "session-%u-track-%02u-subchannel.bin", t->session, t->point);
            if (!da_one(f, s, name, (uint64_t)sub, 96, count, 96, -1, r->subchannel)) return false;
        }
    }
    if (!actual || (missing && r->reading_records)) {
        return false;
    }
    r->incomplete = missing;
    s->size = pm_available(f);
    return true;
}
static xx_io_device *b4_sidecar(const char *descriptor, const char *stored)
{
    const char *leaf = stored;
    char *path;
    size_t i, dir = 0, len;
    xx_io_device *d;
    if (!descriptor || !stored) {
        return NULL;
    }
    for (i = 0; stored[i]; ++i)
        if (stored[i] == '/' || stored[i] == '\\') leaf = stored + i + 1;
    len = xx_rt_strlen(leaf);
    if (!len || len > 255U || !xx_rt_strcmp(leaf, ".") || !xx_rt_strcmp(leaf, "..")) return NULL;
    for (i = 0; i < len; ++i)
        if ((unsigned char)leaf[i] < 32U || (unsigned char)leaf[i] > 126U || leaf[i] == ':' || leaf[i] == '*' || leaf[i] == '?' || leaf[i] == '"' || leaf[i] == '<' ||
            leaf[i] == '>' || leaf[i] == '|' || (i + 1U == len && (leaf[i] == '.' || leaf[i] == ' ')))
            return NULL;
    {
        char upper[5] = {0};
        size_t k = 0;
        while (k < 4U && leaf[k] && leaf[k] != '.') {
            upper[k] = (char)(leaf[k] >= 'a' && leaf[k] <= 'z' ? leaf[k] - 32 : leaf[k]);
            ++k;
        }
        if (!xx_rt_strcmp(upper, "CON") || !xx_rt_strcmp(upper, "PRN") || !xx_rt_strcmp(upper, "AUX") || !xx_rt_strcmp(upper, "NUL") ||
            ((!xx_rt_memcmp(upper, "COM", 3) || !xx_rt_memcmp(upper, "LPT", 3)) && upper[3] >= '1' && upper[3] <= '9'))
            return NULL;
    }
    for (i = 0; descriptor[i]; ++i) {
        if (descriptor[i] == '/' || descriptor[i] == '\\') dir = i + 1U;
    }
    if (dir > SIZE_MAX - len - 1U) return NULL;
    path = (char *)xx_mem_alloc(dir + len + 1U);
    if (!path) return NULL;
    xx_rt_memcpy(path, descriptor, dir);
    xx_rt_memcpy(path + dir, leaf, len + 1U);
    d = xx_io_file_open(path, "rb");
    xx_mem_free(path);
    return d;
}
uint32_t xx_blindwrite4_open_data_files(xx_blindwrite4 *r, const char *path)
{
    b4_desc desc;
    uint32_t i, count = 0;
    if (!r || !path || !b4_decode(&r->format, &desc, NULL)) return 0;
    if (!r->companion && desc.data[0]) {
        r->companion = b4_sidecar(path, desc.data);
        r->owns_companion = r->companion != NULL;
    }
    if (r->companion) ++count;
    if (!r->subchannel && desc.sub[0]) {
        r->subchannel = b4_sidecar(path, desc.sub);
        r->owns_subchannel = r->subchannel != NULL;
    }
    if (r->subchannel) ++count;
    for (i = 0; i < desc.count; ++i)
        if (desc.tracks[i].point < 0xa0U) {
            if (!r->track_sources[i] && desc.tracks[i].file[0]) {
                r->track_sources[i] = b4_sidecar(path, desc.tracks[i].file);
                r->owns_tracks[i] = r->track_sources[i] != NULL;
            }
            if (r->track_sources[i]) ++count;
        }
    r->format.is_valid = false;
    r->format.base_info_handled = false;
    return count;
}
void xx_blindwrite4_set_companions(xx_blindwrite4 *r, xx_io_device *data, xx_io_device *sub)
{
    if (!r) return;
    if (r->owns_companion && r->companion) xx_io_close(r->companion);
    if (r->owns_subchannel && r->subchannel) xx_io_close(r->subchannel);
    r->companion = data;
    r->subchannel = sub;
    r->owns_companion = r->owns_subchannel = false;
    r->format.is_valid = false;
    r->format.base_info_handled = false;
}
DA_API(blindwrite4, XX_FILE_TYPE_BLINDWRITE4, "bwt")
